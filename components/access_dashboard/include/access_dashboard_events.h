// access_dashboard_events.h
//
// Header noi bo (dung chung giua access_dashboard_events.c va
// access_dashboard_http.c trong cung component). Giu bo dem anh JPEG gan day
// trong RAM va ham dung chung de dung thanh JSON lich su.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t access_dashboard_events_init(void);

// Dung mang JSON (cJSON*) chua toi da `limit` su kien gan nhat, kem co "has_image".
// Nguoi goi chiu trach nhiem cJSON_Delete() ket qua tra ve.
cJSON *access_dashboard_build_history_json(size_t limit);

// Anh co san khong (trong RAM hoac tren the SD)? Khong sao chep du lieu.
bool access_dashboard_has_image(uint32_t event_id);

// Sao chep du lieu anh JPEG ung voi event_id (tu bo dem RAM, neu khong co thi doc tu the SD).
// Neu tim thay: tra ve ESP_OK, cap phat *out_copy bang malloc() - NGUOI GOI
// PHAI free() sau khi dung xong. Neu khong tim thay: tra ve ESP_ERR_NOT_FOUND.
esp_err_t access_dashboard_copy_image(uint32_t event_id, uint8_t **out_copy, size_t *out_len);

#ifdef __cplusplus
}
#endif
