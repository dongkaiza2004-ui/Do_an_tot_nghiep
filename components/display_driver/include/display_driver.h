// display_driver.h

#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// Display Configuration
//==================================================

#define DISPLAY_CAM_W     160
#define DISPLAY_CAM_H     120
#define DISPLAY_DRAW_W    160
#define DISPLAY_DRAW_H    128
#define DISPLAY_Y_OFFSET  ((DISPLAY_DRAW_H - DISPLAY_CAM_H) / 2)

//==================================================
// Initialization
//==================================================

esp_err_t display_driver_init(void);

//==================================================
// Display
//==================================================
// Vẽ buffer RGB565 kích thước w x h lên màn hình.
void display_driver_draw(const uint16_t *buf, int w, int h, int y_offset);


#ifdef __cplusplus
}
#endif