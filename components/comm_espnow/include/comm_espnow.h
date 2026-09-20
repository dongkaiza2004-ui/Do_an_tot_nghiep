// comm_espnow.h

#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// Khởi tạo
//==================================================

esp_err_t comm_espnow_init(const uint8_t peer_mac[6]);


//==================================================
// Event Queue
//==================================================

void *comm_espnow_get_event_queue(void);


//==================================================
// ESP-NOW Communication
//==================================================

void comm_espnow_start(void);


#ifdef __cplusplus
}
#endif