// overlay_render.h

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//==================================================
// Overlay Configuration
//==================================================

#define OVERLAY_MAX_FACES  4

//==================================================
// Face Overlay Data
//==================================================

typedef struct {
    int x1, y1, x2, y2;
    bool matched;
    char label[24];
} overlay_face_t;

//==================================================
// Overlay Rendering
//==================================================

// Vẽ box, tên, FPS và hướng dẫn Enrollment trực tiếp lên buffer RGB565.
void overlay_render_draw_live( uint16_t *buf, int width, int height, const overlay_face_t *faces,
    int face_count, bool show_fps, float current_fps, bool enrolling, const char *enroll_hint,
    bool liveness_check, const char *liveness_hint
);

#ifdef __cplusplus
}
#endif