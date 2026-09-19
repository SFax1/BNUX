#include <bnux/types.h>

extern int  ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
extern void kprintf(const char *fmt, ...);
extern void *kmalloc(uint64_t size);

static uint16_t bytes_per_sector;
static uint8_t  sectors_per_cluster;
static uint16_t reserved_sectors;
static uint8_t  num_fats;
static uint32_t fat_size_sectors;
static uint32_t root_cluster;
static uint32_t first_data_sector;
static uint32_t fat_start_sector;
static int fs_ready = 0;

struct PACKED fat32_dirent {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;
    uint32_t size;
};

#define ATTR_LONG_NAME 0x0F
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10

int fat32_init(void) {
    uint8_t *sector = (uint8_t*)kmalloc(512);
    if (!sector) return -1;

    if (ata_read_sectors(0, 1, sector) != 0) {
        kprintf("[fat32] не удалось прочитать сектор 0 (нет диска?)\n");
        return -1;
    }

    /* сигнатура 0x55AA в конце загрузочного сектора — минимальная проверка,
     * что тут вообще что-то похожее на валидную ФС */
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        kprintf("[fat32] нет сигнатуры boot-сектора — диск не размечен?\n");
        return -1;
    }

    bytes_per_sector   = sector[11] | (sector[12] << 8);
    sectors_per_cluster = sector[13];
    reserved_sectors   = sector[14] | (sector[15] << 8);
    num_fats           = sector[16];
    fat_size_sectors   = sector[36] | (sector[37]<<8) | (sector[38]<<16) | (sector[39]<<24);
    root_cluster       = sector[44] | (sector[45]<<8) | (sector[46]<<16) | (sector[47]<<24);

    fat_start_sector  = reserved_sectors;
    first_data_sector = reserved_sectors + (num_fats * fat_size_sectors);

    if (bytes_per_sector != 512 || sectors_per_cluster == 0) {
        kprintf("[fat32] неожиданные параметры BPB, похоже не FAT32\n");
        return -1;
    }

    fs_ready = 1;
    kprintf("[fat32] смонтирован: %d B/sector, %d sect/cluster, root cluster=%d\n",
            bytes_per_sector, sectors_per_cluster, root_cluster);
    return 0;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return first_data_sector + (cluster - 2) * sectors_per_cluster;
}

/* вернёт следующий кластер в цепочке, либо 0 если конец (>=0x0FFFFFF8) */
static uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % bytes_per_sector;

    uint8_t *buf = (uint8_t*)kmalloc(bytes_per_sector);
    if (!buf) return 0;
    if (ata_read_sectors(fat_sector, 1, buf) != 0) return 0;

    uint32_t val = buf[offset_in_sector] | (buf[offset_in_sector+1]<<8) |
                   (buf[offset_in_sector+2]<<16) | (buf[offset_in_sector+3]<<24);
    val &= 0x0FFFFFFF;
    if (val >= 0x0FFFFFF8) return 0;
    return val;
}

static void format_83_name(const char *input, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int oi = 0;
    int i = 0;
    while (input[i] && input[i] != '.' && oi < 8) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[oi++] = c;
        i++;
    }
    while (input[i] && input[i] != '.') i++;
    if (input[i] == '.') {
        i++;
        int ei = 8;
        while (input[i] && ei < 11) {
            char c = input[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[ei++] = c;
            i++;
        }
    }
}

static void print_83_name(const uint8_t name[11]) {
    for (int i = 0; i < 8 && name[i] != ' '; i++) kprintf("%c", name[i]);
    if (name[8] != ' ') {
        kprintf(".");
        for (int i = 8; i < 11 && name[i] != ' '; i++) kprintf("%c", name[i]);
    }
}

/* обходит цепочку кластеров каталога и вызывает callback для каждой записи */
typedef void (*dirent_cb)(struct fat32_dirent *e, void *ctx);

static void walk_directory(uint32_t start_cluster, dirent_cb cb, void *ctx) {
    uint32_t cluster = start_cluster;
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    uint8_t *buf = (uint8_t*)kmalloc(cluster_bytes);
    if (!buf) return;

    while (cluster) {
        ata_read_sectors(cluster_to_lba(cluster), sectors_per_cluster, buf);
        struct fat32_dirent *entries = (struct fat32_dirent*)buf;
        int n = cluster_bytes / sizeof(struct fat32_dirent);

        for (int i = 0; i < n; i++) {
            if (entries[i].name[0] == 0x00) return; // конец каталога
            if (entries[i].name[0] == 0xE5) continue; // удалён
            if (entries[i].attr == ATTR_LONG_NAME) continue; // пропускаем LFN-записи
            if (entries[i].attr & ATTR_VOLUME_ID) continue;
            cb(&entries[i], ctx);
        }
        cluster = fat_next_cluster(cluster);
    }
}

static void list_cb(struct fat32_dirent *e, void *ctx) {
    (void)ctx;
    print_83_name(e->name);
    kprintf(e->attr & ATTR_DIRECTORY ? "  <DIR>\n" : "  %d BYTES\n", e->size);
}

int fat32_is_mounted(void) {
    return fs_ready;
}

void fat32_list_root(void) {
    if (!fs_ready) { kprintf("FAT32 НЕ СМОНТИРОВАН (НЕТ ДИСКА)\n"); return; }
    walk_directory(root_cluster, list_cb, NULL);
}

struct fat32_simple_entry { char name[13]; uint64_t size; int is_dir; };

struct collect_ctx { struct fat32_simple_entry *out; int max; int count; };

static void collect_cb(struct fat32_dirent *e, void *ctx) {
    struct collect_ctx *cc = (struct collect_ctx*)ctx;
    if (cc->count >= cc->max) return;

    struct fat32_simple_entry *dst = &cc->out[cc->count];
    int oi = 0;
    for (int i = 0; i < 8 && e->name[i] != ' '; i++) dst->name[oi++] = e->name[i];
    if (e->name[8] != ' ') {
        dst->name[oi++] = '.';
        for (int i = 8; i < 11 && e->name[i] != ' '; i++) dst->name[oi++] = e->name[i];
    }
    dst->name[oi] = 0;
    dst->size = e->size;
    dst->is_dir = (e->attr & ATTR_DIRECTORY) ? 1 : 0;
    cc->count++;
}

/* Отдаёт содержимое корневого каталога в массив — для GUI (окно "Files"),
 * в отличие от fat32_list_root() которая просто печатает в serial. */
int fat32_get_root_entries(struct fat32_simple_entry *out, int max) {
    if (!fs_ready) return 0;
    struct collect_ctx cc = { out, max, 0 };
    walk_directory(root_cluster, collect_cb, &cc);
    return cc.count;
}

struct find_ctx { uint8_t target[11]; struct fat32_dirent found; int ok; };

static void find_cb(struct fat32_dirent *e, void *ctx) {
    struct find_ctx *fc = (struct find_ctx*)ctx;
    if (fc->ok) return;
    int match = 1;
    for (int i = 0; i < 11; i++) if (e->name[i] != fc->target[i]) { match = 0; break; }
    if (match) { fc->found = *e; fc->ok = 1; }
}

/* читает файл по короткому 8.3 имени (например "README.TXT") в buf,
 * возвращает число прочитанных байт или -1 если не найден/ошибка */
int64_t fat32_read_file(const char *name, uint8_t *buf, uint64_t maxlen) {
    if (!fs_ready) return -1;

    struct find_ctx fc;
    fc.ok = 0;
    format_83_name(name, fc.target);
    walk_directory(root_cluster, find_cb, &fc);
    if (!fc.ok) return -1;

    uint32_t cluster = (fc.found.cluster_hi << 16) | fc.found.cluster_lo;
    uint32_t remaining = fc.found.size;
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    uint64_t written = 0;

    uint8_t *tmp = (uint8_t*)kmalloc(cluster_bytes);
    if (!tmp) return -1;

    while (cluster && remaining > 0 && written < maxlen) {
        ata_read_sectors(cluster_to_lba(cluster), sectors_per_cluster, tmp);
        uint32_t chunk = cluster_bytes;
        if (chunk > remaining) chunk = remaining;
        if (written + chunk > maxlen) chunk = maxlen - written;

        for (uint32_t i = 0; i < chunk; i++) buf[written + i] = tmp[i];
        written += chunk;
        remaining -= chunk;
        cluster = fat_next_cluster(cluster);
    }
    return (int64_t)written;
}
