// comm_uart.c
#include "comm_uart.h"
#include "uart_protocol.h"
#include "app_state.h"
#include "face_engine.h"
#include "cam_driver.h"
#include "esp_wifi.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

//==================================================
// Logging & Runtime State
//==================================================

static const char *TAG = "comm_uart";
static uart_port_t s_port = UART_NUM_1;

//==================================================
// UART Frame Parser
//==================================================

typedef enum {
    WAIT_START,
    WAIT_CMD,
    WAIT_LEN,
    WAIT_PAYLOAD,
    WAIT_CHECKSUM,
    WAIT_END
} parse_state_t;

//==================================================
// Forward Declaration
//==================================================

static void send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len);
static void handle_command(uint8_t cmd, const uint8_t *payload, uint8_t len);
static void uart_rx_task(void *arg);

//==================================================
// UART Frame Transmission
//==================================================

static void send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uart_frame_t frame = {0};

    frame.start = UART_FRAME_START;
    frame.cmd = cmd;
    frame.len = len;

    if (payload && len > 0 && len <= UART_MAX_PAYLOAD) {
        memcpy(frame.payload, payload, len);
    }

    frame.checksum = uart_calc_checksum(frame.cmd, frame.len, frame.payload);
    frame.end = UART_FRAME_END;

    uart_write_bytes(s_port, &frame.start, 1);
    uart_write_bytes(s_port, &frame.cmd, 1);
    uart_write_bytes(s_port, &frame.len, 1);

    if (len > 0) {
        uart_write_bytes(s_port, frame.payload, len);
    }

    uart_write_bytes(s_port, &frame.checksum, 1);
    uart_write_bytes(s_port, &frame.end, 1);
}

static void send_ack(void)
{
    send_frame(RSP_ACK, NULL, 0);
}

static void send_nack(void)
{
    send_frame(RSP_NACK, NULL, 0);
}


//==================================================
// Command Handler
//==================================================

static void handle_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    switch (cmd) {

        //==================================================
        // Recognition Model
        //==================================================

        case CMD_SET_MODEL: {
            if (len < sizeof(payload_set_model_t)) {
                send_nack();
                break;
            }

            payload_set_model_t p;
            memcpy(&p, payload, sizeof(p));

            face_engine_switch_model((recog_model_t)p.model_id);
            send_ack();
            break;
        }

        case CMD_SET_THRESHOLD: {
            if (len < sizeof(payload_set_threshold_t)) {
                send_nack();
                break;
            }

            payload_set_threshold_t p;
            memcpy(&p, payload, sizeof(p));

            face_engine_set_threshold(p.threshold);
            send_ack();
            break;
        }


        //==================================================
        // Camera Configuration
        //==================================================

        case CMD_SET_BRIGHTNESS: {
            if (len < sizeof(payload_set_int_param_t)) {
                send_nack();
                break;
            }

            payload_set_int_param_t p;
            memcpy(&p, payload, sizeof(p));

            app_state_set_cam_param(p.value, app_state_get_contrast(),
                                     app_state_get_flip(), app_state_get_mirror());
            cam_driver_apply_sensor_config(app_state_get_brightness(), app_state_get_contrast(),
                                            app_state_get_flip(), app_state_get_mirror());

            send_ack();
            break;
        }

        case CMD_SET_CONTRAST: {
            if (len < sizeof(payload_set_int_param_t)) {
                send_nack();
                break;
            }

            payload_set_int_param_t p;
            memcpy(&p, payload, sizeof(p));

            app_state_set_cam_param(app_state_get_brightness(), p.value,
                                     app_state_get_flip(), app_state_get_mirror());
            cam_driver_apply_sensor_config(app_state_get_brightness(), app_state_get_contrast(),
                                            app_state_get_flip(), app_state_get_mirror());

            send_ack();
            break;
        }

        case CMD_SET_FLIP: {
            if (len < sizeof(payload_bool_t)) {
                send_nack();
                break;
            }

            payload_bool_t p;
            memcpy(&p, payload, sizeof(p));

            app_state_set_cam_param(app_state_get_brightness(), app_state_get_contrast(),
                                     p.enable != 0, app_state_get_mirror());
            cam_driver_apply_sensor_config(app_state_get_brightness(), app_state_get_contrast(),
                                            app_state_get_flip(), app_state_get_mirror());

            send_ack();
            break;
        }

        case CMD_SET_MIRROR: {
            if (len < sizeof(payload_bool_t)) {
                send_nack();
                break;
            }

            payload_bool_t p;
            memcpy(&p, payload, sizeof(p));

            app_state_set_cam_param(app_state_get_brightness(), app_state_get_contrast(),
                                     app_state_get_flip(), p.enable != 0);
            cam_driver_apply_sensor_config(app_state_get_brightness(), app_state_get_contrast(),
                                            app_state_get_flip(), app_state_get_mirror());

            send_ack();
            break;
        }

        case CMD_SHOW_FPS: {
            if (len < sizeof(payload_bool_t)) {
                send_nack();
                break;
            }

            payload_bool_t p;
            memcpy(&p, payload, sizeof(p));

            app_state_set_show_fps(p.enable != 0);
            send_ack();
            break;
        }


        //==================================================
        // User Enrollment
        //==================================================

        case CMD_ADD_USER_START: {
            if (len < sizeof(payload_add_user_start_t)) {
                send_nack();
                break;
            }

            payload_add_user_start_t p;
            memcpy(&p, payload, sizeof(p));

            face_engine_add_user_start(p.user_id, p.name);
            send_ack();
            break;
        }

        case CMD_ADD_USER_CAPTURE: {
            enroll_state_t st = app_state_get_enroll_state();
            bool ok = face_engine_add_user_capture();

            payload_add_user_result_t result = {0};

            result.success = ok ? 1 : 0;
            result.user_id = st.user_id;

            strncpy(result.name, st.user_name, sizeof(result.name) - 1);
            send_frame(RSP_ADD_USER_RESULT, (uint8_t *)&result, sizeof(result));

            break;
        }

        case CMD_ADD_USER_CANCEL:
            face_engine_add_user_cancel();
            send_ack();
            break;


        //==================================================
        // User Management
        //==================================================

        case CMD_SUB_USER: {
            if (len < sizeof(payload_sub_user_t)) {
                send_nack();
                break;
            }

            payload_sub_user_t p;
            memcpy(&p, payload, sizeof(p));

            bool ok = face_engine_delete_user(p.user_id);

            payload_sub_user_result_t result = {
                .success = (uint8_t)(ok ? 1 : 0),
                .user_id = p.user_id
            };

            send_frame(RSP_SUB_USER_RESULT, (uint8_t *)&result, sizeof(result));

            break;
        }
        case CMD_CLEAR_ALL_USERS: {
            bool ok = face_engine_clear_all_users();
            if (ok) send_ack(); else send_nack();
            break;
        }

        case CMD_LIST_USER_REQ: {
            user_info_t list[16];

            int count = face_engine_list_users(list, 16);

            if (count == 0) {
                send_frame(RSP_USER_LIST_END, NULL, 0);
                break;
            }

            for (int i = 0; i < count; i++) {
                payload_user_list_item_t item = {0};
                item.user_id = list[i].user_id;
                strncpy(item.name, list[i].name, sizeof(item.name) - 1);
                item.is_last = (i == count - 1) ? 1 : 0;
                send_frame(RSP_USER_LIST_ITEM, (uint8_t *)&item, sizeof(item));
            }

            break;
        }
        case CMD_ENABLE_LIVENESS_CHECK: {
            if (len < sizeof(payload_bool_t)) { 
                send_nack(); 
                break; 
            }
            payload_bool_t p; memcpy(&p, payload, sizeof(p));
            app_state_set_liveness_enabled(p.enable != 0);
            send_ack();
            break;
        }

        case CMD_SET_ESPNOW_CHANNEL: {
            if (len < sizeof(payload_set_channel_t)) {
                send_nack();
                break;
            }

            payload_set_channel_t p;
            memcpy(&p, payload, sizeof(p));

            if (p.channel < 1 || p.channel > 13) {
                ESP_LOGW(TAG, "Kenh khong hop le tu Node B: %d", p.channel);
                send_nack();
                break;
            }

            esp_err_t err = esp_wifi_set_channel(p.channel, WIFI_SECOND_CHAN_NONE);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Da doi kenh ESP-NOW theo lenh Node B: kenh %d", p.channel);
                send_ack();
            } 
            else {
                ESP_LOGE(TAG, "esp_wifi_set_channel that bai: %s", esp_err_to_name(err));
                send_nack();
            }
            break;
        }


        //==================================================
        // Streaming
        //==================================================

        case CMD_START_STREAM:
            app_state_set_streaming(true);
            send_ack();
            break;

        case CMD_STOP_STREAM:
            app_state_set_streaming(false);
            send_ack();
            break;
        //==================================================
        // Unknown Command
        //==================================================

        default:
            ESP_LOGW(TAG, "Lenh khong ro: 0x%02X", cmd);
            send_nack();
            break;
    }
}


//==================================================
// UART Receive Task
//==================================================

static void uart_rx_task(void *arg)
{
    uint8_t byte;
    parse_state_t state = WAIT_START;

    uint8_t cmd = 0;
    uint8_t len = 0;
    uint8_t idx = 0;
    uint8_t payload[UART_MAX_PAYLOAD];

    while (1) {

        int n = uart_read_bytes(s_port, &byte, 1, pdMS_TO_TICKS(100));

        if (n <= 0) {
            continue;
        }

        switch (state) {

            case WAIT_START:
                if (byte == UART_FRAME_START) {
                    state = WAIT_CMD;
                }
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
                    state = WAIT_START;
                }
                else if (len == 0) {
                    state = WAIT_CHECKSUM;
                }
                else {
                    state = WAIT_PAYLOAD;
                }

                break;

            case WAIT_PAYLOAD:
                payload[idx++] = byte;

                if (idx >= len) {
                    state = WAIT_CHECKSUM;
                }

                break;

            case WAIT_CHECKSUM: {
                uint8_t expected = uart_calc_checksum(cmd, len, payload);

                if (byte != expected) {
                    ESP_LOGW(TAG, "Sai checksum, huy khung (cmd=0x%02X)", cmd);
                    state = WAIT_START;
                }
                else {
                    state = WAIT_END;
                }

                break;
            }

            case WAIT_END:
                if (byte == UART_FRAME_END) {
                    handle_command(cmd, payload, len);
                }
                else {
                    ESP_LOGW(TAG, "Thieu byte END, huy khung");
                }

                state = WAIT_START;
                break;
        }
    }
}


//==================================================
// Initialization
//==================================================

esp_err_t comm_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud_rate)
{
    s_port = port;

    uart_config_t config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(port, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &config));
    ESP_ERROR_CHECK(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "COM Uart init OK");

    return ESP_OK;
}


//==================================================
// UART Communication
//==================================================

void comm_uart_start(void)
{
    xTaskCreatePinnedToCore(uart_rx_task, "comm_uart_rx", 4096, NULL, 5, NULL, 1);
}

//==================================================
// Response
//==================================================

void comm_uart_send_response(uint8_t rsp_cmd, const void *payload, uint8_t len)
{
    send_frame(rsp_cmd, (const uint8_t *)payload, len);
}