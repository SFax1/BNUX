#include <bnux/types.h>

extern char kbd_getchar(void);
extern void console_putchar(char c);
extern void console_puts(const char *s);
extern void kprintf(const char *fmt, ...);
extern void task_list(void);
extern void task_yield(void);
extern void *kmalloc(uint64_t size);
extern void desktop_run(void *fb, uint64_t w, uint64_t h, uint64_t pitch);
extern void pmm_get_stats(uint64_t *total_pages, uint64_t *free_pages);

struct rtc_time { uint8_t sec, min, hour, day, month; uint16_t year; };
extern void rtc_read(struct rtc_time *t);
extern void pci_scan_and_report(void);
extern int  uhci_init(void);
extern void uhci_check_ports(void);
extern int  usb_mouse_init(void);

struct vfs_entry { char name[13]; uint64_t size; int is_dir; };
extern int vfs_list_mounts(struct vfs_entry *out, int max);
extern int vfs_list_device(const char *device, struct vfs_entry *out, int max);
extern int64_t vfs_read_file(const char *device, const char *filename, uint8_t *buf, uint64_t maxlen);
extern void vfs_split_path(const char *path, char *device_out, char *filename_out);

#define LINE_MAX 128
#define HISTORY_SIZE 8

static char history[HISTORY_SIZE][LINE_MAX];
static int history_count = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void read_line(char *buf, int max) {
    int i = 0;
    for (;;) {
        char c = kbd_getchar();
        if (c == '\n') { console_putchar('\n'); break; }
        if (c == '\b') {
            if (i > 0) { i--; console_putchar('\b'); }
            continue;
        }
        if (i < max - 1 && c >= ' ') {
            if (c >= 'a' && c <= 'z') c -= 32;
            buf[i++] = c;
            console_putchar(c);
        }
    }
    buf[i] = 0;
}

static void split(char *line, char **cmd, char **arg) {
    *cmd = line;
    *arg = NULL;
    char *p = line;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        *p = 0;
        p++;
        while (*p == ' ') p++;
        if (*p) *arg = p;
    }
}

static void cmd_help(void) {
    console_puts("AVAILABLE COMMANDS:\n");
    console_puts("  HELP             - THIS MESSAGE\n");
    console_puts("  LS               - LIST /BNUX/SRC (MOUNTED DISKS)\n");
    console_puts("  LS <DEV>         - LIST FILES ON DEVICE (E.G. LS RAM0)\n");
    console_puts("  CAT <DEV>/<FILE> - PRINT FILE (E.G. CAT RAM0/README.TXT)\n");
    console_puts("  GUI              - LAUNCH DESKTOP MODE (ESC TO EXIT)\n");
    console_puts("  UNAME            - KERNEL INFO\n");
    console_puts("  WHOAMI           - CURRENT USER\n");
    console_puts("  PWD              - CURRENT DIRECTORY\n");
    console_puts("  DATE             - CURRENT DATE/TIME (CMOS RTC)\n");
    console_puts("  MEMINFO          - MEMORY USAGE\n");
    console_puts("  HISTORY          - COMMAND HISTORY\n");
    console_puts("  PCI              - LIST PCI STORAGE CONTROLLERS\n");
    console_puts("  USBINIT          - INIT UHCI CONTROLLER (STAGE 1)\n");
    console_puts("  USBPORTS         - CHECK USB PORT STATUS\n");
    console_puts("  USBMOUSE         - ENUMERATE + INIT USB MOUSE (STAGE 2)\n");
    console_puts("  ECHO <TEXT>      - PRINT TEXT\n");
    console_puts("  PS               - LIST TASKS\n");
    console_puts("  VER              - KERNEL VERSION\n");
    console_puts("  CLEAR            - CLEAR SCREEN\n");
}

static void print_size(uint64_t sz) {
    char buf[24]; int i = 0;
    if (sz == 0) { console_putchar('0'); return; }
    while (sz) { buf[i++] = '0' + (sz % 10); sz /= 10; }
    while (i--) console_putchar(buf[i]);
}

static void cmd_ls(const char *arg) {
    struct vfs_entry entries[32];
    if (!arg) {
        int n = vfs_list_mounts(entries, 32);
        console_puts("/BNUX/SRC:\n");
        for (int i = 0; i < n; i++) {
            console_puts("  ");
            console_puts(entries[i].name);
            console_puts("  <DEV>\n");
        }
        return;
    }
    char device[13], filename[64];
    vfs_split_path(arg, device, filename);
    int n = vfs_list_device(device, entries, 32);
    if (n < 0) { console_puts("UNKNOWN DEVICE (TRY: LS TO SEE MOUNTS)\n"); return; }
    for (int i = 0; i < n; i++) {
        console_puts("  ");
        console_puts(entries[i].name);
        console_puts(entries[i].is_dir ? "  <DIR>\n" : "  ");
        if (!entries[i].is_dir) { print_size(entries[i].size); console_puts(" BYTES\n"); }
    }
}

static void cmd_cat(const char *arg) {
    if (!arg) { console_puts("USAGE: CAT <DEVICE>/<FILE>, E.G. CAT RAM0/README.TXT\n"); return; }
    char device[13], filename[64];
    vfs_split_path(arg, device, filename);
    if (filename[0] == 0) { console_puts("USAGE: CAT <DEVICE>/<FILE>\n"); return; }
    uint8_t *buf = (uint8_t*)kmalloc(65536);
    if (!buf) { console_puts("OUT OF MEMORY\n"); return; }
    int64_t n = vfs_read_file(device, filename, buf, 65536);
    if (n < 0) { console_puts("FILE NOT FOUND\n"); return; }
    for (int64_t i = 0; i < n; i++) console_putchar((char)buf[i]);
}

extern void console_init(void *fb, uint64_t w, uint64_t h, uint64_t pitch);

static void *saved_fb; static uint64_t saved_w, saved_h, saved_pitch;

void shell_register_fb(void *fb, uint64_t w, uint64_t h, uint64_t pitch) {
    saved_fb = fb; saved_w = w; saved_h = h; saved_pitch = pitch;
}

static void cmd_clear(void) {
    console_init(saved_fb, saved_w, saved_h, saved_pitch);
}

static void cmd_uname(void) {
    console_puts("BNUX 0.1 X86_64 LEVERSOFT MONOLITHIC KERNEL\n");
}

static void cmd_whoami(void) {
    console_puts("ROOT (NO USER SYSTEM YET)\n");
}

static void cmd_pwd(void) {
    console_puts("/BNUX (SHELL ALWAYS AT ROOT - NO CD YET)\n");
}

static void print_num(uint64_t v) {
    char buf[24]; int i = 0;
    if (v == 0) { console_putchar('0'); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) console_putchar(buf[i]);
}

static void cmd_date(void) {
    struct rtc_time t;
    rtc_read(&t);
    print_num(t.year); console_putchar('-');
    if (t.month < 10) console_putchar('0');
    print_num(t.month); console_putchar('-');
    if (t.day < 10) console_putchar('0');
    print_num(t.day); console_putchar(' ');
    if (t.hour < 10) console_putchar('0');
    print_num(t.hour); console_putchar(':');
    if (t.min < 10) console_putchar('0');
    print_num(t.min); console_putchar(':');
    if (t.sec < 10) console_putchar('0');
    print_num(t.sec);
    console_puts(" (FROM CMOS RTC)\n");
}

static void cmd_meminfo(void) {
    uint64_t total, free_pages;
    pmm_get_stats(&total, &free_pages);
    uint64_t total_mb = (total * 4096) / (1024 * 1024);
    uint64_t free_mb  = (free_pages * 4096) / (1024 * 1024);
    console_puts("TOTAL: "); print_num(total_mb); console_puts(" MB\n");
    console_puts("FREE:  "); print_num(free_mb); console_puts(" MB\n");
    console_puts("USED:  "); print_num(total_mb - free_mb); console_puts(" MB\n");
}

static void cmd_history(void) {
    for (int i = 0; i < history_count; i++) {
        print_num(i + 1); console_puts("  "); console_puts(history[i]); console_puts("\n");
    }
}

void shell_run(void) {
    char line[LINE_MAX];
    console_puts("\nBNUXDOS V0.1 (C) LEVERSOFT\n");
    console_puts("TYPE HELP FOR COMMANDS.\n");

    for (;;) {
        console_puts("BNUX> ");
        read_line(line, LINE_MAX);
        char *cmd, *arg;

        if (!str_eq(line, "")) {
            if (history_count < HISTORY_SIZE) {
                str_copy(history[history_count++], line, LINE_MAX);
            } else {
                for (int i = 1; i < HISTORY_SIZE; i++) str_copy(history[i-1], history[i], LINE_MAX);
                str_copy(history[HISTORY_SIZE-1], line, LINE_MAX);
            }
        }

        split(line, &cmd, &arg);
        if (str_eq(cmd, "")) continue;
        else if (str_eq(cmd, "HELP")) cmd_help();
        else if (str_eq(cmd, "LS")) cmd_ls(arg);
        else if (str_eq(cmd, "CAT")) cmd_cat(arg);
        else if (str_eq(cmd, "ECHO")) { if (arg) console_puts(arg); console_puts("\n"); }
        else if (str_eq(cmd, "PS")) task_list();
        else if (str_eq(cmd, "UNAME")) cmd_uname();
        else if (str_eq(cmd, "WHOAMI")) cmd_whoami();
        else if (str_eq(cmd, "PWD")) cmd_pwd();
        else if (str_eq(cmd, "DATE")) cmd_date();
        else if (str_eq(cmd, "MEMINFO")) cmd_meminfo();
        else if (str_eq(cmd, "HISTORY")) cmd_history();
        else if (str_eq(cmd, "PCI")) pci_scan_and_report();
        else if (str_eq(cmd, "USBINIT")) uhci_init();
        else if (str_eq(cmd, "USBPORTS")) uhci_check_ports();
        else if (str_eq(cmd, "USBMOUSE")) usb_mouse_init();
        else if (str_eq(cmd, "GUI")) {
            desktop_run(saved_fb, saved_w, saved_h, saved_pitch);
            console_init(saved_fb, saved_w, saved_h, saved_pitch);
            console_puts("BACK TO BNUXDOS SHELL.\n");
        }
        else if (str_eq(cmd, "VER")) console_puts("BNUX KERNEL 0.1 - X86_64 - LEVERSOFT\n");
        else if (str_eq(cmd, "CLEAR")) cmd_clear();
        else { console_puts("UNKNOWN COMMAND: "); console_puts(cmd); console_puts("\n"); }
    }
}
