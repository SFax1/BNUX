#include <bnux/types.h>
#include <bnux/io.h>

#define MOUSE_PORT_DATA   0x60
#define MOUSE_PORT_STATUS 0x64
#define MOUSE_PORT_CMD    0x64

static int32_t mouse_x = 400, mouse_y = 300;
static int mouse_left = 0, mouse_right = 0;
static int64_t screen_w = 1024, screen_h = 768;

static uint8_t packet[3];
static int packet_idx = 0;

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(MOUSE_PORT_STATUS) & 0x02)) return;
    }
}

static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(MOUSE_PORT_STATUS) & 0x01) return;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0xD4); // "следующий байт — для мыши"
    mouse_wait_write();
    outb(MOUSE_PORT_DATA, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(MOUSE_PORT_DATA);
}

static void mouse_flush_output_buffer(void) {
    for (int i = 0; i < 64; i++) {
        if (!(inb(MOUSE_PORT_STATUS) & 0x01)) return; // буфер пуст
        inb(MOUSE_PORT_DATA); // вычитываем и выбрасываем "залежавшийся" байт
    }
}

void mouse_set_screen_bounds(int64_t w, int64_t h) {
    screen_w = w; screen_h = h;
}

/* Полная, "по стандарту" инициализация 8042-контроллера — раньше мы
 * настраивали только мышиный порт и полагались на то, что клавиатурный
 * останется как есть. На реальном железе (в отличие от QEMU) контроллер
 * реагирует строже: если в буфере залип старый байт, или явно не
 * прописать оба порта (клавиатура+мышь) в config byte, легко словить
 * ситуацию "первая команда с клавиатуры прошла, дальше тишина" — то,
 * что было в баг-репорте вместе с нерабочей мышью. Теперь: гасим оба
 * порта, чистим буфер, явно прописываем config byte (оба IRQ включены,
 * оба clock включены), и только потом поднимаем порты обратно. */
void mouse_init(void) {
    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0xAD); // выключить порт клавиатуры на время настройки
    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0xA7); // выключить порт мыши на время настройки

    mouse_flush_output_buffer();

    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0x20); // прочитать configuration byte
    mouse_wait_read();
    uint8_t status = inb(MOUSE_PORT_DATA);
    status |= 0x01;   // bit0: разрешить IRQ1 (клавиатура) — явно, не полагаясь на "и так стояло"
    status |= 0x02;   // bit1: разрешить IRQ12 (мышь)
    status &= ~0x10;  // bit4: 0 = clock клавиатуры включён (1 значило бы "выключен")
    status &= ~0x20;  // bit5: 0 = clock мыши включён

    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0x60); // записать configuration byte
    mouse_wait_write();
    outb(MOUSE_PORT_DATA, status);

    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0xAE); // включить порт клавиатуры обратно
    mouse_wait_write();
    outb(MOUSE_PORT_CMD, 0xA8); // включить порт мыши обратно

    mouse_write(0xF6); mouse_read(); // сбросить настройки мыши по умолчанию
    mouse_write(0xF4); mouse_read(); // включить генерацию пакетов

}

/* вызывается из обработчика IRQ12 (вектор 32+12=44) */
void mouse_irq_handler(void) {
    uint8_t data = inb(MOUSE_PORT_DATA);

    /* Первый байт валидного пакета ВСЕГДА имеет бит 3 = 1 (это часть
     * протокола PS/2). Если мы читаем "первый" байт без этого бита —
     * значит синхронизация уехала (пропустили байт где-то раньше),
     * выкидываем его и ждём настоящего начала пакета. Это и было
     * причиной "мышь едет не туда" — без этой проверки X/Y смещения
     * читались из неправильных байт-позиций пакета. */
    if (packet_idx == 0 && !(data & 0x08)) {
        return;
    }

    packet[packet_idx++] = data;
    if (packet_idx < 3) return;
    packet_idx = 0;

    mouse_left  = packet[0] & 0x01;
    mouse_right = packet[0] & 0x02;

    /* если бит переполнения выставлен — данные о смещении недостоверны,
     * лучше пропустить кадр, чем дёрнуть курсор в случайную сторону */
    if (packet[0] & 0xC0) return;

    int8_t dx = (int8_t)packet[1];
    int8_t dy = (int8_t)packet[2];

    mouse_x += dx;
    mouse_y -= dy; // ось Y у PS/2 инвертирована относительно экрана

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen_w) mouse_x = screen_w - 1;
    if (mouse_y >= screen_h) mouse_y = screen_h - 1;
}

int32_t mouse_get_x(void) { return mouse_x; }
int32_t mouse_get_y(void) { return mouse_y; }
int mouse_left_pressed(void) { return mouse_left; }
int mouse_right_pressed(void) { return mouse_right; }

/* Общий интерфейс для ЛЮБОГО источника мыши (PS/2 или USB) — тот же
 * clamping по границам экрана, что и в mouse_irq_handler. USB HID отдаёт
 * dx/dy сразу в "нормальном" (не инвертированном) виде — переворот оси Y
 * (если нужен) делает вызывающий код (usb_mouse.c), не эта функция. */
void mouse_set_position_delta(int32_t dx, int32_t dy) {
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen_w) mouse_x = screen_w - 1;
    if (mouse_y >= screen_h) mouse_y = screen_h - 1;
}

void mouse_set_buttons(int left, int right) {
    mouse_left = left ? 1 : 0;
    mouse_right = right ? 1 : 0;
}
