/* GRUB грузит нас в 32-битном protected mode, без paging и без long mode.
 * Limine делал этот переход за нас — теперь делаем сами. Это самый
 * рискованный кусок кода во всём проекте: вручную строим page tables,
 * включаем PAE, long mode (EFER.LME), paging, и прыгаем в 64-битный код.
 * Написано по стандартной "higher half x86_64 multiboot2" схеме,
 * НЕ ПРОВЕРЕНО живьём (в песочнице нет способа это запустить) —
 * ожидай, что придётся дебажить по фактическому поведению на железе. */

.set MB2_MAGIC,        0xE85250D6
.set MB2_ARCH_I386,    0
.set KERNEL_VMA_HI,    0xffffffff  /* верхние 32 бита KERNEL_VMA, для movabs */

.section .multiboot, "a"
.align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH_I386
    .long mb2_header_end - mb2_header_start
    .long -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header_start))

    /* запрашиваем framebuffer у GRUB, если получится — 1024x768x32 */
    .align 8
    .word 5
    .word 0
    .long 20
    .long 1024
    .long 768
    .long 32
    .long 0 /* padding до кратности 8 */

    .align 8
    .word 0
    .word 0
    .long 8
mb2_header_end:

/* --- Данные загрузочного трамплина: живут по НИЗКИМ физическим адресам,
 * доступны напрямую (без paging) в 32-битном режиме --- */
.section .boot.bss, "aw", @nobits
.align 4096
boot_pml4:      .skip 4096
boot_pdpt_low:  .skip 4096
boot_pdpt_high: .skip 4096
boot_pd_low:    .skip 4096
boot_pd_high:   .skip 4096
boot_stack_bottom: .skip 16384
boot_stack_top:

.section .boot.data, "aw"
.align 8
mb_magic_save: .long 0
mb_info_save:  .long 0

.align 16
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF   /* 64-бит kernel code */
    .quad 0x00AF92000000FFFF   /* 64-бит kernel data */
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .boot.text, "ax"
.code32
.global _start
.extern kmain_mb2

_start:
    cli
    mov $boot_stack_top, %esp

    mov %eax, mb_magic_save
    mov %ebx, mb_info_save

    /* --- строим page tables: identity map + higher-half map, оба
     * указывают на одни и те же физические 0..1GB (одна PD таблица,
     * 512 записей по 2MB = ровно 1GB). Ограничение "только 1GB памяти
     * доступно PMM" на GRUB-пути — сознательное упрощение первой
     * версии, см. README. */

    mov $boot_pdpt_low, %eax
    or $0x03, %eax
    mov %eax, boot_pml4

    mov $boot_pdpt_high, %eax
    or $0x03, %eax
    mov %eax, boot_pml4 + 511*8

    mov $boot_pd_low, %eax
    or $0x03, %eax
    mov %eax, boot_pdpt_low

    mov $boot_pd_high, %eax
    or $0x03, %eax
    mov %eax, boot_pdpt_high + 510*8

    xor %ecx, %ecx
fill_pd:
    mov $0x200000, %eax
    mul %ecx
    or $0x83, %eax              /* present+writable+PS(2MB huge page) */
    mov %eax, boot_pd_low(,%ecx,8)
    mov %eax, boot_pd_high(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jl fill_pd

    mov $boot_pml4, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $0x20, %eax               /* CR4.PAE */
    mov %eax, %cr4

    mov $0xC0000080, %ecx        /* EFER MSR */
    rdmsr
    or $0x100, %eax              /* EFER.LME */
    wrmsr

    mov %cr0, %eax
    or $0x80000000, %eax         /* CR0.PG */
    mov %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* стек остаётся по низкому (identity-mapped) адресу — рабочий,
     * т.к. identity-мапа никуда не делась после перехода в long mode */

    xor %edi, %edi
    mov mb_info_save, %edi      /* rdi = указатель на multiboot2 info (32-бит, zero-extend) */

    movabs $kmain_mb2, %rax
    call *%rax

hang:
    cli
    hlt
    jmp hang
