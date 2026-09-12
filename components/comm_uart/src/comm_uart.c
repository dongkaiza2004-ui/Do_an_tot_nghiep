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
    ESP_LOGI(TAG, "Doi che do input -> %s", mode == UART_MODE_NAV ? "NAV" : "TEXT");
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

// Node ban phim vat ly da tu lo multi-tap va gui THANG ky tu that (a-z, 0-9...)
// qua UART, kem Enter/Back rieng cho 2 lenh dieu khien. Node nay (ESP32-S3/TFT)
// chi can phan biet dung 2 truong hop: dang o menu (NAV) hay dang nhap 1 o (TEXT).
static void rx_task(void *arg)
{
    static bool ignore_lf_after_cr = false;

    while (true) {
        int key = read_byte(pdMS_TO_TICKS(20));
        if (key < 0) continue;

        if (key == '\n' && ignore_lf_after_cr) { ignore_lf_after_cr = false; continue; }
        ignore_lf_after_cr = (key == '\r');

        // Enter/Back LUON la lenh dac biet, khong bao gio bi hieu la ky tu thuong,
        // du dang o che do NAV hay TEXT.
        if (key == '\n' || key == '\r') { push(NAV_SELECT, 0); continue; }
        if (key == 0x08 || key == 0x7f) { push(NAV_BACK, 0); continue; }

        if (s_mode == UART_MODE_TEXT) {
            // Dang nhap 1 o - nhan MOI ky tu con lai nguyen van, ghi thang vao o
            push(NAV_CHAR, (char)key);
            continue;
        }

        // ---- UART_MODE_NAV: chi 2/4/6/8 co nghia, cac phim khac bo qua ----
        switch (key) {
            case '2': push(NAV_PREV, 0); break;
            case '8': push(NAV_NEXT, 0); break;
            case '4': push(NAV_DEC, 0);  break;
            case '6': push(NAV_INC, 0);  break;
            default: break;
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