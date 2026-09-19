#include <bnux/types.h>

extern void *pmm_alloc_page(void);
extern void  kprintf(const char *fmt, ...);

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t hhdm_offset = 0;
static int vmm_initialized = 0;

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v & PAGE_ADDR_MASK;
}

static inline void invlpg(uint64_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static uint64_t *phys_to_virt(uint64_t phys) {
    return (uint64_t*)(phys + hhdm_offset);
}

void vmm_init(uint64_t hhdm) {
    if (vmm_initialized) return;
    vmm_initialized = 1;
    hhdm_offset = hhdm;
    kprintf("[vmm] инициализирован, pml4 phys=%p\n", read_cr3());
}

uint64_t vmm_get_hhdm_offset(void) {
    return hhdm_offset;
}

/* Возвращает виртуальный адрес таблицы следующего уровня по индексу entry,
 * создавая её (через PMM), если она ещё не существует. */
static uint64_t *get_next_table(uint64_t *table, int index, int create) {
    if (table[index] & PAGE_PRESENT) {
        return phys_to_virt(table[index] & PAGE_ADDR_MASK);
    }
    if (!create) return NULL;

    void *new_phys = pmm_alloc_page();
    if (!new_phys) {
        kprintf("[vmm] OOM при создании таблицы страниц\n");
        return NULL;
    }
    uint64_t *new_virt = phys_to_virt((uint64_t)new_phys);
    for (int i = 0; i < 512; i++) new_virt[i] = 0;

    table[index] = (uint64_t)new_phys | PAGE_PRESENT | PAGE_WRITABLE;
    return new_virt;
}

/* Отображает одну 4KB страницу virt -> phys с заданными флагами.
 * Работает поверх уже активных таблиц (тех, что установил Limine) —
 * мы их дополняем, а не заменяем целиком, это безопаснее на старте. */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = phys_to_virt(read_cr3());

    int i4 = (virt >> 39) & 0x1FF;
    int i3 = (virt >> 30) & 0x1FF;
    int i2 = (virt >> 21) & 0x1FF;
    int i1 = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_next_table(pml4, i4, 1);
    if (!pdpt) return -1;
    uint64_t *pd = get_next_table(pdpt, i3, 1);
    if (!pd) return -1;
    uint64_t *pt = get_next_table(pd, i2, 1);
    if (!pt) return -1;

    pt[i1] = (phys & PAGE_ADDR_MASK) | flags | PAGE_PRESENT;
    invlpg(virt);
    return 0;
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t *pml4 = phys_to_virt(read_cr3());
    int i4 = (virt >> 39) & 0x1FF;
    int i3 = (virt >> 30) & 0x1FF;
    int i2 = (virt >> 21) & 0x1FF;
    int i1 = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_next_table(pml4, i4, 0); if (!pdpt) return;
    uint64_t *pd   = get_next_table(pdpt, i3, 0); if (!pd) return;
    uint64_t *pt   = get_next_table(pd, i2, 0);   if (!pt) return;

    pt[i1] = 0;
    invlpg(virt);
}
