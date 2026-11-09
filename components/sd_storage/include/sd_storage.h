// sd_storage.h
//
// Luu anh JPEG va lich su su kien len the SD (SDMMC 4-bit) de khong ton RAM
// va khong mat khi reset/mat dien.
//
// THIET KE: SD la tuy chon. Neu the khong cam / loi mount thi sd_storage_init()
// tra ve loi va cac component khac tu dong dung RAM nhu truoc day.
//
// Cau truc tren the:
//   /sdcard/img/XXXXXXXX.jpg   anh, ten = event_id dang HEX 8 ky tu (hop 8.3)
//   /sdcard/events.log         lich su, moi dong 1 su kien (CSV)
//   /sdcard/events.old         file log cu (sau khi xoay vong)

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "access_events.h"

// ---- CHAN SDMMC (theo so do mach) -------------------------------------------
#define SD_STORAGE_PIN_CLK   GPIO_NUM_40
#define SD_STORAGE_PIN_CMD   GPIO_NUM_41
#define SD_STORAGE_PIN_D0    GPIO_NUM_39
#define SD_STORAGE_PIN_D1    GPIO_NUM_38
#define SD_STORAGE_PIN_D2    GPIO_NUM_2
#define SD_STORAGE_PIN_D3    GPIO_NUM_42

// ---- GIOI HAN ----------------------------------------------------------------
#define SD_STORAGE_MOUNT_POINT      "/sdcard"
#define SD_STORAGE_MAX_IMAGES       1000            // vuot qua -> xoa anh cu nhat
#define SD_STORAGE_LOG_ROTATE_BYTES (256 * 1024)    // vuot qua -> xoay vong log
// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Mount the SD (KHONG format neu loi, de tranh xoa du lieu cua nguoi dung).
// The phai duoc format san FAT32. An toan khi goi nhieu lan.
esp_err_t sd_storage_init(void);

bool sd_storage_is_mounted(void);

// Luu anh. Ghi ra file tam roi doi ten, nen mat dien giua chung khong de lai
// anh cut nua chung. Goi tu task binh thuong (co the chan vai chuc ms).
esp_err_t sd_storage_save_image(uint32_t event_id, const uint8_t *data, size_t len);

bool sd_storage_image_exists(uint32_t event_id);

// Doc anh vao bo nho malloc(). NGUOI GOI PHAI free().
esp_err_t sd_storage_read_image(uint32_t event_id, uint8_t **out, size_t *out_len);

// Ghi 1 su kien vao log. KHONG chan: chi dua vao hang doi, task rieng se ghi.
esp_err_t sd_storage_log_event(const access_event_t *event);

// Doc toi da max_count su kien gan nhat tu log, THU TU CU -> MOI.
size_t sd_storage_load_recent_events(access_event_t *out, size_t max_count);

#ifdef __cplusplus
}
#endif
