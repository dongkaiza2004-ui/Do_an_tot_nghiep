// app_state.h
// Nguon su that duy nhat cho trang thai/cau hinh runtime cua Node A.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RECOG_MODEL_MFN_S8_V1 = 0,
    RECOG_MODEL_MBF_S8_V1 = 1,
} recog_model_t;

typedef struct {
    bool     in_progress;
    uint16_t user_id;
    char     user_name[24];
} enroll_state_t;

// ---- Khoi tao / luu-nap NVS ----
esp_err_t app_state_init(void);          // goi 1 lan luc app_main, tu load gia tri da luu trong NVS (neu co)
void      app_state_save_to_nvs(void);   // ghi cac gia tri "ben vung" xuong NVS (nen debounce, khong goi qua thuong xuyen)

// ---- Model & threshold nhan dien ----
void          app_state_set_model(recog_model_t model);
recog_model_t app_state_get_model(void);
void          app_state_set_threshold(float threshold);
float         app_state_get_threshold(void);

// ---- Cau hinh camera ----
void  app_state_set_cam_param(int8_t brightness, int8_t contrast, bool flip, bool mirror);
int8_t app_state_get_brightness(void);
int8_t app_state_get_contrast(void);
bool  app_state_get_flip(void);
bool  app_state_get_mirror(void);

// ---- Hien thi ----
void app_state_set_show_fps(bool enable);
bool app_state_get_show_fps(void);

// ---- Streaming (AI + display local co chay hay khong) ----
void app_state_set_streaming(bool enable);
bool app_state_get_streaming(void);

// ---- Enroll (them nguoi dung, luong 2 buoc) ----
void app_state_begin_enroll(uint16_t user_id, const char *name);
void app_state_cancel_enroll(void);
bool app_state_is_enrolling(void);
enroll_state_t app_state_get_enroll_state(void);

// ---- Nguong anh sang (dung cho light_ctrl, co the chinh tu xa sau nay) ----
void app_state_set_light_threshold(int lux);
int  app_state_get_light_threshold(void);

#ifdef __cplusplus
}
#endif
