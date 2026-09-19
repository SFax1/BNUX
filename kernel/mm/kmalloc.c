#include <bnux/types.h>

extern void *pmm_alloc_page(void);
extern void  kprintf(const char *fmt, ...);
extern int   vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

#define PAGE_SIZE 4096
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)

/* Отдельный виртуальный диапазон под кучу ядра. На GRUB-пути наши
 * статические page tables (multiboot2.S) занимают KERNEL_VMA..+1GB
 * (0xffffffff80000000..0xffffffffC0000000) под identity-style отображение
 * первого гигабайта физической памяти — куча должна быть СТРОГО выше
 * этого диапазона, иначе vmm_map_page() наткнётся на уже занятые 2MB
 * huge-page записи и всё сломает. */
#define HEAP_VIRT_BASE 0xffffffffD0000000ULL
#define HEAP_VIRT_MAX  0xfffffffff0000000ULL

struct block_header {
    uint64_t size;      // размер полезной области (без заголовка)
    int      free;
    struct block_header *next;
};

static struct block_header *heap_head = NULL;
static uint64_t heap_next_virt = HEAP_VIRT_BASE;

void kmalloc_init(uint64_t hhdm_offset) {
    (void)hhdm_offset; // больше не нужен напрямую — вся работа через vmm_map_page
    heap_next_virt = HEAP_VIRT_BASE;
    heap_head = NULL;
}

/* мапит `pages` физически (не обязательно смежных) страниц подряд
 * в виртуально смежный диапазон кучи и возвращает начало этого диапазона */
static void *heap_grow(uint64_t pages) {
    if (heap_next_virt + pages * PAGE_SIZE > HEAP_VIRT_MAX) {
        kprintf("[kmalloc] куча исчерпана (виртуальный лимит)\n");
        return NULL;
    }

    uint64_t start_virt = heap_next_virt;
    for (uint64_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            kprintf("[kmalloc] OOM: не хватает физической памяти\n");
            return NULL;
        }
        if (vmm_map_page(heap_next_virt, (uint64_t)phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            kprintf("[kmalloc] ошибка отображения страницы кучи\n");
            return NULL;
        }
        heap_next_virt += PAGE_SIZE;
    }
    return (void*)start_virt;
}

void *kmalloc(uint64_t size) {
    if (size == 0) return NULL;
    size = (size + 15) & ~15UL;

    struct block_header *b = heap_head;
    struct block_header *prev = NULL;
    while (b) {
        if (b->free && b->size >= size) {
            b->free = 0;
            return (void*)((uint64_t)b + sizeof(struct block_header));
        }
        prev = b;
        b = b->next;
    }

    uint64_t total = size + sizeof(struct block_header);
    uint64_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void *mem = heap_grow(pages);
    if (!mem) return NULL;

    struct block_header *nb = (struct block_header*)mem;
    nb->size = pages * PAGE_SIZE - sizeof(struct block_header);
    nb->free = 0;
    nb->next = NULL;

    if (prev) prev->next = nb; else heap_head = nb;
    return (void*)((uint64_t)nb + sizeof(struct block_header));
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct block_header *b = (struct block_header*)((uint64_t)ptr - sizeof(struct block_header));
    b->free = 1;
}

void *kzalloc(uint64_t size) {
    void *p = kmalloc(size);
    if (p) {
        uint8_t *b = (uint8_t*)p;
        for (uint64_t i = 0; i < size; i++) b[i] = 0;
    }
    return p;
}
