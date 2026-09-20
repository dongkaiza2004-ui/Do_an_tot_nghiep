// light_ctrl.h

#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//==================================================
// Light Control Configuration
//==================================================

#define LIGHT_LED_GPIO  38 

//==================================================
// Initialization
//==================================================

esp_err_t light_ctrl_init(void);

//==================================================
// Light Control
//==================================================

void light_ctrl_start(void);

#ifdef __cplusplus
}
#endif