#include <bnux/types.h>

extern const uint8_t *font_get_glyph(char c);

#define GLYPH_W 5
#define GLYPH_H 7
#define SCALE   2               // рисуем шрифт x2 крупнее, читаемее на больших экранах
#define CHAR_W  (GLYPH_W * SCALE + 2)
#define CHAR_H  (GLYPH_H * SCALE + 4)

#define COLOR_FG 0x00E0E0E0
#define COLOR_BG 0x00081018

static uint32_t *fb;
static uint64_t fb_width, fb_height, fb_pitch_px;
static uint64_t cols, rows;
static uint64_t cur_col = 0, cur_row = 0;

void console_init(void *framebuffer, uint64_t width, uint64_t height, uint64_t pitch) {
    fb = (uint32_t*)framebuffer;
    fb_width = width;
    fb_height = height;
    fb_pitch_px = pitch / 4;
    cols = fb_width / CHAR_W;
    rows = fb_height / CHAR_H;
    cur_col = 0;
    cur_row = 0;

    for (uint64_t y = 0; y < fb_height; y++)
        for (uint64_t x = 0; x < fb_width; x++)
            fb[y * fb_pitch_px + x] = COLOR_BG;
}

static void draw_glyph_at(char c, uint64_t col, uint64_t row) {
    const uint8_t *g = font_get_glyph(c);
    uint64_t ox = col * CHAR_W + 1;
    uint64_t oy = row * CHAR_H + 2;

    for (uint64_t y = 0; y < GLYPH_H * SCALE; y++) {
        for (uint64_t x = 0; x < GLYPH_W * SCALE; x++) {
            int bit = 0;
            if (g) {
                uint8_t rowbits = g[y / SCALE];
                bit = (rowbits >> (GLYPH_W - 1 - (x / SCALE))) & 1;
            }
            fb[(oy + y) * fb_pitch_px + (ox + x)] = bit ? COLOR_FG : COLOR_BG;
        }
    }
}

static void scroll_up(void) {
    for (uint64_t y = CHAR_H; y < rows * CHAR_H; y++)
        for (uint64_t x = 0; x < cols * CHAR_W; x++)
            fb[(y - CHAR_H) * fb_pitch_px + x] = fb[y * fb_pitch_px + x];

    for (uint64_t y = (rows - 1) * CHAR_H; y < rows * CHAR_H; y++)
        for (uint64_t x = 0; x < cols * CHAR_W; x++)
            fb[y * fb_pitch_px + x] = COLOR_BG;
}

static void newline(void) {
    cur_col = 0;
    cur_row++;
    if (cur_row >= rows) {
        scroll_up();
        cur_row = rows - 1;
    }
}

void console_putchar(char c) {
    if (c == '\n') { newline(); return; }
    if (c == '\r') { cur_col = 0; return; }
    if (c == '\b') {
        if (cur_col > 0) { cur_col--; draw_glyph_at(' ', cur_col, cur_row); }
        return;
    }
    if (c == '\t') {
        for (int i = 0; i < 4; i++) console_putchar(' ');
        return;
    }

    draw_glyph_at(c, cur_col, cur_row);
    cur_col++;
    if (cur_col >= cols) newline();
}

void console_puts(const char *s) {
    while (*s) console_putchar(*s++);
}
