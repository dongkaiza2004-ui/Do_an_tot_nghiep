// comm_espnow.h
// Nhan face_event_t (JPEG + metadata) tu face_engine qua 1 queue, chia fragment
// va gui sang Node B qua ESP-NOW.
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t comm_espnow_init(const uint8_t peer_mac[6]);

// Tra ve QueueHandle_t (dang void* de tranh phai include face_engine.h/C++ o day) -
// truyen thang vao face_engine_set_event_queue() ben app_main.cpp.
void *comm_espnow_get_event_queue(void);

void comm_espnow_start(void);

#ifdef __cplusplus
}
#endif
