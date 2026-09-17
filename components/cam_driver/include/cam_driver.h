// cam_driver.h
#pragma once
#include "esp_err.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao phan cung camera (chan GPIO, dinh dang, kich thuoc anh...).
// Ap dung san gia tri brightness/contrast/flip/mirror hien co trong app_state.
esp_err_t cam_driver_init(void);

// Bat dau task nen chup frame lien tuc, day tung camera_fb_t* vao out_queue.
// out_queue nen la queue length = 1 va dung xQueueOverwrite ben trong task nay
// de luon giu frame MOI NHAT, tranh don frame cu khi tang xu ly cham hon toc do chup.
void cam_driver_start(QueueHandle_t out_queue);

// Ap dung lai cau hinh cam bien (goi khi comm_uart nhan lenh SET_BRIGHTNESS/CONTRAST/FLIP/MIRROR).
void cam_driver_apply_sensor_config(int8_t brightness, int8_t contrast, bool flip, bool mirror);

#ifdef __cplusplus
}
#endif
