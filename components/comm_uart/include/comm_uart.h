// comm_uart.h
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nav_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UART_MODE_NAV,     // dieu huong menu (mac dinh) - 2/4/6/8 la huong
    UART_MODE_DIGIT,   // dang nhap so vao 1 o nao do - 2/4/6/8 la chu so
} uart_input_mode_t;

QueueHandle_t comm_uart_init(void);
void comm_uart_set_mode(uart_input_mode_t mode);

#ifdef __cplusplus
}
#endif