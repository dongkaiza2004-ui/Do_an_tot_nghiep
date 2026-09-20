// face_engine.h

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// Face Engine Configuration
//==================================================

#define FACE_ENGINE_MAX_RESULTS  4
#define FACE_ENGINE_NAME_LEN     24

//==================================================
// Face Event
//==================================================

typedef enum {
    FACE_EVENT_NONE = 0,
    FACE_EVENT_STRANGER,
    FACE_EVENT_LOGIN_SUCCESS,
    FACE_EVENT_LIVENESS_FAIL,
} face_event_type_t;

//==================================================
// Face Recognition Result
//==================================================

typedef struct {
    int x1, y1, x2, y2;
    bool matched;
    uint16_t user_id;
    char name[FACE_ENGINE_NAME_LEN];
    float similarity;
} face_result_t;

//==================================================
// Face Event Data
//==================================================

typedef struct {
    face_event_type_t type;
    uint32_t event_id;
    uint16_t user_id;
    char name[FACE_ENGINE_NAME_LEN];
    uint16_t width;
    uint16_t height;
    uint8_t *jpeg_data;
    size_t jpeg_len;
} face_event_t;

//==================================================
// User Information
//==================================================

typedef struct {
    uint16_t user_id;
    char name[FACE_ENGINE_NAME_LEN];
} user_info_t;

//==================================================
// Initialization
//==================================================
esp_err_t face_engine_init(void);

//==================================================
// UART Report
//==================================================

typedef void (*face_uart_report_cb_t)(face_event_type_t type, uint32_t event_id, uint16_t user_id, float similarity);
void face_engine_set_uart_report_cb(face_uart_report_cb_t cb);

//==================================================
// Face Detection & Recognition
//==================================================

int face_engine_process_frame(
    uint16_t *rgb565_buf,
    int width,
    int height,
    face_result_t *out_results
);

//==================================================
// Event Queue
//==================================================

void face_engine_set_event_queue(void *queue_handle);

//==================================================
// Recognition Model & Threshold
//==================================================

void face_engine_switch_model(recog_model_t model);
void face_engine_set_threshold(float threshold);

//==================================================
// User Enrollment
//==================================================

void face_engine_add_user_start(uint16_t user_id, const char *name);
bool face_engine_add_user_capture(void);
void face_engine_add_user_cancel(void);

//==================================================
// User Management
//==================================================

bool face_engine_delete_user(uint16_t user_id);
bool face_engine_clear_all_users(void);
int face_engine_list_users(user_info_t *out_list, int max_count);

#ifdef __cplusplus
}
#endif