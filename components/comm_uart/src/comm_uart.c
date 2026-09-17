// comm_uart.c
#include "comm_uart.h"
#include "driver/gpio.h" 
#include "uart_protocol.h"
#include "app_state.h"
#include "face_engine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "comm_uart";
static uart_port_t s_port = UART_NUM_1;

// ---- May trang thai rap khung don gian ----
typedef enum { WAIT_START, WAIT_CMD, WAIT_LEN, WAIT_PAYLOAD, WAIT_CHECKSUM, WAIT_END } parse_state_t;

static void send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uart_frame_t f = {0};
    f.start = UART_FRAME_START;
    f.cmd = cmd;
    f.len = len;
    if (payload && len > 0 && len <= UART_MAX_PAYLOAD) memcpy(f.payload, payload, len);
    f.checksum = uart_calc_checksum(f.cmd, f.len, f.payload);
    f.end = UART_FRAME_END;
    // Chi gui phan thuc su dung (khong gui het 64 byte payload trong struct)
    uart_write_bytes(s_port, &f.start, 1);
    uart_write_bytes(s_port, &f.cmd, 1);
    uart_write_bytes(s_port, &f.len, 1);
    if (len > 0) uart_write_bytes(s_port, f.payload, len);
    uart_write_bytes(s_port, &f.checksum, 1);
    uart_write_bytes(s_port, &f.end, 1);
}

static void send_ack(void)  { send_frame(RSP_ACK, NULL, 0); }
static void send_nack(void) { send_frame(RSP_NACK, NULL, 0); }

static void handle_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    switch (cmd) {
        case CMD_SET_MODEL: {
            if (len < sizeof(payload_set_model_t)) { send_nack(); break; }
            payload_set_model_t p; memcpy(&p, payload, sizeof(p));
            face_engine_switch_model((recog_model_t)p.model_id);
            send_ack();
            break;
        }
        case CMD_SET_THRESHOLD: {
            if (len < sizeof(payload_set_threshold_t)) { send_nack(); break; }
            payload_set_threshold_t p; memcpy(&p, payload, sizeof(p));
            face_engine_set_threshold(p.threshold);
            send_ack();
            break;
        }
        case CMD_SET_BRIGHTNESS: {
            if (len < sizeof(payload_set_int_param_t)) { send_nack(); break; }
            payload_set_int_param_t p; memcpy(&p, payload, sizeof(p));
            app_state_set_cam_param(p.value, app_state_get_contrast(),
                                     app_state_get_flip(), app_state_get_mirror());
            send_ack();
            break;
        }
        case CMD_SET_CONTRAST: {
            if (len < sizeof(payload_set_int_param_t)) { send_nack(); break; }
            payload_set_int_param_t p; memcpy(&p, payload, sizeof(p));
            app_state_set_cam_param(app_state_get_brightness(), p.value,
                                     app_state_get_flip(), app_state_get_mirror());
            send_ack();
            break;
        }
        case CMD_SET_FLIP: {
            if (len < sizeof(payload_bool_t)) { send_nack(); break; }
            payload_bool_t p; memcpy(&p, payload, sizeof(p));
            app_state_set_cam_param(app_state_get_brightness(), app_state_get_contrast(),
                                     p.enable != 0, app_state_get_mirror());
            send_ack();
            break;
        }
        case CMD_SET_MIRROR: {
            if (len < sizeof(payload_bool_t)) { send_nack(); break; }
            payload_bool_t p; memcpy(&p, payload, sizeof(p));
            app_state_set_cam_param(app_state_get_brightness(), app_state_get_contrast(),
                                     app_state_get_flip(), p.enable != 0);
            send_ack();
            break;
        }
        case CMD_SHOW_FPS: {
            if (len < sizeof(payload_bool_t)) { send_nack(); break; }
            payload_bool_t p; memcpy(&p, payload, sizeof(p));
            app_state_set_show_fps(p.enable != 0);
            send_ack();
            break;
        }
        case CMD_ADD_USER_START: {
            if (len < sizeof(payload_add_user_start_t)) { send_nack(); break; }
            payload_add_user_start_t p; memcpy(&p, payload, sizeof(p));
            face_engine_add_user_start(p.user_id, p.name);
            send_ack();
            break;
        }
        case CMD_ADD_USER_CAPTURE: {
            enroll_state_t st = app_state_get_enroll_state(); // lay TRUOC vi face_engine se xoa sau khi xu ly xong
            bool ok = face_engine_add_user_capture();
            payload_add_user_result_t r = {0};
            r.success = ok ? 1 : 0;
            r.user_id = st.user_id;
            strncpy(r.name, st.user_name, sizeof(r.name) - 1);
            send_frame(RSP_ADD_USER_RESULT, (uint8_t*)&r, sizeof(r));
            break;
        }
        case CMD_ADD_USER_CANCEL: {
            face_engine_add_user_cancel();
            send_ack();
            break;
        }
        case CMD_SUB_USER: {
            if (len < sizeof(payload_sub_user_t)) { send_nack(); break; }
            payload_sub_user_t p; memcpy(&p, payload, sizeof(p));
            bool ok = face_engine_delete_user(p.user_id);
            payload_sub_user_result_t r = { .success = (uint8_t)(ok ? 1 : 0), .user_id = p.user_id };
            send_frame(RSP_SUB_USER_RESULT, (uint8_t*)&r, sizeof(r));
            break;
        }
        case CMD_LIST_USER_REQ: {
            user_info_t list[16];
            int n = face_engine_list_users(list, 16);
            if (n == 0) {
                send_frame(RSP_USER_LIST_END, NULL, 0);
            } else {
                for (int i = 0; i < n; i++) {
                    payload_user_list_item_t item = {0};
                    item.user_id = list[i].user_id;
                    strncpy(item.name, list[i].name, sizeof(item.name) - 1);
                    item.is_last = (i == n - 1) ? 1 : 0;
                    send_frame(RSP_USER_LIST_ITEM, (uint8_t*)&item, sizeof(item));
                }
            }
            break;
        }
        case CMD_START_STREAM:
            app_state_set_streaming(true);
            send_ack();
            break;
        case CMD_STOP_STREAM:
            app_state_set_streaming(false);
            send_ack();
            break;
        default:
            ESP_LOGW(TAG, "Lenh khong ro: 0x%02X", cmd);
            send_nack();
            break;
    }
}

static void uart_rx_task(void *arg)
{
    uint8_t byte;
    parse_state_t state = WAIT_START;
    uint8_t cmd = 0, len = 0, idx = 0, payload[UART_MAX_PAYLOAD];

    while (1) {
        ESP_LOGI(TAG, "Task Uart RX da chay");
        int n = uart_read_bytes(s_port, &byte, 1, pdMS_TO_TICKS(1000));
        if (n <= 0) continue;
    
        switch (state) {
            case WAIT_START:
                if (byte == UART_FRAME_START) state = WAIT_CMD;
                break;
            case WAIT_CMD:
                cmd = byte;
                state = WAIT_LEN;
                break;
            case WAIT_LEN:
                len = byte;
                idx = 0;
                if (len > UART_MAX_PAYLOAD) {
                    ESP_LOGW(TAG, "LEN qua lon (%d), huy khung", len);
                    state = WAIT_START; // resync
                } else if (len == 0) {
                    state = WAIT_CHECKSUM;
                } else {
                    state = WAIT_PAYLOAD;
                }
                break;
            case WAIT_PAYLOAD:
                payload[idx++] = byte;
                if (idx >= len) state = WAIT_CHECKSUM;
                break;
            case WAIT_CHECKSUM: {
                uint8_t expected = uart_calc_checksum(cmd, len, payload);
                if (byte != expected) {
                    ESP_LOGW(TAG, "Sai checksum, huy khung (cmd=0x%02X)", cmd);
                    state = WAIT_START; // resync, KHONG xu ly lenh loi
                } else {
                    state = WAIT_END;
                }
                break;
            }
            case WAIT_END:
                if (byte == UART_FRAME_END) {
                    handle_command(cmd, payload, len);
                } else {
                    ESP_LOGW(TAG, "Thieu byte END, huy khung");
                }
                state = WAIT_START;
                break;
        }
        
    }
}

esp_err_t comm_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud_rate)
{
    s_port = port;
    uart_config_t cfg = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(port, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "COM Uart init OK");

    gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY);
    return ESP_OK;
}

void comm_uart_start(void)
{
    xTaskCreatePinnedToCore(uart_rx_task, "comm_uart_rx", 4096, NULL, 5, NULL, 1);
}

// Cho phep noi khac trong code (vd bao cao FPS dinh ky) gui phan hoi ma khong
// can biet chi tiet dinh dang khung.
void comm_uart_send_response(uint8_t rsp_cmd, const void *payload, uint8_t len)
{
    send_frame(rsp_cmd, (const uint8_t*)payload, len);
}
