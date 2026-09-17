// app_main.cpp
// CHI khoi tao + noi cac component lai voi nhau, KHONG chua logic nghiep vu.
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "app_state.h"
#include "cam_driver.h"
#include "display_driver.h"
#include "overlay_render.h"
#include "light_ctrl.h"
#include "face_engine.h"
#include "comm_uart.h"
#include "comm_espnow.h"

static const char *TAG = "app_main";
static SemaphoreHandle_t s_wifi_init_done;

// TODO: doi cho dung chan UART thuc te noi giua Node A va Node B
#define UART_PORT     UART_NUM_1
#define UART_TX_PIN   21
#define UART_RX_PIN   47
#define UART_BAUD     115200

// TODO: doi cho dung dia chi MAC that cua Node B (lay bang esp_wifi_get_mac tren Node B)
static const uint8_t NODE_B_MAC[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
static SemaphoreHandle_t s_hw_init_done;

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Loi mount SPIFFS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mount OK");
    }
}

static void core1_hw_init_task(void *arg)
{
    ESP_ERROR_CHECK(cam_driver_init());
    ESP_ERROR_CHECK(display_driver_init());
    xSemaphoreGive(s_hw_init_done);
    vTaskDelete(NULL);
}

static void core0_wifi_init_task(void *arg)
{
    ESP_LOGI(TAG, "Dang khoi tao ESP-NOW tren Core 0...");
    ESP_ERROR_CHECK(comm_espnow_init(NODE_B_MAC));
    comm_espnow_start();
    xSemaphoreGive(s_wifi_init_done);
    vTaskDelete(NULL); // Khởi tạo xong thì tự xóa task
}

// Task chinh: lay frame moi nhat tu cam_driver, chay face_engine, ve overlay, xuat TFT.
static void main_pipeline_task(void *arg)
{
    QueueHandle_t frame_queue = *(QueueHandle_t *)arg;

    int frame_count = 0;
    int64_t last_fps_ts = esp_timer_get_time();
    float current_fps = 0;

    while (1) {
        camera_fb_t *fb = NULL;
        if (xQueueReceive(frame_queue, &fb, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue; // chua co frame moi (co the dang tam dung streaming)
        }

        // Camera xuat byte-order can swap de hien thi/xu ly dung mau - giu
        // nguyen ky thuat swap tu main.cpp ban goc (swap truoc khi detect,
        // swap lai truoc khi ve TFT - vi driver TFT can thu tu byte khac voi
        // buffer camera/du lieu anh dua vao model AI).
        size_t pixel_count = (size_t)fb->width * fb->height;
        uint16_t *buf = (uint16_t *)fb->buf;
        for (size_t i = 0; i < pixel_count; i++) buf[i] = (buf[i] >> 8) | (buf[i] << 8);

        // vTaskDelay(pdMS_TO_TICKS(5));
        face_result_t results[FACE_ENGINE_MAX_RESULTS];
        int n = face_engine_process_frame(buf, fb->width, fb->height, results);
        // vTaskDelay(pdMS_TO_TICKS(5));

        overlay_face_t ov[FACE_ENGINE_MAX_RESULTS];
        for (int i = 0; i < n; i++) {
            ov[i].x1 = results[i].x1; ov[i].y1 = results[i].y1;
            ov[i].x2 = results[i].x2; ov[i].y2 = results[i].y2;
            ov[i].matched = results[i].matched;
            snprintf(ov[i].label, sizeof(ov[i].label), "%s", results[i].name);
        }

        enroll_state_t est = app_state_get_enroll_state();
        overlay_render_draw_live(buf, fb->width, fb->height, ov, n,
                                  app_state_get_show_fps(), current_fps,
                                  est.in_progress, "DUA MAT VAO KHUNG...");

        // Swap lai truoc khi dua ra TFT (xem ghi chu o tren)
        for (size_t i = 0; i < pixel_count; i++) buf[i] = (buf[i] >> 8) | (buf[i] << 8);

        display_driver_draw(buf, fb->width, fb->height, DISPLAY_Y_OFFSET);

        esp_camera_fb_return(fb);

        frame_count++;
        int64_t now = esp_timer_get_time();
        if (now - last_fps_ts >= 1000000) {
            current_fps = frame_count;
            frame_count = 0;
            last_fps_ts = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    app_state_init();
    init_spiffs();

    s_hw_init_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(core1_hw_init_task, "hw_init", 4096, NULL, 5, NULL, 1);
    xSemaphoreTake(s_hw_init_done, portMAX_DELAY);

    ESP_ERROR_CHECK(face_engine_init());
    ESP_ERROR_CHECK(light_ctrl_init());

    s_wifi_init_done = xSemaphoreCreateBinary();
    // xTaskCreatePinnedToCore(core0_wifi_init_task, "wifi_init_c0", 4096, NULL, 5, NULL, 0);
    // xSemaphoreTake(s_wifi_init_done, portMAX_DELAY);

    ESP_ERROR_CHECK(comm_uart_init(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_BAUD));

    // face_engine_set_event_queue(comm_espnow_get_event_queue());

    static QueueHandle_t frame_queue;
    frame_queue = xQueueCreate(1, sizeof(camera_fb_t *));

    cam_driver_start(frame_queue);
    light_ctrl_start();

    comm_uart_start();

    xTaskCreatePinnedToCore(main_pipeline_task, "main_pipeline_task", 8192, &frame_queue, 3, NULL, 1);
    ESP_LOGI(TAG, "Node A da san sang.");
}
