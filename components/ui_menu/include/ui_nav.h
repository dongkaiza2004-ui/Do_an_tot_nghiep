#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_nav_init(void);
void ui_nav_task(void *arg);

#ifdef __cplusplus
}
#endif