// display_driver.c
#include "display_driver.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h" 

static const char *TAG = "display_driver";

// ---- Chan TFT ST7735 (giu nguyen tu main.cpp goc, da xac nhan dung) ----
#define TFT_CS   1
#define TFT_DC   2
#define TFT_RST  42
#define TFT_MOSI 40
#define TFT_SCLK 41
#define LCD_HOST SPI2_HOST

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static SemaphoreHandle_t s_trans_done_sem = NULL;

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_io_event_data_t *edata,
                                           void *user_ctx)
{
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done_sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

esp_err_t display_driver_init(void)
{
    spi_bus_config_t buscfg = {0};
    buscfg.sclk_io_num = TFT_SCLK;
    buscfg.mosi_io_num = TFT_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = DISPLAY_DRAW_W * DISPLAY_DRAW_H * 2 + 8;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {0};
    io_config.cs_gpio_num = TFT_CS;
    io_config.dc_gpio_num = TFT_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 10 * 1000 * 1000;
    io_config.trans_queue_depth = 50;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    s_io_handle = io_handle;
    s_trans_done_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, NULL));

    esp_lcd_panel_dev_config_t panel_config = {0};
    panel_config.reset_gpio_num = TFT_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "Man hinh TFT (Node A) init OK");
    return ESP_OK;
}

void display_driver_draw(const uint16_t *buf, int w, int h, int y_offset)
{
    if (!s_panel) return;
        static int log_counter = 0;
    if (++log_counter % 30 == 0) {  // log moi ~30 frame de khoi spam
        ESP_LOGW(TAG, "Free DMA-capable: %d bytes | Free internal: %d bytes",
                 heap_caps_get_free_size(MALLOC_CAP_DMA),
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, y_offset, w, y_offset + h, buf);
    xSemaphoreTake(s_trans_done_sem, pdMS_TO_TICKS(200));
}
