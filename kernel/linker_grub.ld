OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;
KERNEL_LMA = 0x100000; /* 1MB — стандартный адрес загрузки для multiboot */

PHDRS
{
    boot    PT_LOAD FLAGS((1 << 2) | (1 << 1) | (1 << 0));
    text    PT_LOAD FLAGS((1 << 2) | (1 << 0));
    rodata  PT_LOAD FLAGS((1 << 2));
    data    PT_LOAD FLAGS((1 << 2) | (1 << 1));
}

SECTIONS
{
    . = KERNEL_LMA;

    /* Boot-трамплин (32-битный код, page tables, GDT64) — линкуется по
     * НИЗКИМ физическим адресам, т.к. выполняется ДО включения paging,
     * когда высокие виртуальные адреса ещё не существуют. */
    .multiboot ALIGN(8) : { *(.multiboot) } :boot
    .boot.text ALIGN(16) : { *(.boot.text) } :boot
    .boot.data ALIGN(16) : { *(.boot.data) } :boot
    .boot.bss  ALIGN(16) : { *(.boot.bss) } :boot

    . += KERNEL_VMA;

    /* Основное ядро — линкуется по высоким (higher-half) виртуальным
     * адресам, но физически (AT) лежит там же, в первом мегабайте+.
     * После того как boot-трамплин включит paging с нужными таблицами,
     * эти виртуальные адреса становятся доступны. */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA) { *(.text .text.*) } :text
    . += CONSTANT(MAXPAGESIZE);
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA) { *(.rodata .rodata.*) } :rodata
    . += CONSTANT(MAXPAGESIZE);
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA) { *(.data .data.*) } :data
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA) { *(COMMON) *(.bss .bss.*) } :data

    /DISCARD/ : { *(.eh_frame) *(.note .note.*) *(.comment) }
}
