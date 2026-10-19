// overlay_render.c

#include "overlay_render.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

//==================================================
// Font 5x7
//==================================================

typedef struct {
    char c;
    uint8_t col[5];
} glyph5x7_t;

static const glyph5x7_t FONT_5X7[] = {
    {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x41}},
    {'D', {0x7F, 0x41, 0x41, 0x41, 0x3E}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x79}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x41, 0x41, 0x7F, 0x41, 0x41}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x04, 0x02, 0x7F}},
    {'N', {0x7F, 0x02, 0x04, 0x08, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

#define FONT_5X7_COUNT  (sizeof(FONT_5X7) / sizeof(FONT_5X7[0]))

//==================================================
// Font 3x5
//==================================================

typedef struct {
    char c;
    uint8_t col[3];
} glyph3x5_t;

static const glyph3x5_t FONT_3X5[] = {
    {' ', {0x00, 0x00, 0x00}},
    {'.', {0x00, 0x10, 0x00}},
    {':', {0x00, 0x0A, 0x00}},
    {'%', {0x19, 0x04, 0x13}},
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

#define FONT_3X5_COUNT  (sizeof(FONT_3X5) / sizeof(FONT_3X5[0]))

//==================================================
// Pixel & Rectangle
//==================================================

static void put_pixel( uint16_t *buf, int width, int height, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    buf[y * width + x] = color;
}

static void draw_rect( uint16_t *buf, int width, int height, int x1, int y1, int x2, int y2, uint16_t color)
{
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= width) {
        x2 = width - 1;
    }
    if (y2 >= height) {
        y2 = height - 1;
    }
    for (int x = x1; x <= x2; x++) {
        put_pixel(buf, width, height, x, y1, color);
        put_pixel(buf, width, height, x, y2, color);
    }
    for (int y = y1; y <= y2; y++) {
        put_pixel(buf, width, height, x1, y, color);
        put_pixel(buf, width, height, x2, y, color);
    }
}

//==================================================
// Text Rendering
//==================================================

static int draw_char(uint16_t *buf, int width, int height, int x, int y, char c, uint16_t color, int scale){

    char cu = (char)toupper((unsigned char)c);
    const glyph5x7_t *g5 = NULL;
    for (size_t i = 0; i < FONT_5X7_COUNT; i++) {
        if (FONT_5X7[i].c == cu) {
            g5 = &FONT_5X7[i];
            break;
        }
    }

    if (g5) {
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g5->col[col] & (1 << row)) {
                    for (int sx = 0; sx < scale; sx++) {
                        for (int sy = 0; sy < scale; sy++) {
                            put_pixel( buf, width, height, x + col * scale + sx, y + row * scale + sy, color
                            );
                        }
                    }
                }
            }
        }

        return 6 * scale;
    }

    const glyph3x5_t *g3 = NULL;

    for (size_t i = 0; i < FONT_3X5_COUNT; i++) {
        if (FONT_3X5[i].c == c) {
            g3 = &FONT_3X5[i];
            break;
        }
    }

    if (g3) {
        for (int col = 0; col < 3; col++) {
            for (int row = 0; row < 5; row++) {
                if (g3->col[col] & (1 << row)) {
                    for (int sx = 0; sx < scale; sx++) {
                        for (int sy = 0; sy < scale; sy++) {
                            put_pixel( buf, width, height, x + col * scale + sx, y + row * scale + sy, color);
                        }
                    }
                }
            }
        }

        return 4 * scale;
    }
    // Placeholder cho ky tu chua duoc ho tro.
    draw_rect(buf, width, height, x, y, x + 2 * scale, y + 4 * scale, color);

    return 4 * scale;
}

static void draw_text(uint16_t *buf, int width, int height, int x, int y, const char *text, uint16_t color, int scale)
{
    int cursor_x = x;
    for (const char *p = text; *p; p++) {
        cursor_x += draw_char( buf, width, height, cursor_x, y, *p, color, scale);
    }
}

//==================================================
// RGB565 Color
//==================================================

#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_WHITE   0xFFFF

#define TEXT_X_MIN   18    
#define TEXT_X_MAX   142   
#define TEXT_LINE_H  9     

static int char_advance(char c, int scale)
{
    return (isalpha((unsigned char)c) ? 6 : 4) * scale;
}
static void draw_text_wrapped(uint16_t *buf, int width, int height, int x_min, int x_max,
                              int y, const char *text, uint16_t color, int scale)
{
    int cx = x_min;
    int cy = y;
    const char *p = text;

    while (*p) {
        if (*p == ' ') {
            cx += char_advance(' ', scale);
            p++;
            continue;
        }

        const char *word_end = p;
        int word_w = 0;
        while (*word_end && *word_end != ' ') {
            word_w += char_advance(*word_end, scale);
            word_end++;
        }

        if (cx > x_min && cx + word_w > x_max) {
            cx = x_min;
            cy += TEXT_LINE_H * scale;
        }

        for (; p < word_end; p++) {
            cx += draw_char(buf, width, height, cx, cy, *p, color, scale);
        }
    }
}
//==================================================
// Overlay Rendering
//==================================================

void overlay_render_draw_live( uint16_t *buf, int width, int height, const overlay_face_t *faces, 
    int face_count, bool show_fps, float current_fps, bool enrolling, const char *enroll_hint,
    bool liveness_check, const char *liveness_hint)
{
    if (!buf) {
        return;
    }

    //==================================================
    // Face Overlay
    //==================================================

    for (int i = 0; i < face_count && i < OVERLAY_MAX_FACES; i++) {
        uint16_t color = faces[i].matched ? COLOR_GREEN : COLOR_RED;

        draw_rect( buf, width, height, faces[i].x1, faces[i].y1, faces[i].x2, faces[i].y2, color);
        draw_text( buf, width, height, faces[i].x1, faces[i].y1 - 6, faces[i].label, color, 1);
    }

    //==================================================
    // FPS
    //==================================================

    if (show_fps) {
        char fps_str[16];
        snprintf(fps_str, sizeof(fps_str), "%d", (int)current_fps);
        draw_text(buf, width, height, TEXT_X_MIN, 2, fps_str, COLOR_YELLOW, 1);
    }

    //==================================================
    // Enrollment Hint
    //==================================================

    if (enrolling && enroll_hint) {
        draw_text(buf, width, height, TEXT_X_MIN, height - 8, enroll_hint, COLOR_WHITE, 1);
    }

    if (liveness_check && liveness_hint) {
        draw_text_wrapped(buf, width, height, TEXT_X_MIN, TEXT_X_MAX, 10, liveness_hint, COLOR_YELLOW, 1);
    }
}