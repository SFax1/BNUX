#include <bnux/types.h>

extern int bnuxfs_count(void);
struct bnuxfs_file { char name[32]; const uint8_t *data; uint64_t size; };
extern const struct bnuxfs_file *bnuxfs_get(int index);
extern const struct bnuxfs_file *bnuxfs_find(const char *name);

struct fat32_simple_entry { char name[13]; uint64_t size; int is_dir; };
extern int fat32_get_root_entries(struct fat32_simple_entry *out, int max);
extern int64_t fat32_read_file(const char *name, uint8_t *buf, uint64_t maxlen);
extern int fat32_is_mounted(void);

/* VFS BNUX: единое дерево /bnux/src/<устройство>/<файл>.
 * RAM0  = встроенный ramdisk (BNUXFS, "прошитые" в ядро файлы)
 * MSD1  = первый обнаруженный диск (BNUX System Disk), сейчас FAT32
 * Подкаталогов пока нет — оба backend'а плоские (ramdisk всегда был,
 * FAT32-драйвер пока читает только корневой каталог). Это ограничение
 * на будущее, не архитектурный тупик — VFS уже готов к тому, чтобы
 * потом добавить вложенность. */

struct vfs_entry { char name[13]; uint64_t size; int is_dir; };

static int str_eq_ci(const char *a, const char *b) { // оба и так в верхнем регистре у нас
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Список примонтированных устройств — это и есть содержимое /bnux/src */
int vfs_list_mounts(struct vfs_entry *out, int max) {
    int n = 0;
    if (n < max) {
        str_copy(out[n].name, "RAM0", 13);
        out[n].size = 0; out[n].is_dir = 1; n++;
    }
    if (fat32_is_mounted() && n < max) {
        str_copy(out[n].name, "MSD1", 13);
        out[n].size = 0; out[n].is_dir = 1; n++;
    }
    return n;
}

/* Список файлов внутри устройства (device = "RAM0" или "MSD1").
 * Возвращает -1 если устройство неизвестно. */
int vfs_list_device(const char *device, struct vfs_entry *out, int max) {
    if (str_eq_ci(device, "RAM0")) {
        int n = bnuxfs_count();
        if (n > max) n = max;
        for (int i = 0; i < n; i++) {
            const struct bnuxfs_file *f = bnuxfs_get(i);
            str_copy(out[i].name, f->name, 13);
            out[i].size = f->size;
            out[i].is_dir = 0;
        }
        return n;
    }
    if (str_eq_ci(device, "MSD1")) {
        struct fat32_simple_entry tmp[32];
        int n = fat32_get_root_entries(tmp, max < 32 ? max : 32);
        for (int i = 0; i < n; i++) {
            str_copy(out[i].name, tmp[i].name, 13);
            out[i].size = tmp[i].size;
            out[i].is_dir = tmp[i].is_dir;
        }
        return n;
    }
    return -1;
}

/* Читает файл по device+filename в buf, возвращает число байт или -1 */
int64_t vfs_read_file(const char *device, const char *filename, uint8_t *buf, uint64_t maxlen) {
    if (str_eq_ci(device, "RAM0")) {
        const struct bnuxfs_file *f = bnuxfs_find(filename);
        if (!f) return -1;
        uint64_t n = f->size < maxlen ? f->size : maxlen;
        for (uint64_t i = 0; i < n; i++) buf[i] = f->data[i];
        return (int64_t)n;
    }
    if (str_eq_ci(device, "MSD1")) {
        return fat32_read_file(filename, buf, maxlen);
    }
    return -1;
}

/* Разбирает путь вида "/BNUX/SRC/RAM0/README.TXT" или просто
 * "RAM0/README.TXT" (короткая форма) на device+filename.
 * Если в пути нет '/', весь путь — это device (filename остаётся пустым). */
void vfs_split_path(const char *path, char *device_out, char *filename_out) {
    const char *p = path;
    const char *prefix = "/BNUX/SRC/";
    int plen = 11;
    int match = 1;
    for (int i = 0; i < plen; i++) {
        if (p[i] != prefix[i]) { match = 0; break; }
    }
    if (match) p += plen;

    int i = 0;
    while (p[i] && p[i] != '/') { device_out[i] = p[i]; i++; }
    device_out[i] = 0;

    filename_out[0] = 0;
    if (p[i] == '/') {
        p += i + 1;
        int j = 0;
        while (p[j]) { filename_out[j] = p[j]; j++; }
        filename_out[j] = 0;
    }
}
