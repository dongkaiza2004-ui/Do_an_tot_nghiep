#include "face_engine.h"
#include "cam_driver.h"      // THEM
#include "ui.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "face_engine";


#define CAM_W   160
#define CAM_H   120
#define DISP_W  124
#define DISP_H  120
#define CROP_X  ((CAM_W - DISP_W) / 2)


static lv_obj_t   *s_cam_img = NULL;
static lv_img_dsc_t s_cam_img_dsc;
static uint16_t   *s_disp_buf = NULL;

static HumanFaceDetect     *s_detect = nullptr;
static HumanFaceRecognizer *s_recognizer = nullptr;

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) ESP_LOGE(TAG, "Loi mount SPIFFS: %s", esp_err_to_name(ret));
    else ESP_LOGI(TAG, "SPIFFS mount OK");
}


static void create_display_image(void)
{
    s_disp_buf = (uint16_t *)heap_caps_malloc(DISP_W * DISP_H * 2, MALLOC_CAP_SPIRAM);
    memset(s_disp_buf, 0, DISP_W * DISP_H * 2);

    s_cam_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_cam_img_dsc.header.always_zero = 0;
    s_cam_img_dsc.header.w = DISP_W;
    s_cam_img_dsc.header.h = DISP_H;
    s_cam_img_dsc.data_size = DISP_W * DISP_H * 2;
    s_cam_img_dsc.data = (const uint8_t *)s_disp_buf;

    if (lvgl_port_lock(1000)) {
        s_cam_img = lv_img_create(ui_displayliveviewcamera);
        lv_img_set_src(s_cam_img, &s_cam_img_dsc);
        lv_obj_center(s_cam_img);
        lvgl_port_unlock();
    }
}

static void swap_rgb565_bytes(uint16_t *buf, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; i++) buf[i] = (buf[i] >> 8) | (buf[i] << 8);
}

static void draw_rectangle(uint16_t *buf, int img_w, int img_h,
                            int x1, int y1, int x2, int y2, uint16_t color)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= img_w) x2 = img_w - 1;
    if (y2 >= img_h) y2 = img_h - 1;
    if (x1 > x2 || y1 > y2) return;
    for (int x = x1; x <= x2; x++) { buf[y1 * img_w + x] = color; buf[y2 * img_w + x] = color; }
    for (int y = y1; y <= y2; y++) { buf[y * img_w + x1] = color; buf[y * img_w + x2] = color; }
}

static void face_engine_task(void *arg)
{
    int frames_since_recognize = 0;
    const int RECOGNIZE_EVERY_N_FRAMES = 10;

    int frame_count = 0;                                  // THEM
    int64_t last_fps_log = esp_timer_get_time();   
    static char s_last_status[32] = "";

    while (true) {
        if (lv_scr_act() != ui_Facerecognition) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        camera_fb_t *fb = cam_driver_get_frame();      
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        size_t pixel_count = (size_t)fb->width * fb->height;
        swap_rgb565_bytes((uint16_t *)fb->buf, pixel_count);

        dl::image::img_t img;
        img.data = fb->buf;
        img.width = fb->width;
        img.height = fb->height;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

        auto &results = s_detect->run(img);
        
        const char *status_text = "Not Face";
        uint32_t status_color = 0x4DEF2D;
        static char status_buf[32];

        if (!results.empty()) {
            frames_since_recognize++;
            for (auto &res : results) {
                draw_rectangle((uint16_t *)fb->buf, fb->width, fb->height,
                                res.box[0], res.box[1], res.box[2], res.box[3], 0xF800);
            }
            if (frames_since_recognize >= RECOGNIZE_EVERY_N_FRAMES) {
                auto rec_res = s_recognizer->recognize(img, results);
                if (!rec_res.empty()) {
                    snprintf(status_buf, sizeof(status_buf), "ID %d (%.2f)", rec_res[0].id, rec_res[0].similarity);
                    status_text = status_buf;
                    status_color = 0x00AAFF;
                } else {
                    status_text = "Khong nhan ra";
                    status_color = 0xFF9900;
                }
                frames_since_recognize = 0;
            } else {
                status_text = "Da phat hien mat";
            }
        } else {
            frames_since_recognize = 0;
        }

        if (lvgl_port_lock(1000)) {
            for (int y = 0; y < DISP_H; ++y) {
                memcpy(s_disp_buf + y * DISP_W,
                       (uint16_t *)fb->buf + y * CAM_W + CROP_X,
                       DISP_W * sizeof(uint16_t));
            }
            lv_obj_invalidate(s_cam_img);

            if (strcmp(status_text, s_last_status) != 0) {
                lv_label_set_text(ui_statusFace, status_text);
                lv_obj_set_style_text_color(ui_statusFace, lv_color_hex(status_color), LV_PART_MAIN | LV_STATE_DEFAULT);
                strncpy(s_last_status, status_text, sizeof(s_last_status) - 1);
            }
            lvgl_port_unlock();
        }

        cam_driver_return_frame(fb);    
        
        frame_count++;                                     // THEM
        int64_t now = esp_timer_get_time();                // THEM
        if (now - last_fps_log >= 1000000) {                // THEM - moi 1 giay
            ESP_LOGI(TAG, "FPS: %d", frame_count);           // THEM
            frame_count = 0;                                 // THEM
            last_fps_log = now;                              // THEM
        }    

        vTaskDelay(1);
    }
}

void face_engine_init(void)
{
    init_spiffs();
    create_display_image();

    s_detect = new HumanFaceDetect();
    s_recognizer = new HumanFaceRecognizer("/spiffs/face.db");

    xTaskCreatePinnedToCore(face_engine_task, "face_engine_task", 8192, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "face_engine_init xong");
}