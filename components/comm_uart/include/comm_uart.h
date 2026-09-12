#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nav_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UART_MODE_NAV,    // 2/4/6/8 la huong di chuyen menu
    UART_MODE_TEXT,   // moi byte nhan duoc la 1 ky tu that, ghi thang vao o dang nhap
} uart_input_mode_t;

QueueHandle_t comm_uart_init(void);
void comm_uart_set_mode(uart_input_mode_t mode);

#ifdef __cplusplus
}
#endif