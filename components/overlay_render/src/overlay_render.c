// overlay_render.c
//
// GHI CHU QUAN TRONG VE FONT CHU:
// De ve so (FPS) mot cach CHINH XAC va de kiem chung, file nay tu dinh nghia
// 1 font so hoc dang dot-matrix 3x5 (chi co 0-9, khoang trang, '.', ':', '%').
// Day la font TOI THIEU chi du ve con so (FPS, % similarity...).
//
// CHUA HO TRO chu cai A-Z (dung de ve ten nguoi dung / "Unknown" / dong huong dan).
// Viet tay 1 bo font 5x7 day du 26 chu cai tu tri nho de dam bao tung diem anh
// dung 100% la rui ro cao (de sai lech, kho kiem tra bang mat thuong khi doc code).
// Khuyen nghi: hoac (1) nho minh sinh tiep bang font day du sau khi ban co the
// test truc tiep tren phan cung de chinh cho dung, hoac (2) dung 1 thu vien font
// co san (vd u8g2 chi phan font, hoac chuyen 1 file .ttf/.otf qua tool
// "font2bitmap" offline roi nhung bang du lieu vao day). Hien tai, ky tu KHONG
// nam trong font so (vi du chu cai) se duoc ve tam thoi bang 1 o vuong nho de
// nguoi dung biet "co chu o day" thay vi im lang bo qua.
#include "overlay_render.h"
#include <string.h>
#include <stdio.h>

// ---- Font so hoc 3x5 (3 cot x 5 hang), moi ky tu = 3 byte (1 byte/cot, bit0=hang tren cung) ----
typedef struct { char c; uint8_t col[3]; } glyph3x5_t;

static const glyph3x5_t FONT_3X5[] = {
    {' ', {0x00, 0x00, 0x00}},
    {'.', {0x00, 0x10, 0x00}},
    {':', {0x00, 0x0A, 0x00}},
    {'%', {0x19, 0x04, 0x13}}, // xap xi, chi mang tinh minh hoa
    {'0', {0x1F, 0x11, 0x1F}},
    {'1', {0x12, 0x1F, 0x10}},
    {'2', {0x1D, 0x15, 0x17}},
    {'3', {0x11, 0x15, 0x1F}},
    {'4', {0x07, 0x04, 0x1F}},
    {'5', {0x17, 0x15, 0x1D}},
    {'6', {0x1F, 0x15, 0x1D}},
    {'7', {0x01, 0x00, 0x1F}},
    {'8', {0x1F, 0x15, 0x1F}},
    {'9', {0x17, 0x15, 0x1F}},
};
#define FONT_3X5_COUNT (sizeof(FONT_3X5) / sizeof(FONT_3X5[0]))

static void put_pixel(uint16_t *buf, int width, int height, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    buf[y * width + x] = color;
}

static void draw_rect(uint16_t *buf, int width, int height,
                       int x1, int y1, int x2, int y2, uint16_t color)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= width)  x2 = width - 1;
    if (y2 >= height) y2 = height - 1;
    for (int x = x1; x <= x2; x++) {
        put_pixel(buf, width, height, x, y1, color);
        put_pixel(buf, width, height, x, y2, color);
    }
    for (int y = y1; y <= y2; y++) {
        put_pixel(buf, width, height, x1, y, color);
        put_pixel(buf, width, height, x2, y, color);
    }
}

// Ve 1 ky tu tai (x,y) goc tren-trai, scale nguyen lan (scale=1 -> 3x5 px that).
// Ky tu khong co trong FONT_3X5 -> ve 1 o vuong nho thay the (xem ghi chu dau file).
static int draw_char(uint16_t *buf, int width, int height, int x, int y,
                      char c, uint16_t color, int scale)
{
    const glyph3x5_t *g = NULL;
    for (size_t i = 0; i < FONT_3X5_COUNT; i++) {
        if (FONT_3X5[i].c == c) { g = &FONT_3X5[i]; break; }
    }
    if (!g) {
        // Placeholder: o vuong nho bao "co chu nhung font chua ho tro"
        draw_rect(buf, width, height, x, y, x + 2 * scale, y + 4 * scale, color);
        return 4 * scale; // be rong da chiem (3 cot + 1 khoang cach) * scale
    }
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 5; row++) {
            if (g->col[col] & (1 << row)) {
                for (int sx = 0; sx < scale; sx++)
                    for (int sy = 0; sy < scale; sy++)
                        put_pixel(buf, width, height,
                                  x + col * scale + sx, y + row * scale + sy, color);
            }
        }
    }
    return 4 * scale; // 3 cot + 1 cot khoang cach, nhan scale
}

static void draw_text(uint16_t *buf, int width, int height, int x, int y,
                       const char *text, uint16_t color, int scale)
{
    int cursor_x = x;
    for (const char *p = text; *p; p++) {
        cursor_x += draw_char(buf, width, height, cursor_x, y, *p, color, scale);
    }
}

// Mau RGB565 dung san
#define COLOR_RED    0xF800
#define COLOR_GREEN  0x07E0
#define COLOR_YELLOW 0xFFE0
#define COLOR_WHITE  0xFFFF

void overlay_render_draw_live(uint16_t *buf, int width, int height,
                               const overlay_face_t *faces, int face_count,
                               bool show_fps, float current_fps,
                               bool enrolling, const char *enroll_hint)
{
    if (!buf) return;

    for (int i = 0; i < face_count && i < OVERLAY_MAX_FACES; i++) {
        uint16_t color = faces[i].matched ? COLOR_GREEN : COLOR_RED;
        draw_rect(buf, width, height, faces[i].x1, faces[i].y1, faces[i].x2, faces[i].y2, color);
        // Ten/nhan ve ngay tren box (chu cai ngoai font so se hien o vuong placeholder)
        draw_text(buf, width, height, faces[i].x1, faces[i].y1 - 6, faces[i].label, color, 1);
    }

    if (show_fps) {
        char fps_str[16];
        snprintf(fps_str, sizeof(fps_str), "%d", (int)current_fps);
        draw_text(buf, width, height, 2, 2, fps_str, COLOR_YELLOW, 1);
    }

    if (enrolling && enroll_hint) {
        draw_text(buf, width, height, 2, height - 8, enroll_hint, COLOR_WHITE, 1);
    }
}
