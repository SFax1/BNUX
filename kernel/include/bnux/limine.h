/* Урезанная версия limine.h — только то, что использует BNUX на старте.
 *
 * ВАЖНО: перед реальной сборкой замени этот файл официальным
 * single-header limine.h из репозитория limine-bootloader/limine
 * (ветка trunk, файл limine.h) — там гарантированно верные magic-числа
 * под ту версию протокола, что использует твой limine-бинарник.
 * Здесь значения даны по протоколу v3 по памяти и могут разойтись
 * с той версией Limine, которую ты реально скачаешь.
 */
#ifndef BNUX_LIMINE_H
#define BNUX_LIMINE_H

#include <bnux/types.h>

#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_BASE_REVISION(N) \
    __attribute__((used, section(".limine_reqs"))) \
    static volatile uint64_t limine_base_revision[3] = \
    { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }

struct limine_file { void *unused; };

/* --- Framebuffer --- */
struct limine_framebuffer {
    void     *address;
    uint64_t  width;
    uint64_t  height;
    uint64_t  pitch;
    uint16_t  bpp;
    uint8_t   memory_model;
    uint8_t   red_mask_size, red_mask_shift;
    uint8_t   green_mask_size, green_mask_shift;
    uint8_t   blue_mask_size, blue_mask_shift;
    uint8_t   unused[7];
    uint64_t  edid_size;
    void     *edid;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct PACKED limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

/* --- Memory map --- */
#define LIMINE_MEMMAP_USABLE 0

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct PACKED limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

/* --- HHDM (higher-half direct map) --- */
struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct PACKED limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

#define LIMINE_FRAMEBUFFER_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b }
#define LIMINE_MEMMAP_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }
#define LIMINE_HHDM_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }

#endif
