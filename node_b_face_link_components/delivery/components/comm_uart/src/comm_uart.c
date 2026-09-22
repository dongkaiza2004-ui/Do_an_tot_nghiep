// comm_uart.c
//
// May trang thai parse khung UART GIONG HET logic ben comm_uart.c cua camera
// (Node A) - dam bao 2 ben hieu cung mot dinh dang khung. Diem khac biet duy
// nhat: o day KHONG co switch-case xu ly nghiep vu cung, ma dispatch moi
// RSP_* nhan duoc toi mot danh sach listener dang ky tu ben ngoai
// (xem access_gateway.cpp va access_dashboard_http.c).

#include "comm_uart.h"
#include "uart_protocol.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "comm_uart";
static uart_port_t s_port = UART_NUM_1;

#define MAX_LISTENERS 6

typedef enum {
    WAIT_START,
    WAIT_CMD,
    WAIT_LEN,
    WAIT_PAYLOAD,
    WAIT_CHECKSUM,
    WAIT_END
} parse_state_t;

typedef struct {
    bool in_use;
    comm_uart_rsp_cb_t cb;
    void *ctx;
} listener_slot_t;

static listener_slot_t s_listeners[MAX_LISTENERS];
static SemaphoreHandle_t s_listener_mutex;

//==================================================
// Gui khung
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

//==================================================
// Dispatch phan hoi toi cac listener dang ky
//==================================================

static void dispatch_response(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    listener_slot_t local[MAX_LISTENERS];

    if (!s_listener_mutex || xSemaphoreTake(s_listener_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Khong lay duoc listener mutex, bo qua dispatch cmd=0x%02X", cmd);
        return;
    }
    memcpy(local, s_listeners, sizeof(local));
    xSemaphoreGive(s_listener_mutex);

    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (local[i].in_use && local[i].cb) {
            local[i].cb(cmd, payload, len, local[i].ctx);
        }
    }
}

//==================================================
// Task doc UART - may trang thai theo LEN (khong do byte END trong luong)
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
            } else if (len == 0) {
                state = WAIT_CHECKSUM;
            } else {
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
            } else {
                state = WAIT_END;
            }
            break;
        }

        case WAIT_END:
            if (byte == UART_FRAME_END) {
                dispatch_response(cmd, payload, len);
            } else {
                ESP_LOGW(TAG, "Thieu byte END, huy khung (cmd=0x%02X)", cmd);
            }
            state = WAIT_START;
            break;
        }
    }
}

//==================================================
// Khoi tao / Bat dau
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

    esp_err_t err = uart_driver_install(port, 1024, 1024, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(port, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY);

    if (!s_listener_mutex) {
        s_listener_mutex = xSemaphoreCreateMutex();
        if (!s_listener_mutex) {
            ESP_LOGE(TAG, "Khong tao duoc listener mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(s_listeners, 0, sizeof(s_listeners));

    ESP_LOGI(TAG, "comm_uart init OK (port=%d, tx=%d, rx=%d, baud=%d)", port, tx_pin, rx_pin, baud_rate);
    return ESP_OK;
}

void comm_uart_start(void)
{
    xTaskCreatePinnedToCore(uart_rx_task, "comm_uart_rx", 4096, NULL, 5, NULL, 1);
}

//==================================================
// Dang ky / go dang ky listener
//==================================================

comm_uart_listener_handle_t comm_uart_register_listener(comm_uart_rsp_cb_t cb, void *ctx)
{
    if (!cb || !s_listener_mutex) {
        return -1;
    }

    int handle = -1;
    xSemaphoreTake(s_listener_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!s_listeners[i].in_use) {
            s_listeners[i].in_use = true;
            s_listeners[i].cb = cb;
            s_listeners[i].ctx = ctx;
            handle = i;
            break;
        }
    }
    xSemaphoreGive(s_listener_mutex);

    if (handle < 0) {
        ESP_LOGE(TAG, "Het cho dang ky listener (MAX_LISTENERS=%d)", MAX_LISTENERS);
    }
    return handle;
}

esp_err_t comm_uart_unregister_listener(comm_uart_listener_handle_t handle)
{
    if (handle < 0 || handle >= MAX_LISTENERS || !s_listener_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_listener_mutex, portMAX_DELAY);
    s_listeners[handle].in_use = false;
    s_listeners[handle].cb = NULL;
    s_listeners[handle].ctx = NULL;
    xSemaphoreGive(s_listener_mutex);
    return ESP_OK;
}

//==================================================
// Gui lenh (generic)
//==================================================

esp_err_t comm_uart_send_cmd(uint8_t cmd, const void *payload, uint8_t len)
{
    if (len > UART_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    send_frame(cmd, (const uint8_t *)payload, len);
    return ESP_OK;
}

//==================================================
// Typed helper - moi ham dong goi 1 struct payload_*_t roi goi send_cmd
//==================================================

esp_err_t comm_uart_cmd_set_model(uint8_t model_id)
{
    payload_set_model_t p = { .model_id = model_id };
    return comm_uart_send_cmd(CMD_SET_MODEL, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_set_threshold(float threshold)
{
    payload_set_threshold_t p = { .threshold = threshold };
    return comm_uart_send_cmd(CMD_SET_THRESHOLD, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_set_brightness(int8_t value)
{
    payload_set_int_param_t p = { .value = value };
    return comm_uart_send_cmd(CMD_SET_BRIGHTNESS, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_set_contrast(int8_t value)
{
    payload_set_int_param_t p = { .value = value };
    return comm_uart_send_cmd(CMD_SET_CONTRAST, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_set_flip(bool enable)
{
    payload_bool_t p = { .enable = (uint8_t)(enable ? 1 : 0) };
    return comm_uart_send_cmd(CMD_SET_FLIP, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_set_mirror(bool enable)
{
    payload_bool_t p = { .enable = (uint8_t)(enable ? 1 : 0) };
    return comm_uart_send_cmd(CMD_SET_MIRROR, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_show_fps(bool enable)
{
    payload_bool_t p = { .enable = (uint8_t)(enable ? 1 : 0) };
    return comm_uart_send_cmd(CMD_SHOW_FPS, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_add_user_start(uint16_t user_id, const char *name)
{
    payload_add_user_start_t p = {0};
    p.user_id = user_id;
    if (name) {
        strncpy(p.name, name, sizeof(p.name) - 1);
    }
    return comm_uart_send_cmd(CMD_ADD_USER_START, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_add_user_capture(void)
{
    return comm_uart_send_cmd(CMD_ADD_USER_CAPTURE, NULL, 0);
}

esp_err_t comm_uart_cmd_add_user_cancel(void)
{
    return comm_uart_send_cmd(CMD_ADD_USER_CANCEL, NULL, 0);
}

esp_err_t comm_uart_cmd_sub_user(uint16_t user_id)
{
    payload_sub_user_t p = { .user_id = user_id };
    return comm_uart_send_cmd(CMD_SUB_USER, &p, sizeof(p));
}

esp_err_t comm_uart_cmd_clear_all_users(void)
{
    return comm_uart_send_cmd(CMD_CLEAR_ALL_USERS, NULL, 0);
}

esp_err_t comm_uart_cmd_list_users_request(void)
{
    return comm_uart_send_cmd(CMD_LIST_USER_REQ, NULL, 0);
}

esp_err_t comm_uart_cmd_start_stream(void)
{
    return comm_uart_send_cmd(CMD_START_STREAM, NULL, 0);
}

esp_err_t comm_uart_cmd_stop_stream(void)
{
    return comm_uart_send_cmd(CMD_STOP_STREAM, NULL, 0);
}

esp_err_t comm_uart_cmd_enable_liveness_check(bool enable)
{
    payload_bool_t p = { .enable = (uint8_t)(enable ? 1 : 0) };
    return comm_uart_send_cmd(CMD_ENABLE_LIVENESS_CHECK, &p, sizeof(p));
}
