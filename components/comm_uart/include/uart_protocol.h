// uart_protocol.h
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// UART Frame Configuration
//==================================================

#define UART_FRAME_START   0xAA
#define UART_FRAME_END     0x55
#define UART_MAX_PAYLOAD   64


//==================================================
// UART Command ID
//==================================================

typedef enum {

    // Node B -> Node A
    CMD_SET_MODEL         = 0x01,
    CMD_SET_THRESHOLD     = 0x02,
    CMD_SET_BRIGHTNESS    = 0x03,
    CMD_SET_CONTRAST      = 0x04,
    CMD_SET_FLIP          = 0x05,
    CMD_SET_MIRROR        = 0x06,
    CMD_SHOW_FPS          = 0x07,

    CMD_ADD_USER_START    = 0x08,
    CMD_ADD_USER_CAPTURE  = 0x09,
    CMD_ADD_USER_CANCEL   = 0x0A,
    CMD_SUB_USER          = 0x0B,
    CMD_CLEAR_ALL_USERS   = 0x0F,
    CMD_LIST_USER_REQ     = 0x0C,

    CMD_START_STREAM      = 0x0D,
    CMD_STOP_STREAM       = 0x0E,

    CMD_ENABLE_LIVENESS_CHECK = 0x10,
    CMD_SET_ESPNOW_CHANNEL    = 0x11,

    // Node A -> Node B
    RSP_ACK               = 0x80,
    RSP_NACK              = 0x81,
    RSP_ADD_USER_RESULT   = 0x82,
    RSP_SUB_USER_RESULT   = 0x83,
    RSP_USER_LIST_ITEM    = 0x84,
    RSP_USER_LIST_END     = 0x85,
    RSP_FPS_REPORT        = 0x86,
    RSP_FACE_MATCHED      = 0x87,
    RSP_FACE_UNKNOWN      = 0x88,
    RSP_LIVENESS_FAIL     = 0x89,

} uart_cmd_id_t;


//==================================================
// Recognition Model
//==================================================

// typedef enum {
//     MODEL_FEAT_MFN_S8_V1 = 0,
//     MODEL_FEAT_MBF_S8_V1 = 1,
// } recog_model_t;


//==================================================
// Payload - Recognition Model
//==================================================

typedef struct __attribute__((packed)) {
    uint8_t model_id;
} payload_set_model_t;


//==================================================
// Payload - Recognition Threshold
//==================================================

typedef struct __attribute__((packed)) {
    float threshold;
} payload_set_threshold_t;


//==================================================
// Payload - Camera Parameter
//==================================================

typedef struct __attribute__((packed)) {
    int8_t value;
} payload_set_int_param_t;


//==================================================
// Payload - Boolean Parameter
//==================================================

typedef struct __attribute__((packed)) {
    uint8_t enable;
} payload_bool_t;


//==================================================
// Payload - Add User
//==================================================

typedef struct __attribute__((packed)) {
    uint16_t user_id;
    char name[24];
} payload_add_user_start_t;


//==================================================
// Payload - Remove User
//==================================================

typedef struct __attribute__((packed)) {
    uint16_t user_id;
} payload_sub_user_t;


//==================================================
// Payload - Add User Result
//==================================================

typedef struct __attribute__((packed)) {
    uint8_t success;
    uint16_t user_id;
    char name[24];
} payload_add_user_result_t;


//==================================================
// Payload - Remove User Result
//==================================================

typedef struct __attribute__((packed)) {
    uint8_t success;
    uint16_t user_id;
} payload_sub_user_result_t;


//==================================================
// Payload - User List
//==================================================

typedef struct __attribute__((packed)) {
    uint16_t user_id;
    char name[24];
    uint8_t is_last;
} payload_user_list_item_t;


//==================================================
// Payload - FPS Report
//==================================================

typedef struct __attribute__((packed)) {
    float fps;
} payload_fps_report_t;


//==================================================
// Payload - Face Matched
//==================================================

typedef struct __attribute__((packed)) {
    uint32_t event_id;
    uint16_t user_id;
    float similarity;
} payload_face_matched_t;


//==================================================
// Payload - Face Event
//==================================================

typedef struct __attribute__((packed)) {
    uint32_t event_id;
} payload_face_event_id_t;

//==================================================
// Payload - Set ESP-NOW WiFi Channel
//==================================================

typedef struct __attribute__((packed)) {
    uint8_t channel;   // 1-13, lay tu esp_wifi_get_channel() ben Node B
} payload_set_channel_t;

//==================================================
// UART Frame
//==================================================

// START | CMD | LEN | PAYLOAD | CHECKSUM | END
typedef struct __attribute__((packed)) {
    uint8_t start;
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[UART_MAX_PAYLOAD];
    uint8_t checksum;
    uint8_t end;
} uart_frame_t;


//==================================================
// Checksum
//==================================================

static inline uint8_t uart_calc_checksum(
    uint8_t cmd,
    uint8_t len,
    const uint8_t *payload
)
{
    uint8_t checksum = cmd ^ len;

    for (uint8_t i = 0; i < len; i++) {
        checksum ^= payload[i];
    }

    return checksum;
}


#ifdef __cplusplus
}
#endif