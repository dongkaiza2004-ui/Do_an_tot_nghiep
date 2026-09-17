// uart_protocol.h
// Giao thuc dieu khien qua UART giua Node B (UI/LVGL) va Node A (Camera/AI)
// Node A la ben THUC THI lenh va PHAN HOI ket qua.
// Khung truyen don gian: START | CMD | LEN | PAYLOAD[LEN] | CHECKSUM | END

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_FRAME_START   0xAA
#define UART_FRAME_END     0x55
#define UART_MAX_PAYLOAD   64

// ---------------------------------------------------------------------------
// Ma lenh
// ---------------------------------------------------------------------------
typedef enum {
    // ---- Lenh dieu khien: Node B -> Node A ----
    CMD_SET_MODEL         = 0x01,  // payload_set_model_t
    CMD_SET_THRESHOLD     = 0x02,  // payload_set_threshold_t
    CMD_SET_BRIGHTNESS    = 0x03,  // payload_set_int_param_t
    CMD_SET_CONTRAST      = 0x04,  // payload_set_int_param_t
    CMD_SET_FLIP          = 0x05,  // payload_bool_t
    CMD_SET_MIRROR        = 0x06,  // payload_bool_t
    CMD_SHOW_FPS          = 0x07,  // payload_bool_t (bat/tat overlay FPS len bitmap)
    CMD_ADD_USER_START    = 0x08,  // payload_add_user_start_t - vao che do cho, ve huong dan "dua mat vao khung"
    CMD_ADD_USER_CAPTURE  = 0x09,  // (khong payload) - nguoi dung da an nut xac nhan tren Node B, chup enroll ngay
    CMD_ADD_USER_CANCEL   = 0x0A,  // (khong payload) - huy che do enroll, ve lai man hinh binh thuong
    CMD_SUB_USER          = 0x0B,  // payload_sub_user_t
    CMD_LIST_USER_REQ     = 0x0C,  // (khong payload) - yeu cau tra danh sach user
    CMD_START_STREAM      = 0x0D,  // (khong payload) - bat dau gui bitmap qua ESP-NOW
    CMD_STOP_STREAM       = 0x0E,  // (khong payload) - dung gui, tiet kiem bang thong/nang luong

    // ---- Phan hoi: Node A -> Node B ----
    RSP_ACK               = 0x80,  // (khong payload hoac 1 byte echo cmd goc)
    RSP_NACK              = 0x81,  // payload: 1 byte ma loi (tuy dinh nghia them)
    RSP_ADD_USER_RESULT   = 0x82,  // payload_add_user_result_t
    RSP_SUB_USER_RESULT   = 0x83,  // payload_sub_user_result_t
    RSP_USER_LIST_ITEM    = 0x84,  // payload_user_list_item_t - gui lap lai cho tung user
    RSP_USER_LIST_END     = 0x85,  // (khong payload) - bao het danh sach
    RSP_FPS_REPORT        = 0x86,  // payload_fps_report_t - gui dinh ky (vd 1s/lan)
} uart_cmd_id_t;
// Danh sach model nhan dien co san (khop voi Kconfig da bat trong sdkconfig)
// typedef enum {
//     MODEL_FEAT_MFN_S8_V1 = 0,
//     MODEL_FEAT_MBF_S8_V1 = 1,
// } recog_model_t;
// ---------------------------------------------------------------------------
// Cac cau truc payload cho tung lenh
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t model_id;              // xem recog_model_t
} payload_set_model_t;

typedef struct __attribute__((packed)) {
    float threshold;               // nguong tuong dong de coi la "khop"
} payload_set_threshold_t;

typedef struct __attribute__((packed)) {
    int8_t value;                  // dung chung cho brightness/contrast, tuy pham vi cam ho tro (vd -2..2)
} payload_set_int_param_t;

typedef struct __attribute__((packed)) {
    uint8_t enable;                 // 0 = tat, 1 = bat. Dung chung cho flip/mirror/show_fps/stream
} payload_bool_t;

typedef struct __attribute__((packed)) {
    uint16_t user_id;
    char     name[24];              // ten hien thi, null-terminated, cat bot neu dai hon 23 ky tu
} payload_add_user_start_t;

typedef struct __attribute__((packed)) {
    uint16_t user_id;
} payload_sub_user_t;

typedef struct __attribute__((packed)) {
    uint8_t  success;               // 1 = thanh cong
    uint16_t user_id;
    char     name[24];
} payload_add_user_result_t;

typedef struct __attribute__((packed)) {
    uint8_t  success;
    uint16_t user_id;
} payload_sub_user_result_t;

typedef struct __attribute__((packed)) {
    uint16_t user_id;
    char     name[24];
    uint8_t  is_last;               // 1 neu la phan tu cuoi trong danh sach dang tra ve
} payload_user_list_item_t;

typedef struct __attribute__((packed)) {
    float fps;
} payload_fps_report_t;

// ---------------------------------------------------------------------------
// Khung truyen chung tren day UART
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t start;                          // = UART_FRAME_START
    uint8_t cmd;                            // uart_cmd_id_t
    uint8_t len;                            // do dai payload thuc te (0..UART_MAX_PAYLOAD)
    uint8_t payload[UART_MAX_PAYLOAD];
    uint8_t checksum;                       // XOR(cmd, len, payload[0..len-1])
    uint8_t end;                            // = UART_FRAME_END
} uart_frame_t;

// Ham tinh checksum don gian (XOR) - dat inline de dung chung ca 2 node
static inline uint8_t uart_calc_checksum(uint8_t cmd, uint8_t len, const uint8_t *payload)
{
    uint8_t c = cmd ^ len;
    for (uint8_t i = 0; i < len; i++) {
        c ^= payload[i];
    }
    return c;
}

#ifdef __cplusplus
}
#endif
