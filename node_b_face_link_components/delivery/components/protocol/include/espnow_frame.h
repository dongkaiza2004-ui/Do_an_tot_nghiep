// espnow_frame.h

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// ESP-NOW Frame Configuration
//==================================================

#define ESPNOW_FRAGMENT_MAX_PAYLOAD  230


//==================================================
// ESP-NOW Frame Fragment
//==================================================

typedef struct __attribute__((packed)) {
    uint32_t event_id;
    uint16_t width;
    uint16_t height;
    uint32_t jpeg_len;

    uint16_t fragment_index;
    uint16_t fragment_total;
    uint16_t payload_len;

    uint8_t payload[ESPNOW_FRAGMENT_MAX_PAYLOAD];
} espnow_frame_fragment_t;


#ifdef __cplusplus
}
#endif