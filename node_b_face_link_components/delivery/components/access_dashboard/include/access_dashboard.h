// access_dashboard.h
//
// HTTP server phuc vu Web Dashboard / App Qt: lich su truy cap, anh su kien,
// va cac lenh cau hinh mo rong ngoai chuan Matter (camera, nguoi dung nhan
// dien, chong gia mao...). KHONG dieu khien khoa truc tiep - viec do da co
// Matter (Google Home/Apple Home) lo, tranh 2 duong dieu khien khoa chong
// cheo nhau.

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi dong server (dang ky lam listener/subscriber cho access_events va
// comm_espnow, roi mo httpd). Goi sau khi comm_uart_init()/comm_espnow_init()
// da chay xong.
esp_err_t access_dashboard_start(void);

void access_dashboard_stop(void);

#ifdef __cplusplus
}
#endif
