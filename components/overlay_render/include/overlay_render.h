// overlay_render.h
// Ve box/ten/FPS/huong dan enroll de len bitmap RGB565, dung cho man hinh LOCAL cua Node A.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OVERLAY_MAX_FACES 4

typedef struct {
    int x1, y1, x2, y2;      // toa do box (theo he toa do anh)
    bool matched;             // da nhan ra nguoi trong CSDL hay chua
    char label[24];           // ten neu matched, hoac "Unknown"
} overlay_face_t;

// Ve toan bo overlay cho 1 frame: box moi khuon mat + nhan, FPS (neu show_fps=true),
// va dong huong dan enroll (neu enrolling=true). Ve truc tiep len buf (in-place).
void overlay_render_draw_live(uint16_t *buf, int width, int height,
                               const overlay_face_t *faces, int face_count,
                               bool show_fps, float current_fps,
                               bool enrolling, const char *enroll_hint);

#ifdef __cplusplus
}
#endif
