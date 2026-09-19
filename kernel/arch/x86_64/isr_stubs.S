.section .text
.extern isr_common_handler
.global idt_load
.global isr_stub_table

idt_load:
    lidt (%rdi)
    ret

# Векторы с CPU-исключением, которое само кладёт error_code в стек
.macro ISR_NOERR num
.global isr_stub_\num
isr_stub_\num:
    pushq $0            # фиктивный error_code для единообразия
    pushq $\num
    jmp isr_common_stub
.endm

.macro ISR_ERR num
.global isr_stub_\num
isr_stub_\num:
    pushq $\num
    jmp isr_common_stub
.endm

isr_common_stub:
    # На стеке: [vector][error_code] (сверху вниз после push'ей)
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, %rdx          # передаём указатель на interrupt_frame
    mov 120(%rsp), %rdi     # vector
    mov 128(%rsp), %rsi     # error_code
    call isr_common_handler

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp   # убрать vector + error_code
    iretq

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

.altmacro
.set i, 32
.rept 224
    ISR_NOERR %i
    .set i, i+1
.endr

.macro EMIT_QUAD num
    .quad isr_stub_\num
.endm

.section .data
.align 8
isr_stub_table:
.set i, 0
.rept 256
    EMIT_QUAD %i
    .set i, i+1
.endr
