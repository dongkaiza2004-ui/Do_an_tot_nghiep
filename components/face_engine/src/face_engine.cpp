// face_engine.cpp
//
// XEM GHI CHU O DAU face_engine.h VE RUI RO SAI LECH TEN HAM/ENUM CUA
// human_face_detect / human_face_recognition SO VOI BAN THUC TE BAN KEO VE.
//
// Vi thu vien esp-dl human_face_recognition (theo tim hieu) chi luu ID + vector
// dac trung, KHONG luu ten nguoi dung, nen file nay tu quan ly rieng 1 bang
// anh xa "user_id -> name" luu trong 1 file don gian tren SPIFFS
// (/spiffs/user_names.bin), doc/ghi lai toan bo moi khi co thay doi (du du
// cho quy mo vai chuc nguoi dung).
#include "face_engine.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "img_converters.h"     // fmt2jpg() - tu esp32-camera, chuyen RGB565 tho -> JPEG
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
#include <vector>

static const char *TAG = "face_engine";

#define USER_NAME_DB_PATH   "/spiffs/user_names.bin"
#define MAX_USERS           32
#define EVENT_COOLDOWN_MS   10000
#define EVENT_POS_TOLERANCE 24     // px, coi la "cung 1 khuon mat" neu tam box lech duoi nguong nay
#define JPEG_QUALITY         30    // 0-100, thap hon = nen nhieu hon/nhe hon nhung xau hon
#define RECOG_FRAME_INTERVAL  50    // Số frame cách nhau giữa mỗi lần nhận diện (10 lần detect mới recognize 1 lần)

static HumanFaceDetect      *s_detect     = nullptr;
static HumanFaceRecognizer  *s_recognizer = nullptr;
static QueueHandle_t         s_event_queue = nullptr;
static uint32_t              s_frame_counter = 0;

// ---- Bang anh xa id -> ten, luu don gian tren SPIFFS ----
typedef struct { uint16_t user_id; char name[FACE_ENGINE_NAME_LEN]; bool used; } user_name_entry_t;
static user_name_entry_t s_users[MAX_USERS];

static void user_db_load(void)
{
    memset(s_users, 0, sizeof(s_users));
    FILE *f = fopen(USER_NAME_DB_PATH, "rb");
    if (!f) { ESP_LOGW(TAG, "Chua co user_names.bin, bat dau tu rong"); return; }
    fread(s_users, sizeof(s_users), 1, f);
    fclose(f);
}
static void user_db_save(void)
{
    FILE *f = fopen(USER_NAME_DB_PATH, "wb");
    if (!f) { ESP_LOGE(TAG, "Khong ghi duoc user_names.bin"); return; }
    fwrite(s_users, sizeof(s_users), 1, f);
    fclose(f);
}
static const char* user_db_find_name(uint16_t id)
{
    for (int i = 0; i < MAX_USERS; i++)
        if (s_users[i].used && s_users[i].user_id == id) return s_users[i].name;
    return "Unknown";
}
static void user_db_add(uint16_t id, const char *name)
{
    for (int i = 0; i < MAX_USERS; i++) {
        if (!s_users[i].used) {
            s_users[i].used = true;
            s_users[i].user_id = id;
            strncpy(s_users[i].name, name, FACE_ENGINE_NAME_LEN - 1);
            user_db_save();
            return;
        }
    }
    ESP_LOGW(TAG, "Bang ten day (MAX_USERS=%d), khong them duoc", MAX_USERS);
}
static bool user_db_remove(uint16_t id)
{
    for (int i = 0; i < MAX_USERS; i++) {
        if (s_users[i].used && s_users[i].user_id == id) {
            s_users[i].used = false;
            user_db_save();
            return true;
        }
    }
    return false;
}

// ---- Cooldown chong spam su kien ----
typedef struct { face_event_type_t type; uint16_t user_id; int cx, cy; int64_t last_ms; bool used; } cooldown_slot_t;
static cooldown_slot_t s_cooldown[FACE_ENGINE_MAX_RESULTS];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool cooldown_should_emit(face_event_type_t type, uint16_t user_id, int cx, int cy)
{
    int64_t t = now_ms();
    int free_slot = -1;
    for (int i = 0; i < FACE_ENGINE_MAX_RESULTS; i++) {
        if (!s_cooldown[i].used) { if (free_slot < 0) free_slot = i; continue; }
        int dx = s_cooldown[i].cx - cx, dy = s_cooldown[i].cy - cy;
        bool same_pos = (dx * dx + dy * dy) < (EVENT_POS_TOLERANCE * EVENT_POS_TOLERANCE);
        if (s_cooldown[i].type == type && s_cooldown[i].user_id == user_id && same_pos) {
            if (t - s_cooldown[i].last_ms < EVENT_COOLDOWN_MS) return false; // van con cooldown
            s_cooldown[i].last_ms = t; s_cooldown[i].cx = cx; s_cooldown[i].cy = cy;
            return true;
        }
    }
    // khong tim thay slot cu khop -> tao slot moi (ghi de slot trong hoac slot 0 neu day)
    int idx = (free_slot >= 0) ? free_slot : 0;
    s_cooldown[idx] = { type, user_id, cx, cy, t, true };
    return true;
}

// ---- Enroll 2 buoc: dong bo hoa qua semaphore vi lenh CAPTURE den tu task UART,
//      con enroll thuc su chi thuc hien duoc trong task xu ly frame (co anh moi nhat) ----
static SemaphoreHandle_t s_capture_done_sem = nullptr;
static volatile bool     s_capture_requested = false;
static volatile bool     s_last_capture_ok   = false;

esp_err_t face_engine_init(void)
{
    s_detect = new HumanFaceDetect();
    // Model mac dinh lay tu app_state (co the da duoc load tu NVS)
    // TODO: kiem tra constructor thuc te cua HumanFaceRecognizer co nhan
    // model_type_t hay khong theo dung version esp-dl da keo ve; neu API
    // khac, sua doan khoi tao nay cho khop.
    s_recognizer = new HumanFaceRecognizer("/spiffs/face.db");
    s_capture_done_sem = xSemaphoreCreateBinary();
    user_db_load();
    memset(s_cooldown, 0, sizeof(s_cooldown));
    ESP_LOGI(TAG, "face_engine init OK");
    return ESP_OK;
}

void face_engine_set_event_queue(void *queue_handle)
{
    s_event_queue = (QueueHandle_t)queue_handle;
}

void face_engine_switch_model(recog_model_t model)
{
    // TODO: xac nhan enum model_type_t thuc te cua HumanFaceFeat/HumanFaceRecognizer
    // sau khi build lan dau roi map recog_model_t (app_state.h) sang dung enum do.
    delete s_recognizer;
    s_recognizer = new HumanFaceRecognizer("/spiffs/face.db"); // TODO: truyen them model type
    app_state_set_model(model);
    ESP_LOGI(TAG, "Da doi model nhan dien");
}

void face_engine_set_threshold(float threshold)
{
    // TODO: goi dung ham cua thu vien, vi du s_recognizer->set_thresh(threshold);
    app_state_set_threshold(threshold);
}

void face_engine_add_user_start(uint16_t user_id, const char *name)
{
    app_state_begin_enroll(user_id, name);
}

bool face_engine_add_user_capture(void)
{
    if (!app_state_is_enrolling()) {
        ESP_LOGW(TAG, "add_user_capture duoc goi nhung khong o trong che do enroll");
        return false;
    }
    s_capture_requested = true;
    // Cho toi da 2s de task xu ly frame thuc hien enroll xong
    if (xSemaphoreTake(s_capture_done_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout cho enroll capture");
        s_capture_requested = false;
        return false;
    }
    return s_last_capture_ok;
}

void face_engine_add_user_cancel(void)
{
    s_capture_requested = false;
    app_state_cancel_enroll();
}

bool face_engine_delete_user(uint16_t user_id)
{
    // TODO: goi them ham xoa feature vector trong thu vien nhan dien, vi du
    // s_recognizer->delete_id(user_id); - ten ham can doi chieu voi header thuc te.
    return user_db_remove(user_id);
}

int face_engine_list_users(user_info_t *out_list, int max_count)
{
    int n = 0;
    for (int i = 0; i < MAX_USERS && n < max_count; i++) {
        if (s_users[i].used) {
            out_list[n].user_id = s_users[i].user_id;
            strncpy(out_list[n].name, s_users[i].name, FACE_ENGINE_NAME_LEN - 1);
            n++;
        }
    }
    return n;
}

int face_engine_process_frame(uint16_t *rgb565_buf, int width, int height,
                               face_result_t *out_results)
{
    dl::image::img_t img;
    img.data = rgb565_buf;
    img.width = width;
    img.height = height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

    auto &results = s_detect->run(img);
    int count = 0;

    for (auto &res : results) {
        if (count >= FACE_ENGINE_MAX_RESULTS) break;
        out_results[count].x1 = res.box[0];
        out_results[count].y1 = res.box[1];
        out_results[count].x2 = res.box[2];
        out_results[count].y2 = res.box[3];
        out_results[count].matched = false;
        out_results[count].user_id = 0xFFFF;
        strncpy(out_results[count].name, "Unknown", FACE_ENGINE_NAME_LEN - 1);
        out_results[count].similarity = 0.0f;
        count++;
    }

    // --- Xu ly enroll neu co yeu cau capture dang cho ---
    if (s_capture_requested && !results.empty()) {
        enroll_state_t st = app_state_get_enroll_state();
        int id = s_recognizer->enroll(img, results);
        s_last_capture_ok = (id >= 0);
        if (s_last_capture_ok) {
            user_db_add(st.user_id, st.user_name);
            ESP_LOGI(TAG, "Enroll OK, user_id=%d ten=%s", st.user_id, st.user_name);
        } else {
            ESP_LOGE(TAG, "Enroll that bai");
        }
        s_capture_requested = false;
        app_state_cancel_enroll();
        xSemaphoreGive(s_capture_done_sem);
        return count; // bo qua buoc recognize/event trong chinh frame dung de enroll
    }

    // --- Nhan dien + phat su kien (chi xet khuon mat dau tien, giong logic goc) ---
    if (!results.empty() && !app_state_is_enrolling()) {
        s_frame_counter++;
        if ((s_frame_counter % RECOG_FRAME_INTERVAL) == 0) {
            auto rec_res = s_recognizer->recognize(img, results);
            face_event_type_t evt_type = FACE_EVENT_STRANGER;
            uint16_t evt_user_id = 0xFFFF;
            const char *evt_name = "Unknown";

            if (!rec_res.empty() && rec_res[0].similarity >= app_state_get_threshold()) {
                evt_type = FACE_EVENT_LOGIN_SUCCESS;
                evt_user_id = rec_res[0].id;
                evt_name = user_db_find_name(evt_user_id);
                out_results[0].matched = true;
                out_results[0].user_id = evt_user_id;
                out_results[0].similarity = rec_res[0].similarity;
                strncpy(out_results[0].name, evt_name, FACE_ENGINE_NAME_LEN - 1);
            }

            ESP_LOGI(TAG, "Ket qua recognize [%s] - Matched: %s, ID: %d, Ten: %s, Score: %.2f",
                     (evt_type == FACE_EVENT_LOGIN_SUCCESS) ? "SUCCESS" : "STRANGER",
                     out_results[0].matched ? "YES" : "NO",
                     evt_user_id,
                     evt_name,
                     out_results[0].similarity);

            int cx = (out_results[0].x1 + out_results[0].x2) / 2;
            int cy = (out_results[0].y1 + out_results[0].y2) / 2;

            if (s_event_queue && cooldown_should_emit(evt_type, evt_user_id, cx, cy)) {
                uint8_t *jpg_buf = nullptr;
                size_t   jpg_len = 0;
                if (fmt2jpg((uint8_t *)rgb565_buf, (size_t)width * height * 2,
                            width, height, PIXFORMAT_RGB565, JPEG_QUALITY, &jpg_buf, &jpg_len)) {
                    face_event_t evt = {};
                    evt.type = evt_type;
                    evt.user_id = evt_user_id;
                    strncpy(evt.name, evt_name, FACE_ENGINE_NAME_LEN - 1);
                    evt.jpeg_data = jpg_buf;   // comm_espnow phai free() sau khi gui xong
                    evt.jpeg_len = jpg_len;
                    if (xQueueSend(s_event_queue, &evt, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "event_queue day, bo qua su kien nay");
                        free(jpg_buf);
                    }
                } else {
                    ESP_LOGE(TAG, "Nen JPEG that bai cho anh su kien");
                }
            }
        }
        else if(results.empty()){
            s_frame_counter = 0;
        }
    }

    return count;
}
