// app_state.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// Kiểu dữ liệu
//==================================================

typedef enum {
    RECOG_MODEL_MFN_S8_V1 = 0,
    RECOG_MODEL_MBF_S8_V1 = 1,
} recog_model_t;

typedef struct {
    bool     in_progress;
    uint16_t user_id;
    char     user_name[24];
} enroll_state_t;

//==================================================
// Khởi tạo / NVS
//==================================================

esp_err_t app_state_init(void);
void app_state_save_to_nvs(void);

//==================================================
// Recognition Model & Threshold
//==================================================

void app_state_set_model(recog_model_t model);
recog_model_t app_state_get_model(void);
void app_state_set_threshold(float threshold);
float app_state_get_threshold(void);

//==================================================
// Cấu hình Camera
//==================================================

void app_state_set_cam_param(
    int8_t brightness,
    int8_t contrast,
    bool flip,
    bool mirror
);

int8_t app_state_get_brightness(void);
int8_t app_state_get_contrast(void);
bool app_state_get_flip(void);
bool app_state_get_mirror(void);

//==================================================
// Hiển thị
//==================================================

void app_state_set_show_fps(bool enable);
bool app_state_get_show_fps(void);

//==================================================
// Streaming
//==================================================

void app_state_set_streaming(bool enable);
bool app_state_get_streaming(void);

//==================================================
// Enrollment
//==================================================

void app_state_begin_enroll(uint16_t user_id, const char *name);
void app_state_cancel_enroll(void);
bool app_state_is_enrolling(void);
enroll_state_t app_state_get_enroll_state(void);


//==================================================
// Light Control
//==================================================

void app_state_set_light_threshold(int lux);
int app_state_get_light_threshold(void);

#ifdef __cplusplus
}
#endif