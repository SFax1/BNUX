#ifndef BNUX_SYSCALL_H
#define BNUX_SYSCALL_H

#include <bnux/types.h>

/* Номера системных вызовов */
#define SYS_EXIT    1
#define SYS_WRITE   4
#define SYS_READ    3

/* Сегменты GDT для ring3 */
#define GDT_USER_CODE 0x1B  /* селектор user code */
#define GDT_USER_DATA 0x23  /* селектор user data */

/* Флаги для RFLAGS при переходе в ring3 */
#define EFLAGS_IF     (1 << 9)  /* Interrupt Flag */

/* Структура стека для iretq при переходе в ring3 */
struct PACKED iretq_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Функция перехода в user mode */
void enter_user_mode(uint64_t entry_point, uint64_t user_stack);

#endif
