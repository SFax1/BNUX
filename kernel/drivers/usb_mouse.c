#include <bnux/types.h>

extern void kprintf(const char *fmt, ...);
extern int uhci_control_transfer(uint8_t device_addr, uint8_t req_type, uint8_t request,
                                  uint16_t value, uint16_t index, uint16_t length,
                                  void *data_inout, int in_data, int low_speed);
extern int uhci_interrupt_in(uint8_t device_addr, uint8_t endpoint, int toggle, int len,
                              uint8_t *out_buf, int low_speed);
extern int uhci_port_connected(int port);
extern int uhci_port_low_speed(int port);
extern void uhci_reset_port(int port);

extern void mouse_set_position_delta(int32_t dx, int32_t dy);
extern void mouse_set_buttons(int left, int right);

/* --- стандартные USB request codes --- */
#define REQ_GET_DESCRIPTOR 0x06
#define REQ_SET_ADDRESS     0x05
#define REQ_SET_CONFIG      0x09
#define REQ_SET_PROTOCOL    0x0B // class-specific HID request

#define REQTYPE_DEV_TO_HOST_STD  0x80
#define REQTYPE_HOST_TO_DEV_STD  0x00
#define REQTYPE_HOST_TO_DEV_CLASS_IFACE 0x21

#define DESC_DEVICE 0x01
#define DESC_CONFIG 0x02

static int usb_mouse_ready = 0;
static uint8_t mouse_device_addr = 1;
static uint8_t mouse_endpoint = 1;
static int mouse_toggle = 0;
static int mouse_low_speed = 0; // определяется реально через статус порта, не угадываем

/* ищет первый прерывающий IN endpoint в дампе config descriptor —
 * стандартный простейший разбор, без учёта нескольких интерфейсов */
static int find_interrupt_in_endpoint(uint8_t *desc, int len, uint8_t *out_ep) {
    int i = 0;
    while (i + 2 <= len) {
        uint8_t dlen = desc[i];
        uint8_t dtype = desc[i + 1];
        if (dlen == 0) break;
        if (dtype == 0x05 && i + 6 <= len) { // endpoint descriptor
            uint8_t ep_addr = desc[i + 2];
            uint8_t attrs = desc[i + 3];
            if ((ep_addr & 0x80) && (attrs & 0x03) == 0x03) { // IN + Interrupt
                *out_ep = ep_addr & 0x0F;
                return 1;
            }
        }
        i += dlen;
    }
    return 0;
}

/* Полная последовательность enumeration + переключение в HID boot
 * protocol. Печатает диагностику на каждом шаге — если что-то пойдёт
 * не так, будет видно ГДЕ именно. САМЫЙ рискованный код во всём
 * проекте — контроллер работает с DMA-структурами (TD/QH) напрямую,
 * без вмешательства CPU в момент передачи. */
int usb_mouse_init(void) {
    if (!uhci_port_connected(0)) {
        kprintf("[usbmouse] на порту 0 ничего не подключено\n");
        return -1;
    }

    kprintf("[usbmouse] сбрасываю порт 0...\n");
    uhci_reset_port(0);

    if (!uhci_port_connected(0)) {
        kprintf("[usbmouse] устройство пропало после сброса порта\n");
        return -1;
    }

    mouse_low_speed = uhci_port_low_speed(0);
    kprintf("[usbmouse] скорость устройства: %s\n", mouse_low_speed ? "LOW-SPEED" : "FULL-SPEED");

    /* --- 1. GET_DESCRIPTOR(DEVICE, 8 байт) на адрес 0 (устройство
     * по умолчанию отвечает на адрес 0 до SET_ADDRESS) --- */
    uint8_t dev_desc[18];
    for (int i = 0; i < 18; i++) dev_desc[i] = 0;

    int r = uhci_control_transfer(0, REQTYPE_DEV_TO_HOST_STD, REQ_GET_DESCRIPTOR,
                                   (DESC_DEVICE << 8), 0, 8, dev_desc, 1, mouse_low_speed);
    if (r != 0) { kprintf("[usbmouse] GET_DESCRIPTOR(8) не удался\n"); return -1; }
    kprintf("[usbmouse] bMaxPacketSize0 = %d\n", dev_desc[7]);

    /* --- 2. SET_ADDRESS --- */
    r = uhci_control_transfer(0, REQTYPE_HOST_TO_DEV_STD, REQ_SET_ADDRESS,
                               mouse_device_addr, 0, 0, NULL, 0, mouse_low_speed);
    if (r != 0) { kprintf("[usbmouse] SET_ADDRESS не удался\n"); return -1; }
    kprintf("[usbmouse] адрес назначен: %d\n", mouse_device_addr);

    /* --- 3. Полный GET_DESCRIPTOR(DEVICE, 18 байт) на новом адресе --- */
    r = uhci_control_transfer(mouse_device_addr, REQTYPE_DEV_TO_HOST_STD, REQ_GET_DESCRIPTOR,
                               (DESC_DEVICE << 8), 0, 18, dev_desc, 1, mouse_low_speed);
    if (r != 0) { kprintf("[usbmouse] полный GET_DESCRIPTOR не удался\n"); return -1; }
    kprintf("[usbmouse] VID=0x%x PID=0x%x класс=0x%x\n",
            dev_desc[8] | (dev_desc[9] << 8),
            dev_desc[10] | (dev_desc[11] << 8),
            dev_desc[4]);

    /* --- 4. GET_DESCRIPTOR(CONFIG) — сначала 9 байт чтобы узнать
     * wTotalLength, потом всё целиком --- */
    uint8_t config_desc[64];
    for (int i = 0; i < 64; i++) config_desc[i] = 0;

    r = uhci_control_transfer(mouse_device_addr, REQTYPE_DEV_TO_HOST_STD, REQ_GET_DESCRIPTOR,
                               (DESC_CONFIG << 8), 0, 9, config_desc, 1, mouse_low_speed);
    if (r != 0) { kprintf("[usbmouse] GET_DESCRIPTOR(CONFIG,9) не удался\n"); return -1; }

    uint16_t total_len = config_desc[2] | (config_desc[3] << 8);
    if (total_len > 64) total_len = 64;
    kprintf("[usbmouse] config descriptor: %d байт всего\n", total_len);

    r = uhci_control_transfer(mouse_device_addr, REQTYPE_DEV_TO_HOST_STD, REQ_GET_DESCRIPTOR,
                               (DESC_CONFIG << 8), 0, total_len, config_desc, 1, mouse_low_speed);
    if (r != 0) { kprintf("[usbmouse] полный GET_DESCRIPTOR(CONFIG) не удался\n"); return -1; }

    uint8_t config_value = config_desc[5];

    if (!find_interrupt_in_endpoint(config_desc, total_len, &mouse_endpoint)) {
        kprintf("[usbmouse] не нашёл interrupt IN endpoint в дескрипторе\n");
        return -1;
    }
    kprintf("[usbmouse] найден endpoint IN%d\n", mouse_endpoint);

    /* --- 5. SET_CONFIGURATION --- */
    r = uhci_control_transfer(mouse_device_addr, REQTYPE_HOST_TO_DEV_STD, REQ_SET_CONFIG,
                               config_value, 0, 0, NULL, 0, mouse_low_speed);
    if (r != 0) { kprintf("[usbmouse] SET_CONFIGURATION не удался\n"); return -1; }
    kprintf("[usbmouse] конфигурация %d выбрана\n", config_value);

    /* --- 6. HID SET_PROTOCOL(BOOT) на интерфейс 0 — упрощённый формат
     * отчётов (3-4 байта: кнопки, dx, dy), не надо парсить HID report
     * descriptor --- */
    r = uhci_control_transfer(mouse_device_addr, REQTYPE_HOST_TO_DEV_CLASS_IFACE, REQ_SET_PROTOCOL,
                               0 /* boot protocol */, 0 /* interface 0 */, 0, NULL, 0, mouse_low_speed);
    if (r != 0) {
        kprintf("[usbmouse] SET_PROTOCOL(BOOT) не удался — пробуем работать как есть\n");
        /* не фатально: некоторые устройства уже в boot protocol по умолчанию */
    } else {
        kprintf("[usbmouse] переключено в HID boot protocol\n");
    }

    mouse_toggle = 0;
    usb_mouse_ready = 1;
    kprintf("[usbmouse] готово! addr=%d endpoint=IN%d\n", mouse_device_addr, mouse_endpoint);
    return 0;
}

int usb_mouse_is_ready(void) {
    return usb_mouse_ready;
}

/* Один опрос — вызывать регулярно (каждый кадр GUI). Если данных нет
 * (обычная ситуация, мышь не двигалась) — просто ничего не делает. */
void usb_mouse_poll(void) {
    if (!usb_mouse_ready) return;

    uint8_t report[8];
    int n = uhci_interrupt_in(mouse_device_addr, mouse_endpoint, mouse_toggle, 4, report, mouse_low_speed);
    if (n < 0) return; // NAK или STALL — не критично, попробуем в следующий раз

    mouse_toggle ^= 1;
    if (n < 3) return; // отчёт короче ожидаемого — игнорируем

    uint8_t buttons = report[0];
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];

    mouse_set_position_delta(dx, -dy); // HID: dy положительный = вниз, как и у PS/2
    mouse_set_buttons(buttons & 0x01, buttons & 0x02);
}
