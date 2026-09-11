#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

void display_lvgl_init(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle);

#ifdef __cplusplus
}
#endif

#endif