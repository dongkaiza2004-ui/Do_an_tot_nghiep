// light_ctrl.h
// Doc cam bien anh sang, tu bat/tat den khi troi toi. Chay doc lap, khong phu
// thuoc vao luong camera/AI.
//
// CHUA CHOT LOAI CAM BIEN: file nay mac dinh dung LDR qua ADC (don gian, re,
// pho bien). Neu ban dung cam bien so qua I2C (vd BH1750), doi phan
// "light_ctrl_read_raw()" trong .c sang goi driver I2C tuong ung.
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_LED_GPIO   38   // TODO: doi cho dung chan thuc te dieu khien den/relay

esp_err_t light_ctrl_init(void);
void      light_ctrl_start(void);   // tao task nen, chu ky doc vd moi 5s

#ifdef __cplusplus
}
#endif
