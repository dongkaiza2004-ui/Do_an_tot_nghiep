// cam_driver.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// Khởi tạo
//==================================================

esp_err_t cam_driver_init(void);


//==================================================
// Camera Capture
//==================================================

void cam_driver_start(QueueHandle_t out_queue);


//==================================================
// Cấu hình Camera Sensor
//==================================================

void cam_driver_apply_sensor_config(
    int8_t brightness,
    int8_t contrast,
    bool flip,
    bool mirror
);


#ifdef __cplusplus
}
#endif