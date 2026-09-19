.section .text
.global enter_user_mode_asm

/* 
 * void enter_user_mode_asm(struct iretq_frame *frame)
 * Аргумент в rdi: указатель на структуру iretq_frame
 * Структура: rip, cs, rflags, rsp, ss
 * 
 * iretq ожидает на стеке (сверху вниз): SS, RSP, RFLAGS, CS, RIP
 */
enter_user_mode_asm:
    /* Сохраняем frame pointer */
    push %rbp
    mov %rsp, %rbp
    
    /* Загружаем значения из структуры */
    mov 16(%rdi), %rax          /* rflags */
    mov 8(%rdi), %rcx           /* cs */
    mov (%rdi), %rdx            /* rip */
    mov 32(%rdi), %r8           /* rsp (user) */
    mov 40(%rdi), %r9           /* ss (user) */
    
    /* Строим стек для iretq: SS, RSP, RFLAGS, CS, RIP */
    push %r9                    /* SS */
    push %r8                    /* RSP */
    push %rax                   /* RFLAGS */
    push %rcx                   /* CS */
    push %rdx                   /* RIP */
    
    iretq
    
    pop %rbp
    ret
