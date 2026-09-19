/* 
 * Простая user-программа для ring3
 * Выводит "Hello from ring3!" через syscall int 0x80
 * 
 * Компилируется отдельно и загружается ядром как бинарный blob.
 */

.section .text
.global _start
.global user_program_end

_start:
    /* sys_write(fd=1, buf=message, count=length) */
    mov $4, %rax              /* номер syscall: SYS_WRITE */
    mov $1, %rdi              /* fd = 1 (stdout) */
    lea message(%rip), %rsi   /* указатель на строку */
    mov $message_len, %rdx    /* длина строки */
    int $0x80                 /* вызов syscall */
    
    /* sys_exit(status=0) */
    mov $1, %rax              /* номер syscall: SYS_EXIT */
    mov $0, %rdi              /* status = 0 */
    int $0x80                 /* вызов syscall */
    
    /* Если somehow вернулись — зацикливаемся */
hang:
    cli
    hlt
    jmp hang

.section .rodata
message:
    .ascii "Hello from ring3!\n"
message_len = . - message

user_program_end:
