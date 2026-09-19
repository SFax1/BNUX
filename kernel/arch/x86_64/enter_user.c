#include <bnux/types.h>
#include <bnux/syscall.h>

extern void kprintf(const char *fmt, ...);
extern void enter_user_mode_asm(struct iretq_frame *frame);

/* 
 * enter_user_mode — переключение из ring0 в ring3 через iretq
 * entry_point: виртуальный адрес точки входа user-программы
 * user_stack: виртуальный адрес вершины стека user-программы
 */
void enter_user_mode(uint64_t entry_point, uint64_t user_stack) {
    struct iretq_frame frame;
    
    frame.rip     = entry_point;
    frame.cs      = GDT_USER_CODE;          /* 0x1B — user code segment */
    frame.rflags  = EFLAGS_IF | 0x2;        /* Interrupt Flag + обязательный бит 2 */
    frame.rsp     = user_stack;
    frame.ss      = GDT_USER_DATA;          /* 0x23 — user data segment */
    
    kprintf("[syscall] переход в ring3: rip=0x%x rsp=0x%x\n", entry_point, user_stack);
    
    enter_user_mode_asm(&frame);
    
    /* Сюда не попадаем после iretq */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
