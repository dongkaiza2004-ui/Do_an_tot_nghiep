// access_dashboard_events.c
//
// Giu mot bo dem nho (RAM) cac anh JPEG gan day nhat nhan duoc tu comm_espnow,
// danh chi muc theo event_id, de HTTP handler co the tra ve khi dashboard hoi
// "cho toi xem anh cua su kien X". Khong dung flash/SD card - anh se mat khi
// mat dien hoac khi bi cac anh moi hon day ra khoi bo dem (thiet ke best-effort,
// dung cho xem nhanh chu khong phai luu tru lau dai).

#include "access_dashboard_events.h"
#include "access_events.h"
#include "comm_espnow.h"
#include "sd_storage.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "access_dashboard_events";

#define IMAGE_CACHE_CAPACITY 8
#define HISTORY_QUERY_MAX    32

typedef struct {
    bool in_use;
    uint32_t event_id;
    uint16_t width;
    uint16_t height;
    uint8_t *jpeg_data;
    size_t jpeg_len;
    int64_t stored_at_us;
} image_cache_slot_t;

static image_cache_slot_t s_image_cache[IMAGE_CACHE_CAPACITY];
static SemaphoreHandle_t s_cache_mutex;

//==================================================
// Bo dem anh (LRU don gian theo thoi gian luu)
//==================================================

// Goi khi da giu mutex. Tim va giai phong slot cu nhat de nhuong cho.
static void evict_oldest_locked(void)
{
    int oldest = -1;
    for (int i = 0; i < IMAGE_CACHE_CAPACITY; i++) {
        if (oldest < 0 || (s_image_cache[i].in_use &&
                           (!s_image_cache[oldest].in_use ||
                            s_image_cache[i].stored_at_us < s_image_cache[oldest].stored_at_us))) {
            oldest = i;
        }
    }
    if (oldest >= 0 && s_image_cache[oldest].in_use) {
        free(s_image_cache[oldest].jpeg_data);
        memset(&s_image_cache[oldest], 0, sizeof(s_image_cache[oldest]));
    }
}

static void on_image_ready(uint32_t event_id, uint16_t width, uint16_t height,
                            const uint8_t *jpeg_data, size_t jpeg_len, void *ctx)
{
    (void)ctx;

    if (sd_storage_is_mounted() &&
        sd_storage_save_image(event_id, jpeg_data, jpeg_len) == ESP_OK) {
        return;
    }

    uint8_t *copy = (uint8_t *)malloc(jpeg_len);
    if (!copy) {
        ESP_LOGE(TAG, "Khong du bo nho luu cache anh event_id=%u", (unsigned)event_id);
        return;
    }
    memcpy(copy, jpeg_data, jpeg_len);

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

    int slot_idx = -1;
    for (int i = 0; i < IMAGE_CACHE_CAPACITY; i++) {
        if (!s_image_cache[i].in_use) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0) {
        evict_oldest_locked();
        for (int i = 0; i < IMAGE_CACHE_CAPACITY; i++) {
            if (!s_image_cache[i].in_use) {
                slot_idx = i;
                break;
            }
        }
    }

    if (slot_idx >= 0) {
        s_image_cache[slot_idx].in_use = true;
        s_image_cache[slot_idx].event_id = event_id;
        s_image_cache[slot_idx].width = width;
        s_image_cache[slot_idx].height = height;
        s_image_cache[slot_idx].jpeg_data = copy;
        s_image_cache[slot_idx].jpeg_len = jpeg_len;
        s_image_cache[slot_idx].stored_at_us = esp_timer_get_time();
        copy = NULL; // da chuyen quyen so huu vao cache, khong duoc free ben duoi nua
    }

    xSemaphoreGive(s_cache_mutex);

    if (copy) {
        // Khong nen roi vao day (da evict o tren) nhung giu de tranh leak neu co.
        free(copy);
    }
}

static void on_access_event(const access_event_t *event, void *ctx)
{
    // Vong RAM da duoc access_events tu luu. O day chi them ban sao ben vung len the SD
    // (khong chan: chi dua vao hang doi, task rieng ghi).
    sd_storage_log_event(event);

}

esp_err_t access_dashboard_events_init(void)
{
    if (!s_cache_mutex) {
        s_cache_mutex = xSemaphoreCreateMutex();
        if (!s_cache_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    memset(s_image_cache, 0, sizeof(s_image_cache));

    // Nap lai lich su tu the SD (neu co) de dashboard thay ngay sau khi reboot.
    if (sd_storage_is_mounted()) {
        access_event_t restored[HISTORY_QUERY_MAX];
        size_t n = sd_storage_load_recent_events(restored, HISTORY_QUERY_MAX);
        if (n > 0) {
            access_events_restore(restored, n);
            ESP_LOGI(TAG, "Da nap lai %u su kien tu the SD", (unsigned)n);
        }
    }


    esp_err_t err = comm_espnow_register_image_handler(on_image_ready, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong dang ky duoc image handler: %d", err);
        return err;
    }

    err = access_events_subscribe(on_access_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong dang ky duoc access_events subscriber: %d", err);
        return err;
    }

    return ESP_OK;
}

bool access_dashboard_has_image(uint32_t event_id)
{
    if (s_cache_mutex) {
        bool in_ram = false;
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        for (int i = 0; i < IMAGE_CACHE_CAPACITY; i++) {
            if (s_image_cache[i].in_use && s_image_cache[i].event_id == event_id) {
                in_ram = true;
                break;
            }
        }
        xSemaphoreGive(s_cache_mutex);
        if (in_ram) {
            return true;
        }
    }
    return sd_storage_image_exists(event_id);
}

esp_err_t access_dashboard_copy_image(uint32_t event_id, uint8_t **out_copy, size_t *out_len)
{
    if (!out_copy || !out_len || !s_cache_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    for (int i = 0; i < IMAGE_CACHE_CAPACITY; i++) {
        if (s_image_cache[i].in_use && s_image_cache[i].event_id == event_id) {
            uint8_t *copy = (uint8_t *)malloc(s_image_cache[i].jpeg_len);
            if (copy) {
                memcpy(copy, s_image_cache[i].jpeg_data, s_image_cache[i].jpeg_len);
                *out_copy = copy;
                *out_len = s_image_cache[i].jpeg_len;
                result = ESP_OK;
            } else {
                result = ESP_ERR_NO_MEM;
            }
            break;
        }
    }
    xSemaphoreGive(s_cache_mutex);
    
    if (result == ESP_ERR_NOT_FOUND && sd_storage_is_mounted()) {
        result = sd_storage_read_image(event_id, out_copy, out_len);
    }

    return result;
}

cJSON *access_dashboard_build_history_json(size_t limit)
{
    if (limit > HISTORY_QUERY_MAX) {
        limit = HISTORY_QUERY_MAX;
    }

    access_event_t buffer[HISTORY_QUERY_MAX];
    size_t n = access_events_get_recent(buffer, limit);

    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }

    for (size_t i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }

        cJSON_AddNumberToObject(item, "event_id", buffer[i].event_id);
        cJSON_AddNumberToObject(item, "face_user_id", buffer[i].face_user_id);
        cJSON_AddNumberToObject(item, "matter_user_index", buffer[i].matter_user_index);
        cJSON_AddNumberToObject(item, "similarity", buffer[i].similarity);
        cJSON_AddNumberToObject(item, "timestamp_us", (double)buffer[i].timestamp_us);

        const char *type_str = "unknown";
        switch (buffer[i].type) {
            case ACCESS_EVENT_GRANTED:       type_str = "granted"; break;
            case ACCESS_EVENT_DENIED:        type_str = "denied"; break;
            case ACCESS_EVENT_UNKNOWN_FACE:  type_str = "unknown_face"; break;
            case ACCESS_EVENT_LIVENESS_FAIL: type_str = "liveness_fail"; break;
        }
        cJSON_AddStringToObject(item, "type", type_str);

        // unix_time > 0 = gio that (UTC, giay); 0 = chua dong bo gio khi su kien xay ra.
        cJSON_AddNumberToObject(item, "unix_time", (double)buffer[i].unix_time_s);
        cJSON_AddBoolToObject(item, "has_image", access_dashboard_has_image(buffer[i].event_id));

        cJSON_AddItemToArray(array, item);
    }

    return array;
}
