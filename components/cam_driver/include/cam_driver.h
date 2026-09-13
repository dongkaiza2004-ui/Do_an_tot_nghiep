#pragma once
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

void cam_driver_init(void);
camera_fb_t *cam_driver_get_frame(void);
void cam_driver_return_frame(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif