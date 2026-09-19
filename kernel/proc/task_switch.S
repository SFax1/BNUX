.section .text
.global task_switch

# void task_switch(uint64_t *old_rsp, uint64_t new_rsp)
# rdi = куда сохранить текущий rsp, rsi = rsp задачи на которую переключаемся
task_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, (%rdi)   # сохранить текущий rsp в *old_rsp
    mov %rsi, %rsp      # переключиться на стек новой задачи

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp

    ret                  # вернётся по адресу, лежащему на новом стеке
                          # (либо в точку вызова task_switch у старой задачи,
                          #  либо в task_trampoline для новой задачи)
