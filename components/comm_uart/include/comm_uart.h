// comm_uart.h
#pragma once
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t comm_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud_rate);
void      comm_uart_start(void);   // tao task nen: doc byte, rap khung, validate checksum, dispatch lenh

// Cho phep noi khac (vd task bao cao FPS dinh ky trong app_main) gui 1 khung phan hoi.
void comm_uart_send_response(uint8_t rsp_cmd, const void *payload, uint8_t len);

#ifdef __cplusplus
}
#endif
