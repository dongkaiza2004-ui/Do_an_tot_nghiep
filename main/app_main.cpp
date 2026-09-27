// app_main.cpp

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "app_state.h"
#include "cam_driver.h"
#include "display_driver.h"
#include "overlay_render.h"
#include "light_ctrl.h"
#include "face_engine.h"
#include "comm_uart.h"
#include "comm_espnow.h"
#include "uart_protocol.h"


//==================================================
// Logging
//==================================================

static const char *TAG = "app_main";

//==================================================
// UART Configuration
//==================================================

#define UART_PORT  UART_NUM_1
#define UART_TX_PIN  21
#define UART_RX_PIN  47
#define UART_BAUD  115200

//==================================================
// ESP-NOW Configuration
//==================================================

static const uint8_t NODE_B_MAC[6] = {
    0xE0, 0x72, 0xA1, 0xD7, 0x1B, 0xCC
};

//==================================================
// Initialization Synchronization
//==================================================

static SemaphoreHandle_t s_wifi_init_done;
static SemaphoreHandle_t s_hw_init_done;

//==================================================
// SPIFFS
//==================================================

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
    }
    else {
        ESP_LOGI(TAG, "SPIFFS mount OK");
    }
}

//==================================================
// Face Event -> UART Report
//==================================================

static void uart_report_face_event(face_event_type_t type, uint32_t event_id, 
                                uint16_t user_id, float similarity)
{
    switch (type) {

        case FACE_EVENT_LOGIN_SUCCESS: {
            payload_face_matched_t p = {
                .event_id = event_id,
                .user_id = user_id,
                .similarity = similarity
            };
            comm_uart_send_response(RSP_FACE_MATCHED,&p,sizeof(p));
            break;
        }

        case FACE_EVENT_STRANGER: {
            payload_face_event_id_t p = {
                .event_id = event_id
            };
            comm_uart_send_response(RSP_FACE_UNKNOWN, &p, sizeof(p));
            break;
        }

        case FACE_EVENT_LIVENESS_FAIL: {
            payload_face_event_id_t p = {
                .event_id = event_id
            };
            comm_uart_send_response( RSP_LIVENESS_FAIL, &p, sizeof(p));
            break;
        }
        default:
            break;
    }
}

//==================================================
// Hardware Initialization - Core 1
//==================================================

static void core1_hw_init_task(void *arg)
{
    ESP_ERROR_CHECK(cam_driver_init());
    ESP_ERROR_CHECK(display_driver_init());

    xSemaphoreGive(s_hw_init_done);
    vTaskDelete(NULL);
}

//==================================================
// ESP-NOW Initialization - Core 0
//==================================================

static void core0_wifi_init_task(void *arg)
{
    ESP_LOGI(TAG, "Dang khoi tao ESP-NOW tren Core 0...");
    ESP_ERROR_CHECK(comm_espnow_init(NODE_B_MAC));

    comm_espnow_start();
    xSemaphoreGive(s_wifi_init_done);

    vTaskDelete(NULL);
}

//==================================================
// Main Processing Pipeline
//==================================================

static void main_pipeline_task(void *arg)
{
    QueueHandle_t frame_queue = *(QueueHandle_t *)arg;

    int frame_count = 0;
    int64_t last_fps_ts = esp_timer_get_time();
    float current_fps = 0;

    while (1) {
        camera_fb_t *fb = NULL;

        if (xQueueReceive(frame_queue, &fb, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }
        //==================================================
        // Camera Byte Order
        //==================================================

        size_t pixel_count = (size_t)fb->width * fb->height;
        uint16_t *buf = (uint16_t *)fb->buf;

        for (size_t i = 0; i < pixel_count; i++) {
            buf[i] = (buf[i] >> 8) | (buf[i] << 8);
        }
        //==================================================
        // Face Detection & Recognition
        //==================================================

        face_result_t results[FACE_ENGINE_MAX_RESULTS];
        int n = face_engine_process_frame( buf, fb->width, fb->height, results);

        //==================================================
        // Prepare Overlay Data
        //==================================================

        overlay_face_t ov[FACE_ENGINE_MAX_RESULTS];

        for (int i = 0; i < n; i++) {
            ov[i].x1 = results[i].x1;
            ov[i].y1 = results[i].y1;
            ov[i].x2 = results[i].x2;
            ov[i].y2 = results[i].y2;
            ov[i].matched = results[i].matched;

            snprintf( ov[i].label, sizeof(ov[i].label), "%s", results[i].name);
        }

        //==================================================
        // Render Overlay
        //==================================================

        enroll_state_t est = app_state_get_enroll_state();
        overlay_render_draw_live( buf, fb->width, fb->height, ov, n, 
                                    app_state_get_show_fps(), current_fps, 
                                    est.in_progress, "DUA MAT VAO KHUNG...",
                                    app_state_get_liveness_prompt(), 
                                    "Xoay dau trai/phai de kiem tra song thuc");
        //==================================================
        // Display Byte Order
        //==================================================

        for (size_t i = 0; i < pixel_count; i++) {
            buf[i] = (buf[i] >> 8) | (buf[i] << 8);
        }
        display_driver_draw( buf, fb->width, fb->height, DISPLAY_Y_OFFSET);
        esp_camera_fb_return(fb);

        //==================================================
        // FPS
        //==================================================
        frame_count++;
        int64_t now = esp_timer_get_time();
        if (now - last_fps_ts >= 1000000) {
            current_fps = frame_count;
            frame_count = 0;
            last_fps_ts = now;
            if (app_state_get_show_fps()) {
                ESP_LOGI("app_main", "Chuan bi gui FPS report: %.1f", current_fps);
                payload_fps_report_t p = { .fps = current_fps };
                comm_uart_send_response(RSP_FPS_REPORT, &p, sizeof(p));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

//==================================================
// Application Initialization
//==================================================

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    app_state_init();
    init_spiffs();

    //==================================================
    // Hardware Initialization
    //==================================================

    s_hw_init_done = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore( core1_hw_init_task, "hw_init", 4096, NULL, 5, NULL, 1);
    xSemaphoreTake(s_hw_init_done, portMAX_DELAY);
    //==================================================
    // Application Components
    //==================================================

    ESP_ERROR_CHECK(face_engine_init());
    ESP_ERROR_CHECK(light_ctrl_init());

    //==================================================
    // ESP-NOW Initialization
    //==================================================

    s_wifi_init_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore( core0_wifi_init_task, "wifi_init_c0", 4096, NULL, 5, NULL, 0);
    xSemaphoreTake(s_wifi_init_done, portMAX_DELAY);

    //==================================================
    // UART Initialization
    //==================================================
    ESP_ERROR_CHECK(comm_uart_init(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_BAUD));

    //==================================================
    // Component Connections
    //==================================================

    face_engine_set_event_queue(comm_espnow_get_event_queue());
    face_engine_set_uart_report_cb(uart_report_face_event);
    //==================================================
    // Camera Frame Queue
    //==================================================

    static QueueHandle_t frame_queue;
    frame_queue = xQueueCreate(1, sizeof(camera_fb_t *));
    cam_driver_start(frame_queue);
    light_ctrl_start();
    comm_uart_start();

    //==================================================
    // Main Pipeline
    //==================================================
    xTaskCreatePinnedToCore( main_pipeline_task, "main_pipeline_task", 8192, &frame_queue, 3, NULL, 1);
    ESP_LOGI(TAG, "Node A da san sang.");
}