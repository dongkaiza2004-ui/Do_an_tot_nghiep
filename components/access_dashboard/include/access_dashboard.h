// access_dashboard.h
//
// HTTP server phuc vu Web Dashboard / App Qt: lich su truy cap, anh su kien,
// va cac lenh cau hinh mo rong ngoai chuan Matter (camera, nguoi dung nhan
// dien, chong gia mao...). KHONG dieu khien khoa truc tiep - viec do da co
// Matter (Google Home/Apple Home) lo, tranh 2 duong dieu khien khoa chong
// cheo nhau.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t access_dashboard_start(void);
void access_dashboard_stop(void);

//==========================================================================
// Dang ky nguoi dung = khuon mat (Node A) + PIN (khoa Matter tren Node B)
//
// Dashboard khong biet gi ve Matter: phan ghi vao khoa do main/ cung cap qua cac ham duoi day
// (xem main/lock/door_lock_provision.cpp). Neu KHONG dang ky bo nay thi dashboard chi dang ky khuon mat
// nhu truoc va khong can PIN.
//==========================================================================

// Bo dem (gom ky tu NUL) ma dashboard giu tam PIN trong RAM, xoa ngay sau khi dung xong
#define ACCESS_USER_PIN_BUF 16
#define ACCESS_USER_ADD_FACE_ONLY 1

typedef enum {
    ACCESS_USER_PROV_OK = 0,
    ACCESS_USER_PROV_ERR_EXISTS,               // user_id da co tren khoa
    ACCESS_USER_PROV_ERR_CREDENTIAL_SLOT_BUSY, // o luu PIN tuong ung da duoc dung
    ACCESS_USER_PROV_ERR_PIN_IN_USE,           // PIN da thuoc ve nguoi khac
    ACCESS_USER_PROV_ERR_STORAGE,              // loi ghi kho nguoi dung cua khoa
} access_user_prov_result_t;

typedef struct {
    uint16_t max_user_index;                   // user_id hop le: 1..max_user_index
    uint8_t pin_min_len;                       // so chu so PIN toi thieu
    uint8_t pin_max_len;                       // so chu so PIN toi da (< ACCESS_USER_PIN_BUF)
    bool (*user_exists)(uint16_t user_index);  // true neu nguoi dung da co tren khoa (bat hoac vo hieu hoa) (co the NULL)
    bool (*credential_slot_busy)(uint16_t user_index); // true neu o PIN cua user_index da bi chiem (co the NULL)
    bool (*pin_in_use)(const char *pin);       // true neu PIN da thuoc ve nguoi khac (co the NULL)
    access_user_prov_result_t (*register_user)(uint16_t user_index, const char *name, const char *pin);
    bool (*unregister_user)(uint16_t user_index); // true neu sau khi goi, nguoi dung khong con tren khoa (co the NULL)
} access_user_provisioner_t;

// Goi truoc access_dashboard_start(). NULL = tat tinh nang PIN.
esp_err_t access_dashboard_set_user_provisioner(const access_user_provisioner_t *provisioner);

#ifdef __cplusplus
}
#endif
