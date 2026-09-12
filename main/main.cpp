#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "driver/spi_master.h"

#include "display_driver.h"   // display_lvgl_init() - GIỮ NGUYÊN, không sửa
#include "comm_uart.h"        // comm_uart_init() -> QueueHandle_t nav events
#include "ui_nav.h"           // ui_nav_task()
#include "ui.h"               // ui_init() - SquareLine export

static const char *TAG = "APP_MAIN";

// ---- TFT ST7735 pins ---- (giữ nguyên y hệt file bạn gửi)
#define TFT_CS   1
#define TFT_DC   2
#define TFT_RST  42
#define TFT_MOSI 40
#define TFT_SCLK 41
#define LCD_HOST SPI2_HOST
#define LCD_DRAW_W 128
#define LCD_DRAW_H 160

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

// init_display() giữ NGUYÊN 100% như file bạn gửi - không đổi 1 dòng nào
static void init_display(void)
{
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = TFT_SCLK;
    buscfg.mosi_io_num = TFT_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_DRAW_W * LCD_DRAW_H * 2 + 8;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = TFT_CS;
    io_config.dc_gpio_num = TFT_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 20 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = TFT_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Man hinh TFT ST7735 init OK");
}

extern "C" void app_main(void)
{
    // 1. Phần cứng màn hình - không đổi
    init_display();
    display_lvgl_init(io_handle, panel_handle);

    // 2. Giao diện LVGL từ SquareLine - không đổi
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Khong khoi tao duoc UI: LVGL mutex timeout");
        return;
    }
    ui_init();
    ui_nav_init(); 
    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL UI init thanh cong");

    // 3. Input: comm_uart đọc UART -> đẩy nav_event_t vào queue
    QueueHandle_t nav_queue = comm_uart_init();

    // 4. ui_nav: nhận nav_event_t từ queue, tự lo focus/slider/dropdown/switch
    xTaskCreate(ui_nav_task, "ui_nav_task", 4096, (void *)nav_queue, 3, NULL);

    // LVGL port đã có task riêng lo timer/redraw, app_main chỉ cần rảnh tay
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}