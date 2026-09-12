#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t comm_uart_init(void);

#ifdef __cplusplus
}
#endif