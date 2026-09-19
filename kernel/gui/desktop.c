#include <bnux/types.h>

extern void kprintf(const char *fmt, ...);
extern int32_t mouse_get_x(void);
extern int32_t mouse_get_y(void);
extern int mouse_left_pressed(void);
extern void mouse_set_screen_bounds(int64_t w, int64_t h);
extern const uint8_t *font_get_glyph(char c);
extern char kbd_getchar(void);
extern int  kbd_has_char(void);
extern void usb_mouse_poll(void);

struct vfs_entry { char name[13]; uint64_t size; int is_dir; };
extern int vfs_list_mounts(struct vfs_entry *out, int max);
extern int vfs_list_device(const char *device, struct vfs_entry *out, int max);

/* ===== Экран ===== */
static uint32_t *fb;
static uint64_t fb_w, fb_h, fb_pitch_px;

/* ===== Палитра macOS Light + Windows 11 Hybrid ===== */
#define C_DESKTOP      0x00F5F5F7
#define C_DESKTOP_GRAD 0x00E8E8ED
#define C_MENU_BAR     0x00FFFFFF
#define C_MENU_BAR_SEP 0x00DDDDDD
#define C_DOCK_BG      0x00FFFFFF
#define C_DOCK_HOV     0x00F0F0F0
#define C_ACCENT       0x00007AFF
#define C_ACCENT_LIGHT 0x004DA3FF
#define C_WIN_BG       0x00FFFFFF
#define C_WIN_TITLE    0x00F9F9FB
#define C_WIN_BORDER   0x00E5E5E5
#define C_WIN_TEXT     0x001D1D1F
#define C_WIN_TEXT_DIM 0x0086868B
#define C_CLOSE_RED    0x00FF5F57
#define C_CLOSE_YELLOW 0x00FEBC2E
#define C_CLOSE_GREEN  0x0028C840
#define C_BTN_HOV      0x00E8E8ED
#define C_SHADOW       0x00000000
#define C_WHITE        0x00FFFFFF
#define C_BLACK        0x00000000
#define C_TRAY_TEXT    0x001D1D1F
#define C_ICON_BLUE    0x00007AFF
#define C_ICON_YELLOW  0x00FF9500
#define C_ICON_RED     0x00FF3B30
#define C_ICON_GREEN   0x0034C759
#define C_ICON_PURPLE  0x00AF52DE
#define C_GLASS        0x00F0F0F0

/* ===== Размеры ===== */
#define MENU_BAR_H   28
#define DOCK_H       64
#define DOCK_Y_OFFSET 10
#define TITLEBAR_H   36
#define BORDER_W     1
#define CORNER_R     10
#define SHADOW_SIZE  6
#define MENU_W       260
#define MENU_ITEM_H  32
#define MENU_ITEMS   6
#define WIN_W        400
#define WIN_H        260
#define FILES_WIN_W  420
#define FILES_WIN_H  320
#define MAX_LIST_ENTRIES 10
#define DOCK_ICON_SZ 48
#define DOCK_PADDING 12

/* ===== ВСЕ переменные состояния (объявлены ДО всех функций) ===== */
static int menu_open = 0;
static int gui_should_exit = 0;
static int last_left = 0;

static int win_open = 0;
static int64_t win_x = 200, win_y = 150;
static int win_dragging = 0;
static int32_t drag_ox = 0, drag_oy = 0;

static int files_win_open = 0;
static int64_t files_win_x = 450, files_win_y = 200;
static int files_win_dragging = 0;
static int32_t files_drag_ox = 0, files_drag_oy = 0;
static char files_current_device[13] = "";

/* Z-order: какое окно рисуется НАВЕРХУ (и первым получает клики). */
static int top_window = 1;

static void bring_to_front(int win_id) {
    top_window = win_id;
}

static const char *menu_labels[MENU_ITEMS] = {
    "About BNUX", "File Manager", "Settings", "Appearance", "Help", "Shut Down GUI"
};

/* Dock иконки */
static const char *dock_labels[] = {"Finder", "About", "Files", "Settings", "Terminal"};
static int dock_count = 5;

/* ===== Примитивы рисования ===== */
static void put_px(int64_t x, int64_t y, uint32_t c) {
    if (x >= 0 && x < (int64_t)fb_w && y >= 0 && y < (int64_t)fb_h)
        fb[y * fb_pitch_px + x] = c;
}

static void fill_rect(int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int64_t)fb_w) w = fb_w - x;
    if (y + h > (int64_t)fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;
    for (int64_t yy = y; yy < y + h; yy++)
        for (int64_t xx = x; xx < x + w; xx++)
            fb[yy * fb_pitch_px + xx] = color;
}

static void draw_border(int64_t x, int64_t y, int64_t w, int64_t h, int64_t t, uint32_t c) {
    fill_rect(x, y, w, t, c);
    fill_rect(x, y + h - t, w, t, c);
    fill_rect(x, y, t, h, c);
    fill_rect(x + w - t, y, t, h, c);
}

static void draw_char(char c, int64_t x, int64_t y, uint32_t color, int scale) {
    const uint8_t *g = font_get_glyph(c);
    if (!g) return;
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if ((g[row] >> (4 - col)) & 1)
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
}

static void draw_text(const char *s, int64_t x, int64_t y, uint32_t color, int scale) {
    int64_t cx = x;
    while (*s) {
        draw_char(*s, cx, y, color, scale);
        cx += 6 * scale;
        s++;
    }
}

static int point_in(int64_t px, int64_t py, int64_t x, int64_t y, int64_t w, int64_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* ===== Скругленные углы (упрощенно через пиксели по углам) ===== */
static void draw_rounded_rect(int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color) {
    int r = CORNER_R;
    fill_rect(x + r, y, w - 2*r, h, color);
    fill_rect(x, y + r, w, h - 2*r, color);
    for (int i = 0; i < r; i++) {
        int offset = r - i - 1;
        if (offset < 0) offset = 0;
        fill_rect(x + offset, y + i, w - 2*offset, 1, color);
        fill_rect(x + offset, y + h - i - 1, w - 2*offset, 1, color);
    }
}

/* ===== Иконки macOS style ===== */
static void draw_apple_logo(int64_t x, int64_t y, int sz, uint32_t c) {
    int cx = x + sz/2, cy = y + sz/2;
    fill_rect(cx - sz/4, cy - sz/6, sz/2, sz/3, c);
    fill_rect(cx - sz/3, cy - sz/4, sz/6, sz/4, c);
    put_px(cx + sz/8, cy - sz/3, c);
}

static void draw_folder_icon(int64_t x, int64_t y, int sz) {
    int tab_w = sz / 3;
    int tab_h = sz / 5;
    fill_rect(x, y + tab_h, sz, sz - tab_h, C_ICON_BLUE);
    fill_rect(x, y, tab_w, tab_h, C_ICON_BLUE);
    fill_rect(x + 2, y + tab_h + 2, sz - 4, sz - tab_h - 4, C_WHITE);
}

static void draw_info_icon(int64_t x, int64_t y, int sz) {
    fill_rect(x + sz/2 - sz/6, y, sz/3, sz, C_ICON_BLUE);
    fill_rect(x, y + sz/2 - sz/6, sz, sz/3, C_ICON_BLUE);
    fill_rect(x + sz/2 - 1, y + 3, 2, 2, C_WHITE);
    fill_rect(x + sz/2 - 1, y + 6, 2, sz - 9, C_WHITE);
}

static void draw_gear_icon(int64_t x, int64_t y, int sz) {
    int cx = x + sz/2, cy = y + sz/2;
    int r = sz/4;
    for (int i = 0; i < 8; i++) {
        int angle = i * 45;
        int dx = (angle == 0 || angle == 180) ? r : (angle == 90 || angle == 270) ? 0 : r/2;
        int dy = (angle == 90 || angle == 270) ? r : (angle == 0 || angle == 180) ? 0 : r/2;
        if (angle == 0) dx = r;
        else if (angle == 90) dy = -r;
        else if (angle == 180) dx = -r;
        else if (angle == 270) dy = r;
        else if (angle == 45) { dx = r/2; dy = -r/2; }
        else if (angle == 135) { dx = -r/2; dy = -r/2; }
        else if (angle == 225) { dx = -r/2; dy = r/2; }
        else if (angle == 315) { dx = r/2; dy = r/2; }
        fill_rect(cx + dx - 2, cy + dy - 2, 4, 4, C_WIN_TEXT_DIM);
    }
    fill_rect(cx - sz/6, cy - sz/6, sz/3, sz/3, C_WIN_BG);
}

static void draw_power_icon(int64_t x, int64_t y, int sz) {
    int cx = x + sz/2;
    int r = sz/3;
    for (int i = 0; i < 8; i++) {
        int angle = i * 45;
        int px = cx + (angle == 90 ? -r : angle == 270 ? r : 0);
        int py = y + (angle == 0 ? -r : angle == 180 ? r : 0);
        if (angle == 45) { px = cx + r/2; py = y - r/2; }
        else if (angle == 135) { px = cx - r/2; py = y - r/2; }
        else if (angle == 225) { px = cx - r/2; py = y + r/2; }
        else if (angle == 315) { px = cx + r/2; py = y + r/2; }
        if (angle != 0 && angle != 180) put_px(px, py, C_ICON_RED);
    }
    fill_rect(cx - 1, y + sz/4, 2, sz/2, C_ICON_RED);
}

static void draw_terminal_icon(int64_t x, int64_t y, int sz) {
    fill_rect(x, y, sz, sz, C_BLACK);
    fill_rect(x + 2, y + 2, sz - 4, sz - 4, C_WIN_BG);
    draw_text(">_", x + sz/4, y + sz/3, C_WIN_TEXT_DIM, 1);
}

/* ===== Курсор macOS ===== */
static void draw_cursor(void) {
    int32_t x = mouse_get_x(), y = mouse_get_y();
    for (int i = 0; i < 14; i++) {
        int w = 14 - i;
        if (w > 7) w = 7;
        fill_rect(x + 1, y + i, w, 1, C_WHITE);
    }
    for (int i = 0; i < 15; i++) {
        int w = 15 - i;
        if (w > 8) w = 8;
        put_px(x, y + i, C_BLACK);
        put_px(x + w, y + i, C_BLACK);
    }
    put_px(x, y + 14, C_BLACK);
    fill_rect(x + 5, y + 11, 2, 5, C_WHITE);
    put_px(x + 4, y + 11, C_BLACK);
    put_px(x + 7, y + 11, C_BLACK);
}

/* ===== Menu Bar (macOS style) ===== */
static void draw_menu_bar(void) {
    fill_rect(0, 0, fb_w, MENU_BAR_H, C_MENU_BAR);
    fill_rect(0, MENU_BAR_H - 1, fb_w, 1, C_MENU_BAR_SEP);

    int32_t mx = mouse_get_x(), my = mouse_get_y();

    /* Apple logo + меню приложения */
    int apple_hov = point_in(mx, my, 8, 4, 24, MENU_BAR_H - 8);
    if (apple_hov) fill_rect(8, 4, 24, MENU_BAR_H - 8, C_DOCK_HOV);
    draw_apple_logo(14, 6, 18, C_WIN_TEXT);

    /* Название приложения */
    draw_text("BNUX", 40, 8, C_WIN_TEXT, 1);

    /* Меню приложения */
    const char *items[] = {"File", "Edit", "View", "Go", "Window", "Help"};
    int x_offset = 100;
    for (int i = 0; i < 6; i++) {
        int item_w = 50;
        int hov = point_in(mx, my, x_offset, 4, item_w, MENU_BAR_H - 8);
        if (hov) fill_rect(x_offset, 4, item_w, MENU_BAR_H - 8, C_DOCK_HOV);
        draw_text(items[i], x_offset + 8, 8, C_WIN_TEXT, 1);
        x_offset += item_w + 10;
    }

    /* Трей справа */
    draw_text("Mon 21 Aug", fb_w - 140, 8, C_TRAY_TEXT, 1);
    draw_text("21:37", fb_w - 60, 8, C_TRAY_TEXT, 1);
}

/* ===== Dock (macOS style) ===== */
static void draw_dock(void) {
    int dock_w = dock_count * (DOCK_ICON_SZ + DOCK_PADDING) + DOCK_PADDING * 2;
    int dock_x = (fb_w - dock_w) / 2;
    int dock_y = fb_h - DOCK_H - DOCK_Y_OFFSET;

    int32_t mx = mouse_get_x(), my = mouse_get_y();

    /* Фон dock */
    fill_rect(dock_x - 4, dock_y - 4, dock_w + 8, DOCK_H + 8, C_SHADOW);
    draw_rounded_rect(dock_x, dock_y, dock_w, DOCK_H, C_DOCK_BG);
    draw_border(dock_x, dock_y, dock_w, DOCK_H, 1, C_WIN_BORDER);

    /* Иконки */
    for (int i = 0; i < dock_count; i++) {
        int icon_x = dock_x + DOCK_PADDING + i * (DOCK_ICON_SZ + DOCK_PADDING);
        int icon_y = dock_y + (DOCK_H - DOCK_ICON_SZ) / 2;
        int hov = point_in(mx, my, icon_x, icon_y, DOCK_ICON_SZ, DOCK_ICON_SZ);

        if (hov) {
            fill_rect(icon_x - 4, icon_y - 4, DOCK_ICON_SZ + 8, DOCK_ICON_SZ + 8, C_DOCK_HOV);
        }

        /* Рисуем иконки в зависимости от позиции */
        if (i == 0) draw_folder_icon(icon_x + 8, icon_y + 8, 32);
        else if (i == 1) draw_info_icon(icon_x + 8, icon_y + 8, 32);
        else if (i == 2) draw_folder_icon(icon_x + 8, icon_y + 8, 32);
        else if (i == 3) draw_gear_icon(icon_x + 8, icon_y + 8, 32);
        else if (i == 4) draw_terminal_icon(icon_x + 8, icon_y + 8, 32);

        /* Индикатор активного приложения */
        if ((i == 1 && win_open) || (i == 2 && files_win_open)) {
            fill_rect(icon_x + DOCK_ICON_SZ/2 - 3, dock_y + DOCK_H - 6, 6, 3, C_WIN_TEXT_DIM);
        }
    }
}

/* ===== Кнопки управления окном (macOS style - три цветные кнопки) ===== */
static void draw_window_controls(int64_t wx, int64_t wy, int64_t ww) {
    int32_t mx = mouse_get_x(), my = mouse_get_y();
    int btn_sz = 14;
    int gap = 8;
    int base_x = wx + gap;
    int base_y = wy + (TITLEBAR_H - btn_sz) / 2;

    /* Close button (red) */
    int hov_close = point_in(mx, my, base_x, base_y, btn_sz, btn_sz);
    fill_rect(base_x, base_y, btn_sz, btn_sz, hov_close ? C_CLOSE_RED : C_CLOSE_RED);
    if (hov_close) {
        put_px(base_x + btn_sz/2 - 2, base_y + btn_sz/2 - 1, C_BLACK);
        put_px(base_x + btn_sz/2 + 2, base_y + btn_sz/2 + 1, C_BLACK);
        put_px(base_x + btn_sz/2 - 2, base_y + btn_sz/2 + 1, C_BLACK);
        put_px(base_x + btn_sz/2 + 2, base_y + btn_sz/2 - 1, C_BLACK);
    }

    /* Minimize button (yellow) */
    int hov_min = point_in(mx, my, base_x + btn_sz + 4, base_y, btn_sz, btn_sz);
    fill_rect(base_x + btn_sz + 4, base_y, btn_sz, btn_sz, hov_min ? C_CLOSE_YELLOW : C_CLOSE_YELLOW);
    if (hov_min) {
        fill_rect(base_x + btn_sz + 4 + 3, base_y + btn_sz/2, 8, 2, C_BLACK);
    }

    /* Maximize button (green) */
    int hov_max = point_in(mx, my, base_x + (btn_sz + 4) * 2, base_y, btn_sz, btn_sz);
    fill_rect(base_x + (btn_sz + 4) * 2, base_y, btn_sz, btn_sz, hov_max ? C_CLOSE_GREEN : C_CLOSE_GREEN);
    if (hov_max) {
        int cx = base_x + (btn_sz + 4) * 2 + btn_sz/2;
        int cy = base_y + btn_sz/2;
        for (int i = -3; i <= 3; i++) {
            put_px(cx + i, cy + i, C_BLACK);
            put_px(cx + i, cy - i, C_BLACK);
        }
    }
}

/* ===== Окно About ===== */
static void draw_about_window(void) {
    if (!win_open) return;
    
    /* Тень */
    fill_rect(win_x + SHADOW_SIZE, win_y + SHADOW_SIZE, WIN_W, WIN_H, C_SHADOW);
    
    /* Основное окно со скругленными углами */
    draw_rounded_rect(win_x, win_y, WIN_W, WIN_H, C_WIN_BORDER);
    draw_rounded_rect(win_x + BORDER_W, win_y + BORDER_W, WIN_W - BORDER_W*2, WIN_H - BORDER_W*2, C_WIN_BG);
    
    /* Заголовок */
    fill_rect(win_x + BORDER_W, win_y + BORDER_W, WIN_W - BORDER_W*2, TITLEBAR_H, C_WIN_TITLE);
    fill_rect(win_x + BORDER_W, win_y + BORDER_W + TITLEBAR_H, WIN_W - BORDER_W*2, 1, C_WIN_BORDER);
    
    /* Центрированный заголовок */
    int title_len = 9 * 6; /* "About BNUX" */
    draw_text("About BNUX", win_x + (WIN_W - title_len) / 2, win_y + BORDER_W + 10, C_WIN_TEXT_DIM, 1);
    
    draw_window_controls(win_x + BORDER_W, win_y + BORDER_W, WIN_W - BORDER_W*2);

    int64_t cy = win_y + BORDER_W + TITLEBAR_H + 30;
    draw_text("BNUX OS v0.1", win_x + 20, cy, C_ACCENT, 2);
    cy += 28;
    draw_text("Monolithic x86_64 Kernel", win_x + 20, cy, C_WIN_TEXT, 1);
    cy += 20;
    draw_text("by Leversoft", win_x + 20, cy, C_WIN_TEXT_DIM, 1);
    cy += 20;
    draw_text("Mascot: Lix", win_x + 20, cy, C_WIN_TEXT_DIM, 1);
    cy += 28;
    draw_text("Drag titlebar to move", win_x + 20, cy, C_WIN_TEXT_DIM, 1);
}

/* ===== Окно Files ===== */
static void draw_files_window(void) {
    if (!files_win_open) return;
    
    fill_rect(files_win_x + SHADOW_SIZE, files_win_y + SHADOW_SIZE, FILES_WIN_W, FILES_WIN_H, C_SHADOW);
    draw_rounded_rect(files_win_x, files_win_y, FILES_WIN_W, FILES_WIN_H, C_WIN_BORDER);
    draw_rounded_rect(files_win_x + BORDER_W, files_win_y + BORDER_W, FILES_WIN_W - BORDER_W*2, FILES_WIN_H - BORDER_W*2, C_WIN_BG);
    
    fill_rect(files_win_x + BORDER_W, files_win_y + BORDER_W, FILES_WIN_W - BORDER_W*2, TITLEBAR_H, C_WIN_TITLE);
    fill_rect(files_win_x + BORDER_W, files_win_y + BORDER_W + TITLEBAR_H, FILES_WIN_W - BORDER_W*2, 1, C_WIN_BORDER);
    
    int title_len = 10 * 6;
    draw_text("File Manager", files_win_x + (FILES_WIN_W - title_len) / 2, files_win_y + BORDER_W + 10, C_WIN_TEXT_DIM, 1);
    
    draw_window_controls(files_win_x + BORDER_W, files_win_y + BORDER_W, FILES_WIN_W - BORDER_W*2);

    int64_t content_y = files_win_y + BORDER_W + TITLEBAR_H + 16;

    if (files_current_device[0] != 0) {
        int32_t mx = mouse_get_x(), my = mouse_get_y();
        int hov = point_in(mx, my, files_win_x + 12, content_y, 80, 20);
        fill_rect(files_win_x + 12, content_y, 80, 20, hov ? C_DOCK_HOV : C_GLASS);
        draw_text("<- BACK", files_win_x + 18, content_y + 6, C_ACCENT, 1);
        content_y += 28;
    }

    struct vfs_entry entries[MAX_LIST_ENTRIES];
    int count;
    if (files_current_device[0] == 0)
        count = vfs_list_mounts(entries, MAX_LIST_ENTRIES);
    else
        count = vfs_list_device(files_current_device, entries, MAX_LIST_ENTRIES);

    if (count <= 0) {
        draw_text("EMPTY OR NO DISK", files_win_x + 20, content_y + 10, C_WIN_TEXT_DIM, 1);
    } else {
        for (int i = 0; i < count; i++) {
            int64_t row_y = content_y + i * 24;
            int32_t mx = mouse_get_x(), my = mouse_get_y();
            int hov = point_in(mx, my, files_win_x + 8, row_y, FILES_WIN_W - 16, 22);
            if (hov) fill_rect(files_win_x + 8, row_y, FILES_WIN_W - 16, 22, C_DOCK_HOV);

            if (entries[i].is_dir) {
                draw_folder_icon(files_win_x + 14, row_y + 3, 16);
            } else {
                fill_rect(files_win_x + 16, row_y + 3, 12, 16, C_WIN_TEXT_DIM);
                fill_rect(files_win_x + 18, row_y + 5, 8, 12, C_WIN_BG);
            }
            draw_text(entries[i].name, files_win_x + 36, row_y + 7, C_WIN_TEXT, 1);
            if (!entries[i].is_dir)
                draw_text("FILE", files_win_x + FILES_WIN_W - 60, row_y + 7, C_WIN_TEXT_DIM, 1);
        }
    }
}

/* ===== Перетаскивание окон ===== */
static void handle_window_drag(void) {
    if (!win_open) return;
    int32_t mx = mouse_get_x(), my = mouse_get_y();
    int left = mouse_left_pressed();

    if (left && !win_dragging) {
        int btn_w = 46;
        int base_x = win_x + BORDER_W + (WIN_W - BORDER_W*2) - btn_w * 3;
        if (point_in(mx, my, base_x + btn_w*2, win_y + BORDER_W, btn_w, TITLEBAR_H)) {
            win_open = 0; return;
        }
        if (point_in(mx, my, win_x, win_y, WIN_W - btn_w*3, TITLEBAR_H + BORDER_W)) {
            bring_to_front(0);
            win_dragging = 1;
            drag_ox = mx - win_x;
            drag_oy = my - win_y;
        } else if (point_in(mx, my, win_x, win_y, WIN_W, WIN_H)) {
            bring_to_front(0); // клик где угодно по окну — тоже поднимаем наверх
        }
    }
    if (win_dragging) {
        if (!left) { win_dragging = 0; }
        else {
            win_x = mx - drag_ox;
            win_y = my - drag_oy;
            if (win_x < 0) win_x = 0;
            if (win_y < 0) win_y = 0;
            if (win_x + WIN_W > (int64_t)fb_w) win_x = fb_w - WIN_W;
            if (win_y + WIN_H > (int64_t)(fb_h - DOCK_H - DOCK_Y_OFFSET)) win_y = fb_h - DOCK_H - DOCK_Y_OFFSET - WIN_H;
        }
    }
}

static void handle_files_window_drag(void) {
    if (!files_win_open) return;
    int32_t mx = mouse_get_x(), my = mouse_get_y();
    int left = mouse_left_pressed();

    if (left && !files_win_dragging) {
        int btn_w = 46;
        int base_x = files_win_x + BORDER_W + (FILES_WIN_W - BORDER_W*2) - btn_w * 3;
        if (point_in(mx, my, base_x + btn_w*2, files_win_y + BORDER_W, btn_w, TITLEBAR_H)) {
            files_win_open = 0; return;
        }
        if (point_in(mx, my, files_win_x, files_win_y, FILES_WIN_W - btn_w*3, TITLEBAR_H + BORDER_W)) {
            bring_to_front(1);
            files_win_dragging = 1;
            files_drag_ox = mx - files_win_x;
            files_drag_oy = my - files_win_y;
            return;
        }
        if (point_in(mx, my, files_win_x, files_win_y, FILES_WIN_W, FILES_WIN_H)) {
            bring_to_front(1);
        }
        int64_t content_y = files_win_y + BORDER_W + TITLEBAR_H + 8;
        if (files_current_device[0] != 0) {
            if (point_in(mx, my, files_win_x + 12, content_y, 80, 20)) {
                files_current_device[0] = 0; return;
            }
            content_y += 28;
        }
        struct vfs_entry entries[MAX_LIST_ENTRIES];
        int count = (files_current_device[0] == 0)
        ? vfs_list_mounts(entries, MAX_LIST_ENTRIES)
        : vfs_list_device(files_current_device, entries, MAX_LIST_ENTRIES);
        for (int i = 0; i < count; i++) {
            int64_t row_y = content_y + i * 24;
            if (point_in(mx, my, files_win_x + 8, row_y, FILES_WIN_W - 16, 22)) {
                if (files_current_device[0] == 0 && entries[i].is_dir) {
                    int j = 0;
                    while (entries[i].name[j]) { files_current_device[j] = entries[i].name[j]; j++; }
                    files_current_device[j] = 0;
                }
                return;
            }
        }
    }
    if (files_win_dragging) {
        if (!left) { files_win_dragging = 0; }
        else {
            files_win_x = mx - files_drag_ox;
            files_win_y = my - files_drag_oy;
            if (files_win_x < 0) files_win_x = 0;
            if (files_win_y < 0) files_win_y = 0;
            if (files_win_x + FILES_WIN_W > (int64_t)fb_w) files_win_x = fb_w - FILES_WIN_W;
            if (files_win_y + FILES_WIN_H > (int64_t)(fb_h - DOCK_H - DOCK_Y_OFFSET)) files_win_y = fb_h - DOCK_H - DOCK_Y_OFFSET - FILES_WIN_H;
        }
    }
}

/* ===== Клики ===== */
static void handle_click(void) {
    int left = mouse_left_pressed();
    if (left && !last_left) {
        int32_t mx = mouse_get_x(), my = mouse_get_y();
        
        /* Клик по меню бару (Apple logo) */
        if (point_in(mx, my, 8, 4, 24, MENU_BAR_H - 8)) {
            menu_open = !menu_open;
        } else if (menu_open) {
            int64_t menu_y = MENU_BAR_H + 4;
            if (point_in(mx, my, 8, menu_y, MENU_W, MENU_ITEMS * MENU_ITEM_H)) {
                int idx = (my - menu_y) / MENU_ITEM_H;
                if (idx == 0) { win_open = 1; bring_to_front(0); }
                if (idx == 1) { files_win_open = 1; bring_to_front(1); }
                if (idx == MENU_ITEMS - 1) { menu_open = 0; gui_should_exit = 1; }
                menu_open = 0;
            } else {
                menu_open = 0;
            }
        }
        
        /* Клик по dock иконкам */
        int dock_w = dock_count * (DOCK_ICON_SZ + DOCK_PADDING) + DOCK_PADDING * 2;
        int dock_x = (fb_w - dock_w) / 2;
        int dock_y = fb_h - DOCK_H - DOCK_Y_OFFSET;
        
        for (int i = 0; i < dock_count; i++) {
            int icon_x = dock_x + DOCK_PADDING + i * (DOCK_ICON_SZ + DOCK_PADDING);
            int icon_y = dock_y + (DOCK_H - DOCK_ICON_SZ) / 2;
            if (point_in(mx, my, icon_x, icon_y, DOCK_ICON_SZ, DOCK_ICON_SZ)) {
                if (i == 1) { win_open = 1; bring_to_front(0); }
                if (i == 2) { files_win_open = 1; bring_to_front(1); }
            }
        }
    }
    last_left = left;
}

/* ===== Иконки на рабочем столе (минималистичные) ===== */
static void draw_desktop_icons(void) {
    int32_t mx = mouse_get_x(), my = mouse_get_y();
    
    /* Иконка About */
    int hov1 = point_in(mx, my, 30, 60, 70, 80);
    if (hov1) fill_rect(30, 60, 70, 80, C_DOCK_HOV);
    draw_info_icon(48, 70, 32);
    draw_text("About", 42, 112, C_WIN_TEXT, 1);

    /* Иконка Files */
    int hov2 = point_in(mx, my, 30, 150, 70, 80);
    if (hov2) fill_rect(30, 150, 70, 80, C_DOCK_HOV);
    draw_folder_icon(48, 160, 32);
    draw_text("Files", 44, 202, C_WIN_TEXT, 1);
}

static void handle_desktop_icons(void) {
    int left = mouse_left_pressed();
    if (left && !last_left) {
        int32_t mx = mouse_get_x(), my = mouse_get_y();
        if (point_in(mx, my, 20, 20, 64, 70)) { win_open = 1; bring_to_front(0); }
        if (point_in(mx, my, 20, 100, 64, 70)) { files_win_open = 1; bring_to_front(1); }
    }
}

/* ===== Главный цикл ===== */
void desktop_run(void *framebuffer, uint64_t w, uint64_t h, uint64_t pitch) {
    fb = (uint32_t*)framebuffer;
    fb_w = w; fb_h = h; fb_pitch_px = pitch / 4;
    mouse_set_screen_bounds(w, h);
    gui_should_exit = 0;
    menu_open = 0;
    win_open = 0;
    files_win_open = 0;

    kprintf("[gui] macOS+W11 hybrid desktop %dx%d\n", (int)w, (int)h);

    while (!gui_should_exit) {
        /* Градиентный фон рабочего стола */
        for (int64_t y = 0; y < fb_h - DOCK_H - DOCK_Y_OFFSET; y++) {
            uint32_t c = C_DESKTOP;
            if (y < fb_h / 2) {
                c = C_DESKTOP_GRAD;
            }
            fill_rect(0, y, fb_w, 1, c);
        }

        draw_menu_bar();
        draw_desktop_icons();
        
        /* z-order: нижнее окно рисуем первым, верхнее — последним (поверх) */
        if (top_window == 1) {
            draw_about_window();
            draw_files_window();
        } else {
            draw_files_window();
            draw_about_window();
        }
        
        draw_dock();
        draw_cursor();

        handle_desktop_icons();
        /* z-order: верхнее окно первым получает клик */
        if (top_window == 1) {
            handle_files_window_drag();
            handle_window_drag();
        } else {
            handle_window_drag();
            handle_files_window_drag();
        }
        handle_click();
        usb_mouse_poll();

        if (kbd_has_char()) {
            char c = kbd_getchar();
            if (c == 27) gui_should_exit = 1;
        }

        for (volatile int i = 0; i < 200000; i++);
    }
    kprintf("[gui] desktop exited\n");
}
