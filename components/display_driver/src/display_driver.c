#include "display_driver.h"
#include "esp_lvgl_port.h"

lv_disp_t *disp_handle = NULL;

void display_lvgl_init(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle)
{
    // 1. Khởi tạo tác vụ nền cho LVGL
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 0;
    lvgl_port_init(&lvgl_cfg);

    // 2. Cấu hình hiển thị theo chuẩn LVGL v8
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = 128 * 160,    // Kích thước buffer (tính bằng số pixel)
        .double_buffer = false,
        .trans_size = 128 * 10,
        .hres = 128,                 // Độ phân giải chiều rộng
        .vres = 160,                 // Độ phân giải chiều cao
        .monochrome = false,         // Màn hình màu ST7735 (không phải đơn sắc)
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,        // Bật DMA giúp truyền dữ liệu qua SPI nhanh hơn
            .buff_spiram = true
        }
    };

    disp_handle = lvgl_port_add_disp(&disp_cfg);
}