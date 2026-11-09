// access_events.h
//
// Lop "bus" trung gian, KHONG phu thuoc Matter/BoltLockManager, dung de
// access_gateway (nam trong main/, co quyen truy cap BoltLockManager va
// DoorLockStorage vi la header rieng cua component main) cong bo ket qua
// quyet dinh mo khoa cho access_dashboard (mot component tach biet, HTTP/log)
// ma khong can access_dashboard include bat ky header private nao cua main.
//
// Neu khong co lop trung gian nay, access_dashboard se phai REQUIRES thang
// vao "main" de doc bolt_lock_manager.h/door_lock_storage.h - dieu nay khong
// lam duoc trong ESP-IDF vi cac header do chi duoc khai bao PRIV_INCLUDE_DIRS
// (rieng tu) cua component main, khong lo ra ngoai cho component khac dung.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACCESS_EVENT_GRANTED = 0,      // Nhan dien khop, user hop le -> da mo khoa
    ACCESS_EVENT_DENIED,           // Nhan dien khop nhung user khong hop le (vo hieu hoa/khong map duoc)
    ACCESS_EVENT_UNKNOWN_FACE,     // Khong nhan dien duoc khuon mat nao
    ACCESS_EVENT_LIVENESS_FAIL,    // Nghi ngo gia mao (khong vuot qua challenge-response)
} access_event_type_t;

typedef struct {
    uint32_t event_id;             // Trung voi event_id ben UART/ESP-NOW de doi chieu anh
    access_event_type_t type;
    uint16_t face_user_id;         // Ma nguoi dung noi bo cua camera (0 neu khong ap dung, vd unknown/liveness_fail)
    uint16_t matter_user_index;    // Chi so user tuong ung tren Matter cluster (0 neu khong map duoc)
    float similarity;              // Do tin cay nhan dien (0 neu khong ap dung)
    int64_t timestamp_us;          // Tu dong dien esp_timer_get_time() neu de = 0 khi publish
    int64_t unix_time_s;           // Gio that UTC (giay). Tu dien khi publish neu da dong bo SNTP, 0 neu chua biet
} access_event_t;

typedef void (*access_events_cb_t)(const access_event_t *event, void *ctx);

// Goi 1 lan truoc khi dung (an toan khi goi nhieu lan).
esp_err_t access_events_init(void);

// Cong bo 1 su kien - se duoc luu vao lich su noi bo (vong tron, xem
// access_events_get_recent) va goi toi tat ca subscriber da dang ky.
esp_err_t access_events_publish(const access_event_t *event);

// Dang ky nhan MOI su kien duoc publish (toi da vai subscriber dong thoi).
esp_err_t access_events_subscribe(access_events_cb_t cb, void *ctx);

// Nap lai lich su da luu truoc do (vd doc tu the SD luc khoi dong). events theo
// thu tu CU -> MOI. KHONG goi subscriber. Goi truoc khi co su kien moi.
esp_err_t access_events_restore(const access_event_t *events, size_t count);


// Lay toi da max_count su kien gan nhat, moi nhat truoc. Tra ve so phan tu
// thuc su duoc chep vao out.
size_t access_events_get_recent(access_event_t *out, size_t max_count);

#ifdef __cplusplus
}
#endif
