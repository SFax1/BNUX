#ifndef BNUX_BOOTINFO_H
#define BNUX_BOOTINFO_H

#include <bnux/types.h>

/* Нейтральная структура, которую заполняет loader-специфичный код
 * (boot_limine.c ИЛИ boot_grub.c) перед вызовом общей kernel_main(). */

struct boot_fb {
    int      present;
    void    *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
};

struct boot_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type; // 0 = usable, что угодно ещё = занято/зарезервировано
};

struct boot_info {
    struct boot_fb fb;
    struct boot_mmap_entry *mmap;
    uint64_t mmap_count;
    uint64_t hhdm_offset; // виртуальный базовый адрес прямого отображения физ.памяти
};

void kernel_main(struct boot_info *bi);

#endif
