// display_driver.h
// Driver muc thap cho TFT ST7735 128x160 gan truc tiep tren Node A.
// Chi ve bitmap RGB565 thang qua SPI, KHONG dung LVGL.
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_CAM_W      160
#define DISPLAY_CAM_H      120
#define DISPLAY_DRAW_W     160
#define DISPLAY_DRAW_H     128
#define DISPLAY_Y_OFFSET   ((DISPLAY_DRAW_H - DISPLAY_CAM_H) / 2)

esp_err_t display_driver_init(void);

// Ve 1 buffer RGB565 kich thuoc w x h len man hinh, bat dau tu (0, y_offset).
void display_driver_draw(const uint16_t *buf, int w, int h, int y_offset);

#ifdef __cplusplus
}
#endif
