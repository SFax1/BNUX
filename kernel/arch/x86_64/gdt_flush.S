.section .text
.global gdt_flush
.global tss_flush

# void gdt_flush(uint64_t gdtp_addr) -- rdi = адрес gdt_ptr
gdt_flush:
    lgdt (%rdi)
    mov $0x10, %ax      # селектор kernel data (индекс 2 * 8)
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    pop %rdi             # адрес возврата
    mov $0x08, %rax       # селектор kernel code (индекс 1 * 8)
    push %rax
    push %rdi
    lretq                 # far return -> перезагружает CS

tss_flush:
    mov $0x2B, %ax        # селектор TSS (индекс 5 * 8) | RPL0
    ltr %ax
    ret
