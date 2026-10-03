// comm_uart.h
//
// Lop giao tiep UART phia Node B (door_lock). Doi xung voi comm_uart.c ben
// project camera (Node A) nhung dao vai tro: Node B la ben CHU DONG gui CMD_*
// xuong va NHAN cac RSP_* tra ve.
//
// Lop nay CHI lo phan giao thuc/byte - khong biet gi ve BoltLockManager hay
// Matter. Logic nghiep vu (quyet dinh mo khoa...) nam o access_gateway
// (trong main/), dang ky lam "listener" thong qua comm_uart_register_listener().

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/uart.h"

#include "uart_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Callback nhan MOI phan hoi RSP_* tu camera. payload/len la du lieu tho da
// qua kiem tra checksum, CHUA duoc ep kieu ve struct - callback tu memcpy
// sang struct payload_*_t tuong ung voi rsp_cmd truoc khi doc.
typedef void (*comm_uart_rsp_cb_t)(uint8_t rsp_cmd, const uint8_t *payload, uint8_t len, void *ctx);

// Handle tra ve boi comm_uart_register_listener(). Gia tri < 0 nghia la dang
// ky that bai (het cho).
typedef int comm_uart_listener_handle_t;

// Khoi tao UART (cai dat driver, cau hinh chan, tao mutex noi bo).
// KHONG tu tao task nhan - goi comm_uart_start() rieng sau khi da dang ky het
// cac listener can thiet, de tranh bo lot khung dau tien.
esp_err_t comm_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud_rate);

// Bat task doc UART chay nen (uu tien cao, ghim vao core 1 giong ben camera).
void comm_uart_start(void);

// Dang ky nhan TAT CA cac RSP_* tu camera. Co the dang ky nhieu listener dong
// thoi (toi da COMM_UART_MAX_LISTENERS, xem comm_uart.c).
comm_uart_listener_handle_t comm_uart_register_listener(comm_uart_rsp_cb_t cb, void *ctx);

// Go dang ky mot listener da dang ky truoc do.
//
// BAT BUOC phai goi ham nay TRUOC KHI giai phong bat ky bo nho/queue nao ma
// callback cua listener con tham chieu toi (vi du: cac "cho phan hoi dong bo
// tam thoi" trong access_dashboard) - neu khong, task doc UART co the goi vao
// callback dang tro toi vung nho da bi giai phong va gay crash.
//
// GHI CHU (gioi han da biet): viec go dang ky va viec dispatch dang chay o
// task khac co mot khoang rat hep co the dam vao nhau (dispatch da lay snapshot
// truoc do). Voi toc do khung UART thap cua he thong nay, rui ro nay chap
// nhan duoc; neu can loai bo hoan toan phai them co che dong bo chat hon
// (vd dem so callback dang chay va cho ve 0 truoc khi tra ve tu ham nay).
esp_err_t comm_uart_unregister_listener(comm_uart_listener_handle_t handle);

// Gui mot khung CMD_*/RSP_* tuy y voi payload tho. Dung khi khong co san ham
// goi rieng (typed helper) ben duoi.
esp_err_t comm_uart_send_cmd(uint8_t cmd, const void *payload, uint8_t len);

// ------------------------------------------------------------------------
// Cac ham goi lenh dinh san (khuyen khich dung thay vi tu tay dong goi
// struct payload) - moi ham tuong ung 1-1 voi 1 lenh CMD_* trong uart_protocol.h
// ------------------------------------------------------------------------

esp_err_t comm_uart_cmd_set_model(uint8_t model_id);
esp_err_t comm_uart_cmd_set_threshold(float threshold);
esp_err_t comm_uart_cmd_set_brightness(int8_t value);
esp_err_t comm_uart_cmd_set_contrast(int8_t value);
esp_err_t comm_uart_cmd_set_flip(bool enable);
esp_err_t comm_uart_cmd_set_mirror(bool enable);
esp_err_t comm_uart_cmd_show_fps(bool enable);

// name: chuoi C, se bi cat bot neu dai hon 23 ky tu (payload_add_user_start_t.name[24]).
esp_err_t comm_uart_cmd_add_user_start(uint16_t user_id, const char *name);
esp_err_t comm_uart_cmd_add_user_capture(void);
esp_err_t comm_uart_cmd_add_user_cancel(void);
esp_err_t comm_uart_cmd_sub_user(uint16_t user_id);
esp_err_t comm_uart_cmd_clear_all_users(void);
esp_err_t comm_uart_cmd_list_users_request(void);
esp_err_t comm_uart_cmd_start_stream(void);
esp_err_t comm_uart_cmd_stop_stream(void);
esp_err_t comm_uart_cmd_enable_liveness_check(bool enable);
esp_err_t comm_uart_cmd_set_espnow_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif
