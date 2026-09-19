#include <bnux/types.h>

extern void serial_init(void);
extern void print_lix_logo(void);
extern void kprintf(const char *fmt, ...);

struct mm_region { uint64_t base; uint64_t length; uint64_t type; };
extern void kernel_common_init(void *fb, uint64_t fb_w, uint64_t fb_h, uint64_t fb_pitch,
                                struct mm_region *regions, uint64_t region_count,
                                uint64_t hhdm_offset);

struct mb2_mm_region { uint64_t base; uint64_t length; uint64_t type; };
struct mb2_fb_info {
    uint64_t addr;
    uint32_t width, height, pitch;
    uint8_t bpp;
    int valid;
};
extern void mb2_parse(uint32_t mb_addr, struct mb2_mm_region *out_regions, int *out_count,
                       struct mb2_fb_info *out_fb);

extern int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
extern void vmm_init(uint64_t hhdm_offset);
extern void pmm_init(struct mm_region *regions, size_t count, uint64_t hhdm_offset);

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)

/* KERNEL_VMA — то же значение, что и в multiboot2.S/linker_grub.ld.
 * Наши статические page tables из boot-трамплина уже отображают физическую
 * [0, 1GB) на виртуальную [KERNEL_VMA, KERNEL_VMA+1GB) — используем это
 * же как "псевдо-HHDM" для остального кода ядра (VMM, PMM). */
#define KERNEL_VMA 0xffffffff80000000ULL

static struct mm_region region_buf[64];

/* Виртуальный адрес, куда явно замаппим реальный framebuffer — GRUB отдаёт
 * его физический адрес, который почти наверняка ВНЕ наших статических
 * 1GB, поэтому его нужно замапить отдельно уже после того как VMM готов. */
#define FB_VIRT_BASE 0xffffffffE0000000ULL

void kmain_mb2(uint32_t mb_addr) {
    serial_init();
    print_lix_logo();
    kprintf("[boot] GRUB/multiboot2 путь загрузки\n");

    struct mb2_mm_region mb_regions[64];
    int mb_count = 0;
    struct mb2_fb_info fb_info;
    mb2_parse(mb_addr, mb_regions, &mb_count, &fb_info);

    if (mb_count > 64) mb_count = 64;
    for (int i = 0; i < mb_count; i++) {
        region_buf[i].base = mb_regions[i].base;
        region_buf[i].length = mb_regions[i].length;
        region_buf[i].type = mb_regions[i].type;
    }
    kprintf("[boot] multiboot2: %d регионов памяти (усечено до 1GB — см. README)\n", mb_count);

    /* PMM+VMM нужны заранее, чтобы замапить framebuffer до общей инициализации.
     * Обе функции идемпотентны — kernel_common_init вызовет их ещё раз,
     * это просто ничего не сделает благодаря внутренним guard'ам. */
    pmm_init(region_buf, (uint64_t)mb_count, KERNEL_VMA);
    vmm_init(KERNEL_VMA);

    void *fb_virt = NULL;
    uint64_t fb_w = 0, fb_h = 0, fb_pitch = 0;

    if (fb_info.valid) {
        kprintf("[boot] framebuffer от GRUB: %dx%d @ phys %p\n",
                (int)fb_info.width, (int)fb_info.height, fb_info.addr);

        /* framebuffer нужно замапить ДО вызова kernel_common_init, чтобы
         * debug_checkpoint внутри неё уже мог рисовать. PMM/VMM уже
         * проинициализированы вызовами чуть выше. */
        uint64_t bytes = (uint64_t)fb_info.pitch * fb_info.height;
        uint64_t pages = (bytes + 4095) / 4096;
        for (uint64_t p = 0; p < pages; p++) {
            vmm_map_page(FB_VIRT_BASE + p * 4096, fb_info.addr + p * 4096,
                         PAGE_PRESENT | PAGE_WRITABLE);
        }
        fb_virt = (void*)FB_VIRT_BASE;
        fb_w = fb_info.width; fb_h = fb_info.height; fb_pitch = fb_info.pitch;
    } else {
        kprintf("[boot] GRUB не дал framebuffer — консоли не будет\n");
    }

    kernel_common_init(fb_virt, fb_w, fb_h, fb_pitch, region_buf, (uint64_t)mb_count, KERNEL_VMA);
}
