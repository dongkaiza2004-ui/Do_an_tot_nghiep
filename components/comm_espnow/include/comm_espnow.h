// comm_espnow.h
//
// Lop nhan anh su kien qua ESP-NOW phia Node B. Khac voi ben camera (chi
// gui), Node B CHI NHAN va tu ghep cac fragment JPEG lai theo event_id.
//
// QUAN TRONG - Wi-Fi channel: ESP-NOW chi hoat dong khi 2 thiet bi dang o
// CUNG mot kenh Wi-Fi. Node B dung kenh do AP/router thuc te cap sau khi
// Matter ket noi thanh cong (kenh nay co the thay doi tuy router, KHONG duoc
// tu y ep lai tu component nay vi se lam gay ket noi Matter dang chay). Camera
// (Node A) hien dang chay Wi-Fi STA khong ket noi AP nao nen mac dinh o kenh 1.
// => Phai co dinh kenh cua router that su ve mot gia tri va cau hinh lai
// camera dung dung kenh do (esp_wifi_set_channel ngay sau esp_wifi_start ben
// Node A), neu khong 2 ben se khong bao gio nhan duoc ESP-NOW cua nhau dù
// Matter/Wi-Fi cua Node B van hoat dong binh thuong (rat de gay nham lan khi
// debug vi trieu chung la "im lang" chu khong bao loi ro rang).

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Goi khi mot anh JPEG da duoc ghep day du tu tat ca fragment.
// jpeg_data chi hop le trong pham vi callback - neu can giu lai lau hon phai
// tu memcpy sang bo nho rieng (xem access_dashboard_events.c lam vi du).
typedef void (*comm_espnow_image_cb_t)(uint32_t event_id, uint16_t width, uint16_t height,
                                        const uint8_t *jpeg_data, size_t jpeg_len, void *ctx);

// Khoi tao ESP-NOW nhan anh.
//
// LUU Y BAT BUOC: KHONG duoc goi esp_netif_init()/esp_wifi_init()/esp_wifi_start()
// o day hay o noi goi ham nay - Matter (esp_matter::start()) da khoi tao va
// dang so huu Wi-Fi driver roi. Goi lai cac ham do se lam gay ket noi Wi-Fi
// cua Matter. Chi goi ham nay SAU KHI esp_matter::start() da tra ve thanh cong.
esp_err_t comm_espnow_init(void);

// Bat task ghep manh anh chay nen (mot task rieng, khong chay trong callback
// cua Wi-Fi driver de tranh lam nghen stack Wi-Fi).
void comm_espnow_start(void);

// Dang ky nhan cac anh da ghep xong. Toi da vai listener dong thoi
// (xem COMM_ESPNOW_MAX_LISTENERS trong comm_espnow.c).
esp_err_t comm_espnow_register_image_handler(comm_espnow_image_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
