// comm_uart.c
#include "comm_uart.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "comm_uart";
static QueueHandle_t s_nav_queue = NULL;
static bool s_uart_ready = false;
static volatile uart_input_mode_t s_mode = UART_MODE_NAV;

void comm_uart_set_mode(uart_input_mode_t mode)
{
    s_mode = mode;
    ESP_LOGI(TAG, "Doi che do input -> %s", mode == UART_MODE_NAV ? "NAV" : "DIGIT");
}

static int read_byte(TickType_t timeout)
{
    if (!s_uart_ready) return -1;
    uint8_t b = 0;
    return uart_read_bytes(UART_NUM_0, &b, 1, timeout) == 1 ? b : -1;
}

static void push(nav_event_t type, char ch)
{
    nav_msg_t msg = { .type = type, .ch = ch };
    if (s_nav_queue) xQueueSend(s_nav_queue, &msg, 0);
}

static void rx_task(void *arg)
{
    static bool ignore_lf_after_cr = false;

    while (true) {
        int key = read_byte(pdMS_TO_TICKS(20));
        if (key < 0) continue;

        if (key == '\n' && ignore_lf_after_cr) { ignore_lf_after_cr = false; continue; }
        ignore_lf_after_cr = (key == '\r');

        // Enter/Back LUON co y nghia dac biet, khong bao gio bi hieu la chu so
        if (key == '\n' || key == '\r') {
            ESP_LOGI(TAG, "Key ENTER -> NAV_SELECT");
            push(NAV_SELECT, 0);
            continue;
        }
        if (key == 0x08 || key == 0x7f) {
            ESP_LOGI(TAG, "Key BACK -> NAV_BACK");
            push(NAV_BACK, 0);
            continue;
        }

        if (s_mode == UART_MODE_DIGIT) {
            if (key >= '0' && key <= '9') {
                ESP_LOGI(TAG, "Key '%c' -> NAV_CHAR (dang nhap so)", key);
                push(NAV_CHAR, (char)key);
            }
            continue;   // trong che do nhap so, 2/4/6/8 KHONG con la huong nua
        }

        // ---- UART_MODE_NAV ----
        switch (key) {
            case '2': ESP_LOGI(TAG, "Key '2' -> NAV_PREV"); push(NAV_PREV, 0); break;
            case '8': ESP_LOGI(TAG, "Key '8' -> NAV_NEXT"); push(NAV_NEXT, 0); break;
            case '4': ESP_LOGI(TAG, "Key '4' -> NAV_DEC");  push(NAV_DEC, 0);  break;
            case '6': ESP_LOGI(TAG, "Key '6' -> NAV_INC");  push(NAV_INC, 0);  break;
            default: break;  // 0,1,3,5,7,9 khong co nghia khi dang dieu huong
        }
    }
}

QueueHandle_t comm_uart_init(void)
{
    esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    s_uart_ready = (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
    if (!s_uart_ready) ESP_LOGW(TAG, "Khong khoi tao duoc UART: %s", esp_err_to_name(err));

    s_nav_queue = xQueueCreate(8, sizeof(nav_msg_t));
    xTaskCreate(rx_task, "uart_rx_task", 3072, NULL, 3, NULL);
    return s_nav_queue;
}