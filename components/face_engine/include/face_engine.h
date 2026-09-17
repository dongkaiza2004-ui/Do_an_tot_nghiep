// face_engine.h
// Loi AI: detect + recognize + quan ly user DB + phan loai su kien (nguoi la /
// dang nhap thanh cong) kem chong spam. Header nay dung C-linkage de comm_uart.c
// (file C) va app_main.cpp deu goi duoc.
//
// GHI CHU QUAN TRONG: cac ham cua HumanFaceDetect/HumanFaceRecognizer trong file
// .cpp duoc viet dua tren API duoc mo ta trong tai lieu esp-dl tai thoi diem thiet
// ke (constructor nhan model_type_t, ham enroll()/recognize() nhan (img, results),
// ham set_thresh()/get_thresh()). Vi idf_component.yml khai bao version "*" (luon
// lay ban moi nhat), TEN HAM/ENUM CU THE CO THE LECH so voi ban ban dang keo ve.
// Sau khi build lan dau (managed_components duoc tai ve), hay doi chieu lai voi:
//   managed_components/espressif__human_face_detect/include/human_face_detect.hpp
//   managed_components/espressif__human_face_recognition/include/human_face_recognition.hpp
// va sua ten ham/enum trong face_engine.cpp cho khop neu can.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACE_ENGINE_MAX_RESULTS 4
#define FACE_ENGINE_NAME_LEN    24

typedef enum {
    FACE_EVENT_NONE = 0,
    FACE_EVENT_STRANGER,
    FACE_EVENT_LOGIN_SUCCESS,
} face_event_type_t;

typedef struct {
    int x1, y1, x2, y2;
    bool matched;
    uint16_t user_id;
    char name[FACE_ENGINE_NAME_LEN];
    float similarity;
} face_result_t;

typedef struct {
    face_event_type_t type;
    uint16_t user_id;
    char name[FACE_ENGINE_NAME_LEN];
    uint8_t *jpeg_data;   // cap phat bang malloc/heap_caps_malloc, nguoi nhan (comm_espnow) chiu trach nhiem free() sau khi gui xong
    size_t jpeg_len;
} face_event_t;

typedef struct {
    uint16_t user_id;
    char name[FACE_ENGINE_NAME_LEN];
} user_info_t;

esp_err_t face_engine_init(void);

// Xu ly 1 frame RGB565: chay detect+recognize, ghi ket qua vao out_results (toi da
// FACE_ENGINE_MAX_RESULTS phan tu), tra ve so luong khuon mat phat hien duoc.
// Neu co su kien (nguoi la / dang nhap thanh cong, va khong bi cooldown chan),
// ham se tu dong nen JPEG + day 1 face_event_t vao event_queue truyen vao luc init
// (xem face_engine_set_event_queue).
int face_engine_process_frame(uint16_t *rgb565_buf, int width, int height,
                               face_result_t *out_results);

void face_engine_set_event_queue(void *queue_handle); // QueueHandle_t, tranh include freertos o day

// ---- Model & threshold ----
void face_engine_switch_model(recog_model_t model);
void face_engine_set_threshold(float threshold);

// ---- Quan ly user ----
// Buoc 1: bat dau cho, ghi vao app_state (enroll_in_progress=true) de overlay_render ve hint.
void face_engine_add_user_start(uint16_t user_id, const char *name);
// Buoc 2: chup frame HIEN TAI (rgb565_buf dang duoc face_engine_process_frame xu ly gan nhat) de enroll.
// Tra ve true neu enroll thanh cong.
bool face_engine_add_user_capture(void);
void face_engine_add_user_cancel(void);

bool face_engine_delete_user(uint16_t user_id);
int  face_engine_list_users(user_info_t *out_list, int max_count);

#ifdef __cplusplus
}
#endif
