#include <bnux/types.h>

extern void kprintf(const char *fmt, ...);

#define PAGE_SIZE 4096

/* Минимальные структуры, совместимые с limine.h memmap response,
 * чтобы не тащить сюда весь заголовок протокола. */
struct mm_region {
    uint64_t base;
    uint64_t length;
    uint64_t type; // 0 = usable
};

static uint8_t *bitmap;
static uint64_t bitmap_pages;      // сколько страниц описывает битмап
static uint64_t highest_addr;
static uint64_t last_free_index;
static int pmm_initialized = 0;

static inline void bit_set(uint64_t i)   { bitmap[i / 8] |=  (1 << (i % 8)); }
static inline void bit_clear(uint64_t i) { bitmap[i / 8] &= ~(1 << (i % 8)); }
static inline int  bit_test(uint64_t i)  { return bitmap[i / 8] & (1 << (i % 8)); }

/* regions уже отсортированы по base (так отдаёт Limine).
 * bitmap_storage — заранее выбранный usable-регион, куда положим сам битмап.
 * Идемпотентно: повторный вызов (например с GRUB-пути, где PMM может
 * понадобиться раньше остальной инициализации — см. kernel_mb2.c) просто
 * ничего не делает, чтобы не затереть уже сделанные аллокации. */
void pmm_init(struct mm_region *regions, size_t count, uint64_t hhdm_offset) {
    if (pmm_initialized) return;
    pmm_initialized = 1;

    highest_addr = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t end = regions[i].base + regions[i].length;
        if (regions[i].type == 0 && end > highest_addr) highest_addr = end;
    }

    bitmap_pages = highest_addr / PAGE_SIZE;
    uint64_t bitmap_size = (bitmap_pages + 7) / 8;

    /* найти первый usable регион, куда влезает битмап */
    for (size_t i = 0; i < count; i++) {
        if (regions[i].type == 0 && regions[i].length >= bitmap_size) {
            bitmap = (uint8_t*)(regions[i].base + hhdm_offset);
            for (uint64_t b = 0; b < bitmap_size; b++) bitmap[b] = 0xFF; // всё занято по умолчанию
            regions[i].base   += bitmap_size;
            regions[i].length -= bitmap_size;
            break;
        }
    }

    /* пометить usable-регионы как свободные */
    for (size_t i = 0; i < count; i++) {
        if (regions[i].type != 0) continue;
        uint64_t start_page = regions[i].base / PAGE_SIZE;
        uint64_t npages = regions[i].length / PAGE_SIZE;
        for (uint64_t p = 0; p < npages; p++) bit_clear(start_page + p);
    }

    last_free_index = 0;
    kprintf("[pmm] managing %d MB, bitmap=%d bytes\n",
            (int)(highest_addr / (1024*1024)), (int)bitmap_size);
}

void *pmm_alloc_page(void) {
    for (uint64_t i = last_free_index; i < bitmap_pages; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            last_free_index = i + 1;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL; // OOM
}

void pmm_free_page(void *addr) {
    uint64_t i = (uint64_t)addr / PAGE_SIZE;
    bit_clear(i);
    if (i < last_free_index) last_free_index = i;
}

void pmm_get_stats(uint64_t *total_pages, uint64_t *free_pages) {
    *total_pages = bitmap_pages;
    uint64_t free_count = 0;
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        if (!bit_test(i)) free_count++;
    }
    *free_pages = free_count;
}
