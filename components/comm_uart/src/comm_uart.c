#include "comm_uart.h"
#include "nav_event.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "comm_uart";
static QueueHandle_t s_nav_queue = NULL;
static bool s_uart_ready = false;

static int read_byte(TickType_t timeout)
{
    if (!s_uart_ready) return -1;
    uint8_t b = 0;
    return uart_read_bytes(UART_NUM_0, &b, 1, timeout) == 1 ? b : -1;
}

static void push(nav_event_t ev)
{
    if (s_nav_queue) xQueueSend(s_nav_queue, &ev, 0);
}

// Noi nay la CHO DUY NHAT biet ky tu tho nghia la gi.
// Sau nay node thuc gui giao thuc khac, chi sua trong ham nay.
static void rx_task(void *arg)
{
    static bool ignore_lf_after_cr = false;

    while (true) {
        int key = read_byte(pdMS_TO_TICKS(20));
        if (key < 0) continue;

        if (key == '\n' && ignore_lf_after_cr) { ignore_lf_after_cr = false; continue; }
        ignore_lf_after_cr = (key == '\r');

        switch (key) {
            case 'w': case 'W': push(NAV_PREV); ESP_LOGI(TAG, "Key 'w' -> NAV_PREV"); break;
            case 's': case 'S': push(NAV_NEXT); ESP_LOGI(TAG, "Key 's' -> NAV_NEXT"); break;
            case 'a': case 'A': push(NAV_DEC);  ESP_LOGI(TAG, "Key 'a' -> NAV_DEC");  break;
            case 'd': case 'D': push(NAV_INC);  ESP_LOGI(TAG, "Key 'd' -> NAV_INC");  break;
            case '\n': case '\r': push(NAV_SELECT); ESP_LOGI(TAG, "Key Enter -> NAV_SELECT"); break;
            case 'b': case 'B': case 0x08: case 0x7f: push(NAV_BACK); ESP_LOGI(TAG, "Key 'b' -> NAV_BACK"); break;
            case 0x1b: {
                int c1 = read_byte(pdMS_TO_TICKS(100));
                if (c1 != '[') break;
                int c2 = read_byte(pdMS_TO_TICKS(100));
                while (c2 >= '0' && c2 <= '9') c2 = read_byte(pdMS_TO_TICKS(100));
                if (c2 == 'A') { push(NAV_PREV); ESP_LOGI(TAG, "Arrow Up -> NAV_PREV"); }
                if (c2 == 'B') { push(NAV_NEXT); ESP_LOGI(TAG, "Arrow Down -> NAV_NEXT"); }
                if (c2 == 'D') { push(NAV_DEC);  ESP_LOGI(TAG, "Arrow Left -> NAV_DEC"); }
                if (c2 == 'C') { push(NAV_INC);  ESP_LOGI(TAG, "Arrow Right -> NAV_INC"); }
                break;
            }
            default: break;
        }
    }
}

QueueHandle_t comm_uart_init(void)
{
    esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    s_uart_ready = (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
    if (!s_uart_ready) {
        ESP_LOGW(TAG, "Khong khoi tao duoc UART: %s", esp_err_to_name(err));
    }

    s_nav_queue = xQueueCreate(8, sizeof(nav_event_t));
    xTaskCreate(rx_task, "uart_rx_task", 3072, NULL, 3, NULL);
    return s_nav_queue;
}