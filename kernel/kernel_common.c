#include <bnux/types.h>

extern void serial_init(void);
extern void kprintf(const char *fmt, ...);
extern void gdt_init(uint64_t kernel_stack_top);
extern void idt_init(void);
extern void pic_remap(void);

struct mm_region { uint64_t base; uint64_t length; uint64_t type; };
extern void pmm_init(struct mm_region *regions, size_t count, uint64_t hhdm_offset);

extern void kmalloc_init(uint64_t hhdm_offset);
extern void vmm_init(uint64_t hhdm_offset);
extern void console_init(void *fb, uint64_t w, uint64_t h, uint64_t pitch);
extern void initrd_install(void);
extern void task_init(uint64_t hhdm_offset);
extern int  task_create(const char *name, void (*entry)(void));
extern int  task_create_user(const char *name, const uint8_t *code, size_t code_size);
extern void task_run_user(int slot);
extern void task_yield(void);
extern void shell_run(void);
extern void shell_register_fb(void *fb, uint64_t w, uint64_t h, uint64_t pitch);
extern void print_lix_logo(void);
extern int  ata_probe(void);
extern int  fat32_init(void);
extern void mouse_init(void);
extern void pci_scan_and_report(void);

static char boot_stack[16384] ALIGN(16);

static void hcf(void) {
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* "Бедный" debug без serial-порта — закрашивает весь экран одним цветом
 * на каждом этапе загрузки. Если система падает в reboot loop, по
 * ПОСЛЕДНЕМУ увиденному цвету можно понять, на каком шаге. */
static void debug_checkpoint(void *fb, uint64_t w, uint64_t h, uint64_t pitch, uint32_t color) {
    if (!fb) return;
    uint32_t *pixels = (uint32_t*)fb;
    for (uint64_t y = 0; y < h; y++)
        for (uint64_t x = 0; x < w; x++)
            pixels[y * (pitch / 4) + x] = color;
}
#define DBG_RED    0x00FF0000
#define DBG_ORANGE 0x00FF8800
#define DBG_YELLOW 0x00FFFF00
#define DBG_GREEN  0x0000FF00
#define DBG_CYAN   0x0000FFFF
#define DBG_BLUE   0x000000FF
#define DBG_PURPLE 0x008800FF
#define DBG_WHITE  0x00FFFFFF

static volatile uint64_t bg_counter = 0;
static void bg_task(void) {
    for (;;) {
        bg_counter++;
        task_yield();
    }
}

/* fb может быть NULL (нет видео) — всё остальное всё равно инициализируется,
 * просто не будет ни консоли, ни шелла. hhdm_offset — на Limine-пути это
 * настоящий HHDM от бутлоадера, на GRUB-пути — наша собственная
 * identity-style карта (см. multiboot2.S), для остального кода разницы нет. */
void kernel_common_init(void *fb, uint64_t fb_w, uint64_t fb_h, uint64_t fb_pitch,
                         struct mm_region *regions, uint64_t region_count,
                         uint64_t hhdm_offset) {
    kprintf("=== BNUX kernel booting (Leversoft) ===\n");

    if (fb) {
        kprintf("[boot] framebuffer %dx%d @ %p\n", (int)fb_w, (int)fb_h, (uint64_t)fb);
    } else {
        kprintf("[boot] нет фреймбуфера, консоль работать не будет!\n");
    }
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_RED);

    gdt_init((uint64_t)&boot_stack[sizeof(boot_stack)]);
    kprintf("[boot] GDT+TSS загружены\n");
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_ORANGE);

    idt_init();
    pic_remap();
    kprintf("[boot] IDT загружен, PIC переопределён\n");
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_YELLOW);

    pmm_init(regions, region_count, hhdm_offset);
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_GREEN);

    kmalloc_init(hhdm_offset);
    vmm_init(hhdm_offset);
    task_init(hhdm_offset);
    kprintf("[boot] kmalloc, VMM и планировщик задач готовы (hhdm=%p)\n", hhdm_offset);
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_CYAN);

    initrd_install();
    kprintf("[boot] BNUXFS ramdisk смонтирован\n");
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_BLUE);

    mouse_init();
    kprintf("[boot] PS/2 мышь инициализирована\n");
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_PURPLE);

    if (ata_probe()) {
        kprintf("[boot] ATA-диск найден\n");
        if (fat32_init() == 0) kprintf("[boot] FAT32 диск смонтирован\n");
    } else {
        kprintf("[boot] ATA-диск не найден\n");
    }
    pci_scan_and_report();
    debug_checkpoint(fb, fb_w, fb_h, fb_pitch, DBG_WHITE);

    __asm__ volatile ("sti");
    kprintf("[boot] прерывания включены.\n");

    if (fb) {
        console_init(fb, fb_w, fb_h, fb_pitch);
        shell_register_fb(fb, fb_w, fb_h, fb_pitch);
        task_create("bg_counter", bg_task);
        shell_run(); // не возвращается
    }

    hcf();
}
