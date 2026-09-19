#include <bnux/types.h>
#include <bnux/io.h>

#define KBD_DATA_PORT 0x60
#define KBD_BUF_SIZE  256

static char kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0, kbd_tail = 0;
static int shift_held = 0;

/* scancode set 1, только нижний регистр набора + верхний по Shift.
 * 0 = нет соответствия / не печатаемая клавиша. */
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, /* left ctrl */
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, /* left shift */
    '\\','z','x','c','v','b','n','m',',','.','/',
    0, /* right shift */
    '*',
    0, /* alt */
    ' ',
    0 /* caps lock и дальше — не обрабатываем */
};

static const char scancode_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,
    '|','Z','X','C','V','B','N','M','<','>','?',
    0,
    '*', 0, ' ', 0
};

#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_RELEASE_MASK 0x80

static void kbd_push(char c) {
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) return; // буфер полон — теряем символ
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

/* вызывается из общего обработчика прерываний на векторе IRQ1 (32+1=33) */
void keyboard_irq_handler(void) {
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc == SCANCODE_LSHIFT || sc == SCANCODE_RSHIFT) { shift_held = 1; return; }
    if (sc == (SCANCODE_LSHIFT | SCANCODE_RELEASE_MASK) ||
        sc == (SCANCODE_RSHIFT | SCANCODE_RELEASE_MASK)) { shift_held = 0; return; }

    if (sc & SCANCODE_RELEASE_MASK) return; // отпускание прочих клавиш — игнор

    char c = shift_held ? scancode_ascii_shift[sc & 0x7F] : scancode_ascii[sc & 0x7F];
    if (c) kbd_push(c);
}

int kbd_has_char(void) {
    return kbd_head != kbd_tail;
}

/* блокирующее чтение: крутимся в hlt пока irq не положит символ в буфер */
char kbd_getchar(void) {
    while (kbd_head == kbd_tail) {
        __asm__ volatile ("sti; hlt");
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}
