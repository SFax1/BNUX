#include <bnux/io.h>
#include <bnux/types.h>

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);    // отключить прерывания
    outb(COM1 + 3, 0x80);    // включить DLAB (baud rate divisor)
    outb(COM1 + 0, 0x03);    // divisor lo byte -> 38400 baud
    outb(COM1 + 1, 0x00);    // divisor hi byte
    outb(COM1 + 3, 0x03);    // 8 бит, без чётности, 1 стоп-бит
    outb(COM1 + 2, 0xC7);    // включить FIFO
    outb(COM1 + 4, 0x0B);    // IRQ enable, RTS/DSR set
}

static int serial_tx_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    while (!serial_tx_empty());
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

/* Мини-printf: поддерживает %s %d %x %u %c %% — этого хватит для kernel-логов */
static void print_uint(uint64_t v, int base, int upper) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v) { buf[i++] = digits[v % base]; v /= base; }
    while (i--) serial_putc(buf[i]);
}

void kprintf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { serial_putc(*p); continue; }
        p++;
        switch (*p) {
            case 's': serial_puts(__builtin_va_arg(args, const char*)); break;
            case 'd': {
                int64_t v = __builtin_va_arg(args, int);
                if (v < 0) { serial_putc('-'); v = -v; }
                print_uint((uint64_t)v, 10, 0);
                break;
            }
            case 'u': print_uint(__builtin_va_arg(args, uint32_t), 10, 0); break;
            case 'x': print_uint(__builtin_va_arg(args, uint64_t), 16, 0); break;
            case 'p': serial_puts("0x"); print_uint(__builtin_va_arg(args, uint64_t), 16, 0); break;
            case 'c': serial_putc((char)__builtin_va_arg(args, int)); break;
            case '%': serial_putc('%'); break;
            default: serial_putc('%'); serial_putc(*p);
        }
    }
    __builtin_va_end(args);
}
