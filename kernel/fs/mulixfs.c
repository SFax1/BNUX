#include <bnux/types.h>

extern void kprintf(const char *fmt, ...);

#define BNUXFS_MAX_FILES 32
#define BNUXFS_NAME_LEN  32

struct bnuxfs_file {
    char     name[BNUXFS_NAME_LEN];
    const uint8_t *data;
    uint64_t size;
};

static struct bnuxfs_file files[BNUXFS_MAX_FILES];
static int file_count = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Регистрирует файл в ramdisk. Данные хранятся статически в бинарнике
 * ядра (см. initrd_data.c) — реальной записи на диск пока нет,
 * это read-only "прошитая" в образ файловая система. */
void bnuxfs_add_file(const char *name, const uint8_t *data, uint64_t size) {
    if (file_count >= BNUXFS_MAX_FILES) return;
    str_copy(files[file_count].name, name, BNUXFS_NAME_LEN);
    files[file_count].data = data;
    files[file_count].size = size;
    file_count++;
}

int bnuxfs_count(void) { return file_count; }

const struct bnuxfs_file *bnuxfs_get(int index) {
    if (index < 0 || index >= file_count) return NULL;
    return &files[index];
}

const struct bnuxfs_file *bnuxfs_find(const char *name) {
    for (int i = 0; i < file_count; i++) {
        if (str_eq(files[i].name, name)) return &files[i];
    }
    return NULL;
}
