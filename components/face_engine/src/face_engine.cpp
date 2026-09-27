// face_engine.cpp

#include "face_engine.h"

#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "img_converters.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

//==================================================
// Logging
//==================================================

static const char *TAG = "face_engine";

//==================================================
// Configuration
//==================================================

static char s_names_path[40] = "/spiffs/user_names_mfn.bin";
#define MAX_USERS               32
#define EVENT_COOLDOWN_MS       10000
#define EVENT_POS_TOLERANCE     24
#define JPEG_QUALITY             30
#define RECOG_FRAME_INTERVAL     50
#define RESULT_PERSIST_FRAMES    40
#define LIVENESS_CHECK_FRAMES   20  
#define LIVENESS_YAW_THRESHOLD  10  

//==================================================
// Runtime State
//==================================================

static HumanFaceDetect     *s_detect = nullptr;
static HumanFaceRecognizer *s_recognizer = nullptr;
static QueueHandle_t s_event_queue = nullptr;
static uint32_t s_frame_counter = 0;
static uint32_t s_event_id_counter = 0;
static face_uart_report_cb_t s_uart_report_cb = nullptr;

//==================================================
// Recognition Result Persistence
//==================================================

static bool s_last_matched = false;
static uint16_t s_last_user_id = 0xFFFF;
static char s_last_name[FACE_ENGINE_NAME_LEN] = "Detecting";
static float s_last_similarity = 0.0f;
static int s_persist_left = 0;

static bool    s_liveness_active   = false;
static int     s_liveness_left     = 0;
static int     s_liveness_baseline = 0;
static bool    s_liveness_passed   = false;

static uint16_t s_pending_user_id;
static float    s_pending_similarity;
static char s_pending_name[FACE_ENGINE_NAME_LEN] = "";
static uint32_t s_pending_event_id;

//==================================================
// User Name Database
//==================================================

typedef struct {
    uint16_t user_id;
    uint16_t feat_id; 
    char name[FACE_ENGINE_NAME_LEN];
    bool used;
} user_name_entry_t;

static user_name_entry_t s_users[MAX_USERS];

//==================================================
// Set Database
//==================================================

static const char* names_path_for_model(recog_model_t model)
{
    return (model == RECOG_MODEL_MBF_S8_V1) ? "/spiffs/user_names_mbf.bin" : "/spiffs/user_names_mfn.bin";
}

//==================================================
// User Database
//==================================================

static void user_db_load(void)
{
    memset(s_users, 0, sizeof(s_users));
    FILE *f = fopen(s_names_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Chua co user_names.bin, bat dau tu rong");
        return;
    }
    fread(s_users, sizeof(s_users), 1, f);
    fclose(f);
}

static void user_db_save(void)
{
    FILE *f = fopen(s_names_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Khong ghi duoc user_names.bin");
        return;
    }
    fwrite(s_users, sizeof(s_users), 1, f);
    fclose(f);
}

static const char *user_db_find_name_by_feat_id(uint16_t feat_id, uint16_t *out_user_id)
{
    for (int i = 0; i < MAX_USERS; i++) {
        if (s_users[i].used && s_users[i].feat_id == feat_id) {
            if (out_user_id) *out_user_id = s_users[i].user_id;
            return s_users[i].name;
        }
    }
    if (out_user_id) *out_user_id = 0xFFFF;
    return "Unknown";
}

static void user_db_add(uint16_t id, uint16_t feat_id, const char *name)
{
    for (int i = 0; i < MAX_USERS; i++) {
        if (!s_users[i].used) {
            s_users[i].used = true;
            s_users[i].user_id = id;
            s_users[i].feat_id = feat_id;

            strncpy(s_users[i].name, name, FACE_ENGINE_NAME_LEN - 1);
            s_users[i].name[FACE_ENGINE_NAME_LEN - 1] = '\0';

            user_db_save();
            return;
        }
    }

    ESP_LOGW(TAG, "Bang ten day (MAX_USERS=%d), khong them duoc", MAX_USERS);
}
//==================================================
// Chống giả mạo
//==================================================

static int estimate_yaw(const std::vector<int> &kp)
{
    if (kp.size() < 10) return 0;
    int eye_mid_x = (kp[0] + kp[2]) / 2;
    return kp[4] - eye_mid_x;
}

//==================================================
// Chose model
//==================================================

static const char* db_path_for_model(recog_model_t model)
{
    return (model == RECOG_MODEL_MBF_S8_V1) ? "/spiffs/face_mbf.db" : "/spiffs/face_mfn.db";
}

//==================================================
// Event Cooldown
//==================================================

typedef struct {
    face_event_type_t type;
    uint16_t user_id;
    int cx;
    int cy;
    int64_t last_ms;
    bool used;
} cooldown_slot_t;

static cooldown_slot_t s_cooldown[FACE_ENGINE_MAX_RESULTS];
static int64_t now_ms(void){
    return esp_timer_get_time() / 1000;
}
static bool cooldown_should_emit(face_event_type_t type, uint16_t user_id, int cx, int cy)
{
    int64_t t = now_ms();
    int free_slot = -1;

    for (int i = 0; i < FACE_ENGINE_MAX_RESULTS; i++) {
        if (!s_cooldown[i].used) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }

        int dx = s_cooldown[i].cx - cx;
        int dy = s_cooldown[i].cy - cy;

        bool same_pos =
            (dx * dx + dy * dy) <
            (EVENT_POS_TOLERANCE * EVENT_POS_TOLERANCE);

        if (s_cooldown[i].type == type &&
            s_cooldown[i].user_id == user_id &&
            same_pos) {

            if (t - s_cooldown[i].last_ms < EVENT_COOLDOWN_MS) {
                return false;
            }

            s_cooldown[i].last_ms = t;
            s_cooldown[i].cx = cx;
            s_cooldown[i].cy = cy;

            return true;
        }
    }

    int idx = (free_slot >= 0) ? free_slot : 0;
    s_cooldown[idx] = {type, user_id, cx, cy, t, true};

    return true;
}

//==================================================
// Enrollment Synchronization
//==================================================

static SemaphoreHandle_t s_capture_done_sem = nullptr;
static volatile bool s_capture_requested = false;
static volatile bool s_last_capture_ok = false;

//==================================================
// Initialization
//==================================================

esp_err_t face_engine_init(void)
{
    s_detect = new HumanFaceDetect();
    recog_model_t model = app_state_get_model();
    s_recognizer = new HumanFaceRecognizer(db_path_for_model(model),
                                        (HumanFaceFeat::model_type_t)model);
    s_capture_done_sem = xSemaphoreCreateBinary();

    user_db_load();
    memset(s_cooldown, 0, sizeof(s_cooldown));

    ESP_LOGI(TAG, "face_engine init OK");

    return ESP_OK;
}

//==================================================
// Event Queue & UART Report
//==================================================

void face_engine_set_event_queue(void *queue_handle)
{
    s_event_queue = (QueueHandle_t)queue_handle;
}

void face_engine_set_uart_report_cb(face_uart_report_cb_t cb)
{
    s_uart_report_cb = cb;
}

//==================================================
// Recognition Model & Threshold
//==================================================

void face_engine_switch_model(recog_model_t model)
{
    delete s_recognizer;
    strncpy(s_names_path, names_path_for_model(model), sizeof(s_names_path) - 1);   // <-- THEM DONG NAY
    s_recognizer = new HumanFaceRecognizer(db_path_for_model(model),
                                            (HumanFaceFeat::model_type_t)model);
    user_db_load();
    app_state_set_model(model);
    ESP_LOGI(TAG, "Da doi model nhan dien -> %s",
             model == RECOG_MODEL_MBF_S8_V1 ? "MBF_S8_V1" : "MFN_S8_V1");
}

void face_engine_set_threshold(float threshold)
{
    app_state_set_threshold(threshold);
}

//==================================================
// User Enrollment
//==================================================

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

//==================================================
// User Management
//==================================================

bool face_engine_delete_user(uint16_t user_id)
{
    bool any_removed = false;
    for (int i = 0; i < MAX_USERS; i++) {
        if (s_users[i].used && s_users[i].user_id == user_id) {
            s_recognizer->delete_feat(s_users[i].feat_id);
            s_users[i].used = false;
            any_removed = true;
        }
    }
    if (any_removed) user_db_save();
    return any_removed;
}

bool face_engine_clear_all_users(void)
{
    esp_err_t err = s_recognizer->clear_all_feats();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear_all_feats that bai (%s)", esp_err_to_name(err));
        return false;
    }
    memset(s_users, 0, sizeof(s_users));
    user_db_save();
    ESP_LOGW(TAG, "Da xoa SACH toan bo user + feature vector");
    return true;
}

int face_engine_list_users(user_info_t *out_list, int max_count)
{
    int n = 0;
    for (int i = 0; i < MAX_USERS && n < max_count; i++) {
        if (!s_users[i].used) continue;
        bool already_listed = false;
        for (int j = 0; j < n; j++) {
            if (out_list[j].user_id == s_users[i].user_id) { already_listed = true; break; }
        }
        if (already_listed) continue;
        out_list[n].user_id = s_users[i].user_id;
        strncpy(out_list[n].name, s_users[i].name, FACE_ENGINE_NAME_LEN - 1);
        n++;
    }
    return n;
}

//==================================================
// Face Detection & Recognition
//==================================================

int face_engine_process_frame(uint16_t *rgb565_buf, int width, int height, face_result_t *out_results)
{
    dl::image::img_t img;

    img.data = rgb565_buf;
    img.width = width;
    img.height = height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

    auto &results = s_detect->run(img);
    int count = 0;

    //==================================================
    // Detection Result
    //==================================================

    for (auto &res : results) {
        if (count >= FACE_ENGINE_MAX_RESULTS) {
            break;
        }

        out_results[count].x1 = res.box[0];
        out_results[count].y1 = res.box[1];
        out_results[count].x2 = res.box[2];
        out_results[count].y2 = res.box[3];

        out_results[count].matched = false;
        out_results[count].user_id = 0xFFFF;

        strncpy(
            out_results[count].name,
            "Detecting",
            FACE_ENGINE_NAME_LEN - 1
        );

        out_results[count].name[FACE_ENGINE_NAME_LEN - 1] = '\0';
        out_results[count].similarity = 0.0f;

        count++;
    }

    //==================================================
    // Liveness Check
    //==================================================
    if (s_liveness_active) {
        if (count >= 1) {
            out_results[0].matched    = true;
            out_results[0].user_id    = s_pending_user_id;
            out_results[0].similarity = s_pending_similarity;
            strncpy(out_results[0].name, s_pending_name, FACE_ENGINE_NAME_LEN - 1);
            out_results[0].name[FACE_ENGINE_NAME_LEN - 1] = '\0';
        }

        if (!results.empty()) {
            int yaw = estimate_yaw(results.front().keypoint);
            if (abs(yaw - s_liveness_baseline) >= LIVENESS_YAW_THRESHOLD) {
                s_liveness_passed = true;
            }
        }

        if (--s_liveness_left <= 0) {
            s_liveness_active = false;
            app_state_set_liveness_prompt(false);

            if (s_liveness_passed) {
                s_last_matched    = true;
                s_last_user_id    = s_pending_user_id;
                s_last_similarity = s_pending_similarity;
                strncpy(s_last_name, s_pending_name, FACE_ENGINE_NAME_LEN - 1);
                s_last_name[FACE_ENGINE_NAME_LEN - 1] = '\0';
                s_persist_left = RESULT_PERSIST_FRAMES;
            } else {

                s_persist_left = 0;
            }

            if (s_uart_report_cb) {
                if (s_liveness_passed) {
                    s_uart_report_cb(FACE_EVENT_LOGIN_SUCCESS, s_pending_event_id,
                                      s_pending_user_id, s_pending_similarity);
                } else {
                    s_uart_report_cb(FACE_EVENT_LIVENESS_FAIL, s_pending_event_id, 0, 0.0f);
                }
            }
        }
        return count;
    }

    //==================================================
    // Recognition Result Persistence
    //==================================================

    if (count >= 1 && s_persist_left > 0) {
        out_results[0].matched = s_last_matched;
        out_results[0].user_id = s_last_user_id;
        out_results[0].similarity = s_last_similarity;
        strncpy(out_results[0].name, s_last_name, FACE_ENGINE_NAME_LEN - 1);
        out_results[0].name[FACE_ENGINE_NAME_LEN - 1] = '\0';
        s_persist_left--;
    }

    if (results.empty()) {
        s_persist_left = 0;
    }


    //==================================================
    // Enrollment Capture
    //==================================================

    if (s_capture_requested && !results.empty()) {
        enroll_state_t st = app_state_get_enroll_state();
        esp_err_t err = s_recognizer->enroll(img, results);
        
        ESP_LOGW(TAG, "get_num_feats() TRUOC enroll = %d", s_recognizer->get_num_feats());;
        s_last_capture_ok = (err == ESP_OK);

        if (s_last_capture_ok) {
            uint16_t feat_id = s_recognizer->get_last_feat_id(); 
            user_db_add(st.user_id, feat_id, st.user_name);
            ESP_LOGI(TAG, "Enroll OK, user_id=%d ten=%s", st.user_id, st.user_name);
        } 
        else {
            ESP_LOGE(TAG, "Enroll that bai");
        }

        s_capture_requested = false;
        app_state_cancel_enroll();
        xSemaphoreGive(s_capture_done_sem);

        return count;
    }

    //==================================================
    // Recognition & Event
    //==================================================
    
    if (!results.empty() && !app_state_is_enrolling()) {
        s_frame_counter++;

        if ((s_frame_counter % RECOG_FRAME_INTERVAL) == 0) {
            auto rec_res = s_recognizer->recognize(img, results);

            face_event_type_t evt_type = FACE_EVENT_STRANGER;
            uint16_t evt_user_id = 0xFFFF;
            const char *evt_name = "Unknown";

            //==================================================
            // Recognition Result
            //==================================================

            if (!rec_res.empty() &&
                rec_res[0].similarity >= app_state_get_threshold()) {

                evt_type = FACE_EVENT_LOGIN_SUCCESS;
                evt_name = user_db_find_name_by_feat_id(rec_res[0].id, &evt_user_id);

                out_results[0].matched = true;
                out_results[0].user_id = evt_user_id;
                out_results[0].similarity = rec_res[0].similarity;
                strncpy(out_results[0].name, evt_name, FACE_ENGINE_NAME_LEN - 1);
                out_results[0].name[FACE_ENGINE_NAME_LEN - 1] = '\0';

                if (app_state_get_liveness_enabled()) {
                    s_liveness_active   = true;
                    s_liveness_left     = LIVENESS_CHECK_FRAMES;
                    s_liveness_baseline = estimate_yaw(results.front().keypoint);
                    s_liveness_passed   = false;
                    s_pending_user_id     = evt_user_id;
                    s_pending_similarity  = rec_res[0].similarity;
                    s_pending_event_id    = ++s_event_id_counter;
                    strncpy(s_pending_name, evt_name, FACE_ENGINE_NAME_LEN - 1);   
                    s_pending_name[FACE_ENGINE_NAME_LEN - 1] = '\0';               
                    app_state_set_liveness_prompt(true);
                    return count;   
                }
            }
            else {
                out_results[0].matched = false;
                out_results[0].user_id = evt_user_id;
                out_results[0].similarity = rec_res.empty() ? 0.0f : rec_res[0].similarity;
                strncpy(out_results[0].name, evt_name, FACE_ENGINE_NAME_LEN - 1);
                out_results[0].name[FACE_ENGINE_NAME_LEN - 1] = '\0';
            }


            //==================================================
            // Recognition State
            //==================================================

            s_last_matched = out_results[0].matched;
            s_last_user_id = out_results[0].user_id;
            s_last_similarity = out_results[0].similarity;

            strncpy(s_last_name, out_results[0].name, FACE_ENGINE_NAME_LEN - 1);

            s_last_name[FACE_ENGINE_NAME_LEN - 1] = '\0';
            s_persist_left = RESULT_PERSIST_FRAMES;

            //==================================================
            // UART Report
            //==================================================

            uint32_t event_id = ++s_event_id_counter;

            if (s_uart_report_cb) {
                s_uart_report_cb(evt_type, event_id, evt_user_id, out_results[0].similarity
                );
            }

            //==================================================
            // Recognition Log
            //==================================================

            ESP_LOGI(TAG, "Ket qua recognize [%s] - Matched: %s, ID: %d, Ten: %s, Score: %.2f",
                     (evt_type == FACE_EVENT_LOGIN_SUCCESS) ? "SUCCESS" : "STRANGER",
                     out_results[0].matched ? "YES" : "NO", evt_user_id, evt_name, out_results[0].similarity);

            //==================================================
            // Event Position
            //==================================================

            int cx = (out_results[0].x1 + out_results[0].x2) / 2;
            int cy = (out_results[0].y1 + out_results[0].y2) / 2;
            //==================================================
            // Event Cooldown
            //==================================================

            if (!s_event_queue) {
                ESP_LOGE(TAG, "Lỗi: s_event_queue chua duoc khoi tao (NULL)!");
            }
            else if (!cooldown_should_emit(evt_type, evt_user_id, cx, cy)) {
                ESP_LOGW(TAG, "Bo qua: Dang trong thoi gian Cooldown (vua gui xong)");
            }
            else {
                uint8_t *jpg_buf = nullptr;
                size_t jpg_len = 0;

                if (fmt2jpg((uint8_t *)rgb565_buf, (size_t)width * height * 2, width, height,
                    PIXFORMAT_RGB565, JPEG_QUALITY, &jpg_buf, &jpg_len)) 
                {
                    face_event_t evt = {};

                    evt.type = evt_type;
                    evt.event_id = event_id;
                    evt.width = (uint16_t)width;
                    evt.height = (uint16_t)height;
                    evt.user_id = evt_user_id;

                    strncpy( evt.name, evt_name, FACE_ENGINE_NAME_LEN - 1);

                    evt.name[FACE_ENGINE_NAME_LEN - 1] = '\0';
                    evt.jpeg_data = jpg_buf;
                    evt.jpeg_len = jpg_len;

                    if (xQueueSend(s_event_queue, &evt, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "event_queue day, bo qua su kien nay");
                        free(jpg_buf);
                    }
                }
                else {
                    ESP_LOGE(TAG, "Nen JPEG that bai cho anh su kien");
                }
            }
        }
    }

    return count;
}