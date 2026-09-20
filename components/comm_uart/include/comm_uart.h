// comm_uart.h

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif


//==================================================
// Khởi tạo
//==================================================

esp_err_t comm_uart_init(
    uart_port_t port,
    int tx_pin,
    int rx_pin,
    int baud_rate
);


//==================================================
// UART Communication
//==================================================

void comm_uart_start(void);


//==================================================
// Response
//==================================================

void comm_uart_send_response(
    uint8_t rsp_cmd,
    const void *payload,
    uint8_t len
);


#ifdef __cplusplus
}
#endif