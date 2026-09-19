#include <bnux/types.h>
#include <bnux/io.h>

extern void kprintf(const char *fmt, ...);

struct PACKED idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
};

struct PACKED idt_ptr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256] ALIGN(16);
static struct idt_ptr   idtp;

extern void *isr_stub_table[];
extern void idt_load(uint64_t idtp_addr);

static void idt_set_gate(int n, void *handler, uint8_t ist, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_lo  = addr & 0xFFFF;
    idt[n].selector   = 0x08; // kernel code segment
    idt[n].ist        = ist;
    idt[n].type_attr  = type_attr;
    idt[n].offset_mid = (addr >> 16) & 0xFFFF;
    idt[n].offset_hi  = (addr >> 32) & 0xFFFFFFFF;
    idt[n].reserved   = 0;
}

static const char *exception_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS",
    "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved", "x87 FP exception", "Alignment check",
    "Machine check", "SIMD FP exception", "Virtualization exception",
    "Control protection", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor injection", "VMM communication",
    "Security exception", "Reserved"
};

extern void keyboard_irq_handler(void);
extern void mouse_irq_handler(void);

/* Структура контекста прерывания — должна совпадать с isr_stubs.S */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
};

/* Обработчик syscall (int 0x80) */
static void syscall_dispatch(struct interrupt_frame *frame) {
    uint64_t syscall_num = frame->rax;
    int64_t result = 0;
    
    extern void console_putchar(char c);
    extern void kprintf(const char *fmt, ...);
    
    /* sys_write: аргументы в rdi(fd), rsi(buf), rdx(count) */
    if (syscall_num == 4) {
        const char *buf = (const char*)frame->rsi;
        size_t count = (size_t)frame->rdx;
        for (size_t i = 0; i < count; i++) {
            console_putchar(buf[i]);
        }
        result = (int64_t)count;
    }
    /* sys_exit: аргумент в rdi(status) */
    else if (syscall_num == 1) {
        kprintf("\n[syscall] exit status=%d\n", (int)frame->rdi);
        for (;;) __asm__ volatile ("cli; hlt");
    }
    /* sys_read: заглушка */
    else if (syscall_num == 3) {
        result = 0;
    }
    else {
        kprintf("\n[syscall] неизвестный номер=%d\n", (int)syscall_num);
        result = -1;
    }
    
    frame->rax = result;
}

/* Общая точка входа из ассемблерных заглушек: rdi = vector, rsi = error_code */
void isr_common_handler(uint64_t vector, uint64_t error_code, struct interrupt_frame *frame) {
    if (vector == 128) {
        /* Syscall через int 0x80 */
        syscall_dispatch(frame);
        return;
    }
    
    if (vector < 32) {
        kprintf("\n[PANIC] Exception %d: %s (err=0x%x)\n", (int)vector,
                exception_names[vector], error_code);
        for (;;) { __asm__ volatile ("cli; hlt"); }
    } else {
        if (vector == 33) keyboard_irq_handler(); // IRQ1 = клавиатура
        if (vector == 44) mouse_irq_handler();    // IRQ12 = мышь (PS/2 aux)
        if (vector >= 40) outb(0xA0, 0x20); // slave PIC EOI
        outb(0x20, 0x20);                    // master PIC EOI
    }
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, isr_stub_table[i], 0, 0x8E); // present, ring0, interrupt gate
    }
    
    /* Устанавливаем обработчик для int 0x80 (syscall) — вектор 128 */
    idt_set_gate(128, isr_stub_table[128], 0, 0xEE); // present, ring3, trap gate
    /* 0xEE = trap gate (0x8F) + DPL=3 (ring3 доступ) => 0x8F | 0x60 = 0xEF? 
     * На самом деле: тип=0xE (trap), P=1, DPL=3 => 0b11101110 = 0xEE */

    idt_load((uint64_t)&idtp);
}

/* Ремап PIC на векторы 32-47, чтобы IRQ не конфликтовали с CPU-исключениями */
void pic_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x0);  outb(0xA1, 0x0);
}
