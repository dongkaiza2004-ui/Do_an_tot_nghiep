// app_state.c
#include <string.h>
#include "app_state.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_state";
static const char *NVS_NS = "app_state";

// --- Trang thai noi bo, bao ve bang mutex vi nhieu task (comm_uart, light_ctrl, face_engine...) cung truy cap ---
static SemaphoreHandle_t s_lock;

static recog_model_t s_model            = RECOG_MODEL_MFN_S8_V1;
static float         s_threshold        = 0.55f;
static int8_t        s_brightness       = 0;
static int8_t        s_contrast         = 0;
static bool          s_flip             = true;
static bool          s_mirror           = true;
static bool          s_show_fps         = false;
static bool          s_streaming        = true;
static int           s_light_threshold  = 50; // lux, gia tri mac dinh, chinh sau khi chon cam bien thuc te
static enroll_state_t s_enroll          = {0};

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

esp_err_t app_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        uint8_t tmp8;
        int32_t tmp32;
        if (nvs_get_u8(h, "model", &tmp8) == ESP_OK)      s_model = (recog_model_t)tmp8;
        if (nvs_get_i32(h, "thresh_x1000", &tmp32) == ESP_OK) s_threshold = tmp32 / 1000.0f;
        if (nvs_get_i8(h, "bright", (int8_t*)&tmp8) == ESP_OK) s_brightness = (int8_t)tmp8;
        if (nvs_get_i8(h, "contrast", (int8_t*)&tmp8) == ESP_OK) s_contrast = (int8_t)tmp8;
        if (nvs_get_u8(h, "flip", &tmp8) == ESP_OK)        s_flip = tmp8 != 0;
        if (nvs_get_u8(h, "mirror", &tmp8) == ESP_OK)      s_mirror = tmp8 != 0;
        if (nvs_get_i32(h, "light_thresh", &tmp32) == ESP_OK) s_light_threshold = tmp32;
        nvs_close(h);
        ESP_LOGI(TAG, "Da nap cau hinh tu NVS");
    } else {
        ESP_LOGW(TAG, "Chua co cau hinh NVS (%s), dung gia tri mac dinh", esp_err_to_name(err));
    }
    return ESP_OK;
}

void app_state_save_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Khong mo duoc NVS de luu");
        return;
    }
    LOCK();
    nvs_set_u8(h, "model", (uint8_t)s_model);
    nvs_set_i32(h, "thresh_x1000", (int32_t)(s_threshold * 1000));
    nvs_set_i8(h, "bright", s_brightness);
    nvs_set_i8(h, "contrast", s_contrast);
    nvs_set_u8(h, "flip", s_flip ? 1 : 0);
    nvs_set_u8(h, "mirror", s_mirror ? 1 : 0);
    nvs_set_i32(h, "light_thresh", s_light_threshold);
    UNLOCK();
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Da luu cau hinh xuong NVS");
}

void app_state_set_model(recog_model_t model) { LOCK(); s_model = model; UNLOCK(); }
recog_model_t app_state_get_model(void)       { LOCK(); recog_model_t m = s_model; UNLOCK(); return m; }

void app_state_set_threshold(float t) { LOCK(); s_threshold = t; UNLOCK(); }
float app_state_get_threshold(void)   { LOCK(); float t = s_threshold; UNLOCK(); return t; }

void app_state_set_cam_param(int8_t brightness, int8_t contrast, bool flip, bool mirror)
{
    LOCK();
    s_brightness = brightness;
    s_contrast   = contrast;
    s_flip       = flip;
    s_mirror     = mirror;
    UNLOCK();
}
int8_t app_state_get_brightness(void) { LOCK(); int8_t v = s_brightness; UNLOCK(); return v; }
int8_t app_state_get_contrast(void)   { LOCK(); int8_t v = s_contrast;   UNLOCK(); return v; }
bool   app_state_get_flip(void)       { LOCK(); bool v = s_flip;         UNLOCK(); return v; }
bool   app_state_get_mirror(void)     { LOCK(); bool v = s_mirror;       UNLOCK(); return v; }

void app_state_set_show_fps(bool enable) { LOCK(); s_show_fps = enable; UNLOCK(); }
bool app_state_get_show_fps(void)        { LOCK(); bool v = s_show_fps; UNLOCK(); return v; }

void app_state_set_streaming(bool enable) { LOCK(); s_streaming = enable; UNLOCK(); }
bool app_state_get_streaming(void)        { LOCK(); bool v = s_streaming; UNLOCK(); return v; }

void app_state_begin_enroll(uint16_t user_id, const char *name)
{
    LOCK();
    s_enroll.in_progress = true;
    s_enroll.user_id = user_id;
    strncpy(s_enroll.user_name, name, sizeof(s_enroll.user_name) - 1);
    s_enroll.user_name[sizeof(s_enroll.user_name) - 1] = '\0';
    UNLOCK();
}
void app_state_cancel_enroll(void)
{
    LOCK();
    memset(&s_enroll, 0, sizeof(s_enroll));
    UNLOCK();
}
bool app_state_is_enrolling(void) { LOCK(); bool v = s_enroll.in_progress; UNLOCK(); return v; }
enroll_state_t app_state_get_enroll_state(void)
{
    LOCK(); enroll_state_t e = s_enroll; UNLOCK(); return e;
}

void app_state_set_light_threshold(int lux) { LOCK(); s_light_threshold = lux; UNLOCK(); }
int  app_state_get_light_threshold(void)    { LOCK(); int v = s_light_threshold; UNLOCK(); return v; }
