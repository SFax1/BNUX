#include <bnux/types.h>
#include <bnux/io.h>

extern void kprintf(const char *fmt, ...);
extern void *pmm_alloc_page(void);
extern uint64_t vmm_get_hhdm_offset(void);
extern int pci_find_uhci(uint32_t *out_io_base);

/* --- UHCI I/O-регистры (смещения от io_base) --- */
#define UHCI_USBCMD    0x00
#define UHCI_USBSTS    0x02
#define UHCI_USBINTR   0x04
#define UHCI_FRNUM     0x06
#define UHCI_FRBASEADD 0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORTSC1   0x10
#define UHCI_PORTSC2   0x12

#define CMD_RS      0x0001 // Run/Stop
#define CMD_HCRESET 0x0002
#define CMD_GRESET  0x0004
#define CMD_CF      0x0040 // Configure Flag
#define CMD_MAXP    0x0080 // Max Packet (64 байта)

#define PORTSC_CONNECTED    0x0001
#define PORTSC_CONNECT_CHG  0x0002
#define PORTSC_ENABLED      0x0004
#define PORTSC_LOW_SPEED    0x0100
#define PORTSC_RESET        0x0200

static uint32_t io_base = 0;
static int uhci_ready = 0;
static uint32_t *frame_list = NULL;      // виртуальный указатель
static uint64_t frame_list_phys_addr = 0;
static uint8_t *usb_mem_virt = NULL;     // одна страница под TD/QH/буферы
static uint64_t usb_mem_phys = 0;

/* --- раскладка одной 4KB-страницы под структуры, с запасом между ---
 * слоты 0-7: TD (по 32 байта каждый = 256 байт)
 * offset 3584: Queue Head для control-transfer'ов
 * offset 3900: буфер под данные (дескрипторы, отчёты мыши)
 * offset 3968: 8-байтный SETUP-пакет */
#define TD_SLOT_SIZE   32
#define QH_OFFSET      3584
#define DATA_OFFSET    3900
#define SETUP_OFFSET   3968

struct PACKED uhci_td {
    uint32_t link;
    uint32_t cs;
    uint32_t token;
    uint32_t buffer;
} ALIGN(16);

struct PACKED uhci_qh {
    uint32_t head_link;
    uint32_t element_link;
} ALIGN(16);

#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

#define LINK_TERMINATE 0x1u
#define LINK_QH        0x2u
#define LINK_VF        0x4u // Depth-First — продолжать эту же цепочку TD сразу, не откладывать на следующий кадр

#define TD_CS_STALLED  (1u << 22)
#define TD_CS_ACTIVE   (1u << 23)
#define TD_CS_IOC      (1u << 24)
#define TD_CS_LS       (1u << 26)
#define TD_CS_CERR_SHIFT 27
#define TD_CS_SPD      (1u << 29)

static struct uhci_td *td_slot(int i) { return (struct uhci_td*)(usb_mem_virt + i * TD_SLOT_SIZE); }
static uint32_t td_slot_phys(int i)   { return (uint32_t)(usb_mem_phys + (uint32_t)(i * TD_SLOT_SIZE)); }
static struct uhci_qh *ctrl_qh(void)  { return (struct uhci_qh*)(usb_mem_virt + QH_OFFSET); }
static uint32_t ctrl_qh_phys(void)    { return (uint32_t)(usb_mem_phys + QH_OFFSET); }

static uint32_t enc_maxlen(int n) { return (n == 0) ? 0x7FFu : (uint32_t)(n - 1); }

static void td_init(struct uhci_td *td, uint32_t link, uint8_t pid, uint8_t addr,
                     uint8_t endpoint, int toggle, int len, uint32_t buffer_phys, int low_speed) {
    td->link = link;
    td->cs = TD_CS_ACTIVE | (low_speed ? TD_CS_LS : 0) | (3u << TD_CS_CERR_SHIFT);
    td->token = pid | ((uint32_t)addr << 8) | ((uint32_t)endpoint << 15) |
                (toggle ? (1u << 19) : 0) | (enc_maxlen(len) << 21);
    td->buffer = buffer_phys;
}

/* Ждёт завершения TD, привязываясь к РЕАЛЬНОМУ времени через FRNUM
 * (аппаратный счётчик USB-кадров, тикает каждую 1мс независимо от
 * скорости эмуляции/CPU) — а не голый busy-loop по числу итераций.
 * Раньше таймаут мог срабатывать быстрее, чем контроллер вообще успевал
 * обработать хотя бы один кадр — особенно заметно под QEMU, где busy-loop
 * может отработать 200000 итераций быстрее реальной 1мс. */
static int wait_td_complete(struct uhci_td *td, int max_frames) {
    uint16_t start_frame = inw((uint16_t)(io_base + UHCI_FRNUM));
    while (td->cs & TD_CS_ACTIVE) {
        uint16_t cur_frame = inw((uint16_t)(io_base + UHCI_FRNUM));
        uint16_t elapsed = (uint16_t)((cur_frame - start_frame) & 0x7FF);
        if (elapsed > (uint16_t)max_frames) return -1; // таймаут
    }
    return 0;
}

/* Диагностика на таймаут — печатает реальное состояние регистров и TD,
 * чтобы не гадать вслепую в третий раз. */
static void dump_diag(struct uhci_qh *qh, struct uhci_td *td_setup,
                       struct uhci_td *td_data, struct uhci_td *td_status) {
    uint16_t cmd = inw((uint16_t)(io_base + UHCI_USBCMD));
    uint16_t sts = inw((uint16_t)(io_base + UHCI_USBSTS));
    uint16_t frn = inw((uint16_t)(io_base + UHCI_FRNUM));
    kprintf("[uhci-diag] USBCMD=0x%x USBSTS=0x%x FRNUM=0x%x\n", cmd, sts, frn);
    kprintf("[uhci-diag] QH head=0x%x elem=0x%x\n", qh->head_link, qh->element_link);
    kprintf("[uhci-diag] TD_SETUP link=0x%x cs=0x%x token=0x%x\n",
            td_setup->link, td_setup->cs, td_setup->token);
    if (td_data) {
        kprintf("[uhci-diag] TD_DATA  link=0x%x cs=0x%x token=0x%x\n",
                td_data->link, td_data->cs, td_data->token);
    }
    kprintf("[uhci-diag] TD_STATUS link=0x%x cs=0x%x token=0x%x\n",
            td_status->link, td_status->cs, td_status->token);
}

/* грубая busy-wait задержка — у нас пока нет прерываний от таймера,
 * которые можно было бы использовать для точного сна */
static void busy_delay_ms(int ms) {
    for (volatile long i = 0; i < (long)ms * 100000; i++);
}

/* Инициализация контроллера: находим через PCI, делаем глобальный сброс
 * шины, сброс самого контроллера, ставим пустой (terminated) frame list,
 * запускаем. Никакого enumeration устройств тут ещё нет — это
 * отдельный следующий шаг, тут только "контроллер жив и видит порты". */
int uhci_init(void) {
    if (!pci_find_uhci(&io_base)) {
        kprintf("[uhci] UHCI-контроллер не найден через PCI\n");
        return -1;
    }
    kprintf("[uhci] найден контроллер, io_base=0x%x\n", io_base);

    /* глобальный сброс шины (USBCMD.GRESET) */
    outw(io_base + UHCI_USBCMD, CMD_GRESET);
    busy_delay_ms(50);
    outw(io_base + UHCI_USBCMD, 0);
    busy_delay_ms(10);

    /* сброс самого контроллера (USBCMD.HCRESET), бит сам сбросится в 0 */
    outw(io_base + UHCI_USBCMD, CMD_HCRESET);
    int tries = 1000;
    while ((inw(io_base + UHCI_USBCMD) & CMD_HCRESET) && tries > 0) tries--;
    if (tries <= 0) {
        kprintf("[uhci] HCRESET не завершился — контроллер не отвечает как ожидалось\n");
        return -1;
    }

    /* Frame List: 1024 записи по 4 байта = ровно одна 4KB страница,
     * должна быть выровнена на 4KB — идеально ложится на одну страницу
     * от PMM. Пока заполняем terminate-битом (ничего не запланировано),
     * реальные Queue Heads появятся когда будем делать enumeration. */
    void *frame_list_phys = pmm_alloc_page();
    if (!frame_list_phys) {
        kprintf("[uhci] не хватило памяти под frame list\n");
        return -1;
    }
    frame_list_phys_addr = (uint64_t)frame_list_phys;
    frame_list = (uint32_t*)((uint64_t)frame_list_phys + vmm_get_hhdm_offset());
    for (int i = 0; i < 1024; i++) frame_list[i] = LINK_TERMINATE;

    void *usb_mem = pmm_alloc_page();
    if (!usb_mem) {
        kprintf("[uhci] не хватило памяти под TD/QH\n");
        return -1;
    }
    usb_mem_phys = (uint64_t)usb_mem;
    usb_mem_virt = (uint8_t*)((uint64_t)usb_mem + vmm_get_hhdm_offset());
    for (int i = 0; i < 4096; i++) usb_mem_virt[i] = 0;

    outl(io_base + UHCI_FRBASEADD, (uint32_t)frame_list_phys_addr);
    outw(io_base + UHCI_FRNUM, 0);
    outb(io_base + UHCI_SOFMOD, 0x40); // стандартное значение, 1мс кадр

    /* запускаем контроллер */
    outw(io_base + UHCI_USBCMD, CMD_RS | CMD_CF | CMD_MAXP);
    busy_delay_ms(10);

    uhci_ready = 1;
    kprintf("[uhci] контроллер запущен\n");
    return 0;
}

/* --- Сброс и включение порта — обязательный шаг USB-спеки перед тем,
 * как устройству можно присваивать адрес. PORTSC_CONNECT_CHG и
 * PortEnableChange (0x0008) — биты "write-1-to-clear": если их не
 * маскировать при read-modify-write, можно случайно затереть статус,
 * который нам ещё пригодится для диагностики. */
#define PORTSC_W1C_MASK (PORTSC_CONNECT_CHG | 0x0008u)

void uhci_reset_port(int port) {
    uint16_t reg = (uint16_t)(io_base + (port == 0 ? UHCI_PORTSC1 : UHCI_PORTSC2));
    uint16_t val = inw(reg);
    outw(reg, (val & ~PORTSC_W1C_MASK) | PORTSC_RESET);
    busy_delay_ms(50); // USB-спека требует минимум 10мс, берём с запасом
    val = inw(reg);
    outw(reg, (val & ~PORTSC_W1C_MASK) & ~PORTSC_RESET);
    busy_delay_ms(10);

    val = inw(reg);
    outw(reg, (val & ~PORTSC_W1C_MASK) | PORTSC_ENABLED);
    busy_delay_ms(10);
}

int uhci_port_connected(int port) {
    uint16_t v = inw((uint16_t)(io_base + (port == 0 ? UHCI_PORTSC1 : UHCI_PORTSC2)));
    return (v & PORTSC_CONNECTED) ? 1 : 0;
}

int uhci_port_low_speed(int port) {
    uint16_t v = inw((uint16_t)(io_base + (port == 0 ? UHCI_PORTSC1 : UHCI_PORTSC2)));
    return (v & PORTSC_LOW_SPEED) ? 1 : 0;
}

/* Control transfer: SETUP + (опционально DATA) + STATUS, через один
 * постоянный Queue Head. Кладём QH во ВСЕ 1024 слота frame list сразу —
 * гарантирует, что контроллер подхватит его на следующем же кадре
 * (максимум ~1мс задержки), а не будет ждать своей очереди где-то в
 * расписании. После завершения — сразу убираем, иначе контроллер будет
 * бесконечно повторять уже готовый transfer. */
int uhci_control_transfer(uint8_t device_addr, uint8_t req_type, uint8_t request,
                           uint16_t value, uint16_t index, uint16_t length,
                           void *data_inout, int in_data, int low_speed) {
    if (!uhci_ready) return -1;

    uint8_t *setup = usb_mem_virt + SETUP_OFFSET;
    uint32_t setup_phys = (uint32_t)(usb_mem_phys + SETUP_OFFSET);
    setup[0] = req_type; setup[1] = request;
    setup[2] = (uint8_t)(value & 0xFF);  setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)(index & 0xFF);  setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)(length & 0xFF); setup[7] = (uint8_t)(length >> 8);

    uint8_t *data_buf = usb_mem_virt + DATA_OFFSET;
    uint32_t data_phys = (uint32_t)(usb_mem_phys + DATA_OFFSET);

    struct uhci_td *td_setup = td_slot(0);
    uint32_t td_setup_phys = td_slot_phys(0);
    struct uhci_td *td_data = (length > 0) ? td_slot(1) : NULL;
    uint32_t td_data_phys = td_slot_phys(1);
    struct uhci_td *td_status = td_slot(2);
    uint32_t td_status_phys = td_slot_phys(2);

    uint32_t after_setup = (td_data ? td_data_phys : td_status_phys) | LINK_VF;
    td_init(td_setup, after_setup, PID_SETUP, device_addr, 0, 0, 8, setup_phys, low_speed);

    if (td_data) {
        uint8_t pid = in_data ? PID_IN : PID_OUT;
        if (!in_data && data_inout) {
            for (int i = 0; i < length; i++) data_buf[i] = ((uint8_t*)data_inout)[i];
        }
        td_init(td_data, td_status_phys | LINK_VF, pid, device_addr, 0, 1, length, data_phys, low_speed);
    }

    uint8_t status_pid = (length == 0 || in_data) ? PID_OUT : PID_IN;
    td_init(td_status, LINK_TERMINATE, status_pid, device_addr, 0, 1, 0, 0, low_speed);
    td_status->cs |= TD_CS_IOC;

    struct uhci_qh *qh = ctrl_qh();
    qh->head_link = LINK_TERMINATE;
    qh->element_link = td_setup_phys;

    for (int i = 0; i < 1024; i++) frame_list[i] = ctrl_qh_phys() | LINK_QH;

    int r = wait_td_complete(td_status, 500); // до 500мс реального времени — щедро с запасом

    for (int i = 0; i < 1024; i++) frame_list[i] = LINK_TERMINATE;

    if (r != 0) {
        kprintf("[uhci] control transfer: таймаут\n");
        dump_diag(qh, td_setup, td_data, td_status);
        return -1;
    }
    if (td_setup->cs & TD_CS_STALLED) { kprintf("[uhci] SETUP stall\n"); return -1; }
    if (td_data && (td_data->cs & TD_CS_STALLED)) { kprintf("[uhci] DATA stall\n"); return -1; }
    if (td_status->cs & TD_CS_STALLED) { kprintf("[uhci] STATUS stall\n"); return -1; }

    if (in_data && data_inout && length > 0) {
        for (int i = 0; i < length; i++) ((uint8_t*)data_inout)[i] = data_buf[i];
    }
    return 0;
}

/* Один IN-transfer на прерывающий endpoint (для опроса отчётов мыши).
 * Возвращает число реально принятых байт, -1 при отсутствии данных
 * (устройству нечего сказать — нормальная ситуация большую часть
 * времени, НЕ ошибка), -2 при STALL. */
int uhci_interrupt_in(uint8_t device_addr, uint8_t endpoint, int toggle, int len,
                       uint8_t *out_buf, int low_speed) {
    if (!uhci_ready) return -1;

    struct uhci_td *td = td_slot(6);
    uint32_t td_phys = td_slot_phys(6);
    uint8_t *buf = usb_mem_virt + DATA_OFFSET + 64;
    uint32_t buf_phys = (uint32_t)(usb_mem_phys + DATA_OFFSET + 64);

    td_init(td, LINK_TERMINATE, PID_IN, device_addr, endpoint, toggle, len, buf_phys, low_speed);
    td->cs |= TD_CS_SPD;

    for (int i = 0; i < 1024; i++) frame_list[i] = td_phys;
    int r = wait_td_complete(td, 20); // опрос мыши — короткий таймаут, это часто вызывается
    for (int i = 0; i < 1024; i++) frame_list[i] = LINK_TERMINATE;

    if (r != 0) return -1;
    if (td->cs & TD_CS_STALLED) return -2;

    int actlen = (int)(td->cs & 0x7FF);
    actlen = (actlen == 0x7FF) ? 0 : actlen + 1;
    for (int i = 0; i < actlen && i < len; i++) out_buf[i] = buf[i];
    return actlen;
}

/* Проверяет оба порта (у UHCI их обычно 2 на контроллер) и печатает
 * что видит — есть ли физически подключённое устройство. Это ещё не
 * "мышь работает", это только "видим ли мы вообще что-то на шине". */
void uhci_check_ports(void) {
    if (!uhci_ready) {
        kprintf("[uhci] контроллер не инициализирован (сначала USBINIT)\n");
        return;
    }

    uint16_t p1 = inw(io_base + UHCI_PORTSC1);
    uint16_t p2 = inw(io_base + UHCI_PORTSC2);

    kprintf("[uhci] PORT1 = 0x%x  %s%s\n", p1,
            (p1 & PORTSC_CONNECTED) ? "УСТРОЙСТВО ЕСТЬ " : "пусто ",
            (p1 & PORTSC_LOW_SPEED) ? "(LOW-SPEED)" : "");
    kprintf("[uhci] PORT2 = 0x%x  %s%s\n", p2,
            (p2 & PORTSC_CONNECTED) ? "УСТРОЙСТВО ЕСТЬ " : "пусто ",
            (p2 & PORTSC_LOW_SPEED) ? "(LOW-SPEED)" : "");
}
