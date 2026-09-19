#include <bnux/types.h>

struct PACKED gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_hi;
};

struct PACKED gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

/* TSS для x86_64: нужен для смены стека при прерываниях/переключении колец */
struct PACKED tss_entry {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static struct gdt_entry gdt[7] ALIGN(16);
static struct gdt_ptr   gdtp;
static struct tss_entry tss ALIGN(16);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_lo     = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_hi     = (base >> 24) & 0xFF;
    gdt[i].limit_lo    = limit & 0xFFFF;
    gdt[i].granularity = (gran & 0xF0) | ((limit >> 16) & 0x0F);
    gdt[i].access      = access;
}

extern void gdt_flush(uint64_t gdtp_addr);
extern void tss_flush(void);

void gdt_init(uint64_t kernel_stack_top) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint64_t)&gdt;

    gdt_set(0, 0, 0, 0, 0);                // null
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xA0);    // kernel code, 64-bit
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xA0);    // kernel data
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xA0);    // user code
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xA0);    // user data

    for (int i = 0; i < (int)sizeof(tss); i++) ((uint8_t*)&tss)[i] = 0;
    tss.rsp0 = kernel_stack_top;
    tss.iomap_base = sizeof(tss);

    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;

    /* TSS descriptor занимает 2 слота в 64-битном GDT */
    struct PACKED tss_desc { uint16_t limit_lo; uint16_t base_lo; uint8_t base_mid;
        uint8_t access; uint8_t gran; uint8_t base_hi; uint32_t base_upper; uint32_t reserved; };
    struct tss_desc *td = (struct tss_desc*)&gdt[5];
    td->limit_lo   = tss_limit & 0xFFFF;
    td->base_lo    = tss_base & 0xFFFF;
    td->base_mid   = (tss_base >> 16) & 0xFF;
    td->access     = 0x89; // present, 64-bit TSS available
    td->gran       = (tss_limit >> 16) & 0x0F;
    td->base_hi    = (tss_base >> 24) & 0xFF;
    td->base_upper = (tss_base >> 32) & 0xFFFFFFFF;
    td->reserved   = 0;

    gdt_flush((uint64_t)&gdtp);
    tss_flush();
}
