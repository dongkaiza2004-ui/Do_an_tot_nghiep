// cam_driver.c
#include "cam_driver.h"
#include "app_state.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "cam_driver";

// ---- Camera pins (giu nguyen tu main.cpp goc, da xac nhan dung phan cung) ----
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y2_GPIO_NUM      11
#define Y3_GPIO_NUM       9
#define Y4_GPIO_NUM       8
#define Y5_GPIO_NUM      10
#define Y6_GPIO_NUM      12
#define Y7_GPIO_NUM      18
#define Y8_GPIO_NUM      17
#define Y9_GPIO_NUM      16
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

static QueueHandle_t s_out_queue = NULL;

esp_err_t cam_driver_init(void)
{
    camera_config_t config = {0};
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format  = PIXFORMAT_RGB565;
    config.frame_size    = FRAMESIZE_QQVGA;   // 160x120 thuc te
    config.fb_count      = 2;
    config.fb_location   = CAMERA_FB_IN_PSRAM;
    config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init that bai: 0x%x", err);
        return err;
    }

    cam_driver_apply_sensor_config(
        app_state_get_brightness(), app_state_get_contrast(),
        app_state_get_flip(), app_state_get_mirror());

    ESP_LOGI(TAG, "Camera init OK");
    return ESP_OK;
}

void cam_driver_apply_sensor_config(int8_t brightness, int8_t contrast, bool flip, bool mirror)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    s->set_brightness(s, brightness);
    s->set_contrast(s, contrast);
    s->set_vflip(s, flip ? 1 : 0);
    s->set_hmirror(s, mirror ? 1 : 0);
}

static void cam_capture_task(void *arg)
{
    while (1) {
        if (!app_state_get_streaming()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Chup anh that bai (fb == NULL)");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        // Queue length = 1 dung xQueueOverwrite: luon giu frame moi nhat,
        // frame cu chua kip xu ly se bi ghi de (khong phai xep hang cho).
        // CANH BAO: neu overwrite frame chua duoc tra (fb_return), fb do se
        // bi "lost" - ben tieu thu (face_engine) PHAI goi esp_camera_fb_return()
        // ngay sau khi lay ra khoi queue va xu ly xong, khong duoc giu lau.
        // Dung xQueueReceive (rut phan tu, khong phai Peek) de thao tac nay
        // "an toan": neu consumer da lay frame cu ra roi (queue rong luc nay),
        // xQueueReceive tra ve pdFALSE va ta KHONG dong esp_camera_fb_return
        // trung voi consumer. Neu consumer chua kip lay, ta rut no ra va tra
        // lai cho camera driver truoc khi nhet frame moi vao.
        camera_fb_t *old = NULL;
        if (xQueueReceive(s_out_queue, &old, 0) == pdTRUE) {
            esp_camera_fb_return(old);
        }
        xQueueSend(s_out_queue, &fb, 0);
    }
}

void cam_driver_start(QueueHandle_t out_queue)
{
    s_out_queue = out_queue;
    xTaskCreatePinnedToCore(cam_capture_task, "cam_capture_task", 4096, NULL, 6, NULL, 1);
}
