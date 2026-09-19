#include <bnux/types.h>

struct mb2_mm_region { uint64_t base; uint64_t length; uint64_t type; };

struct mb2_fb_info {
    uint64_t addr;
    uint32_t width, height, pitch;
    uint8_t bpp;
    int valid;
};        

struct mb2_tag_header { uint32_t type; uint32_t size; };
      
#define MB2_TAG_END        0
#define MB2_TAG_MEMORY_MAP 6
#define MB2_TAG_FRAMEBUFFER 8 

/* Ограничение GRUB-пути: наши статические page tables (multiboot2.S)
 * покрывают только физические первые 1GB — усечём карту памяти под это,
 * чтобы PMM не выдал страницу, к которой у нас физически нет отображения. */
#define GRUB_MAPPED_PHYS_LIMIT 0x40000000ULL

void mb2_parse(uint32_t mb_addr, struct mb2_mm_region *out_regions, int *out_count,
               struct mb2_fb_info *out_fb) {
    *out_count = 0; 
    out_fb->valid = 0;

    uint8_t *ptr = (uint8_t*)(uint64_t)mb_addr;
    uint32_t total_size = *(uint32_t*)ptr;
    uint8_t *end = ptr + total_size;
    ptr += 8; // пропускаем total_size(4) + reserved(4)

    while (ptr < end) {
        struct mb2_tag_header *tag = (struct mb2_tag_header*)ptr;
        if (tag->type == MB2_TAG_END) break;
-- РЕЖИМ ВСТАВКИ --                                                                       2, 1       Начало
