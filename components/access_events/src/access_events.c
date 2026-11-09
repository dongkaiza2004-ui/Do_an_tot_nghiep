// access_events.c

#include "access_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <time.h>
#include <string.h>

static const char *TAG = "access_events";

#define MAX_SUBSCRIBERS   3
#define HISTORY_CAPACITY  32

typedef struct {
    bool in_use;
    access_events_cb_t cb;
    void *ctx;
} subscriber_t;

static subscriber_t s_subscribers[MAX_SUBSCRIBERS];
static access_event_t s_history[HISTORY_CAPACITY];
static size_t s_history_count = 0;   // so phan tu dang dung (<= CAPACITY)
static size_t s_history_head = 0;    // vi tri se ghi tiep theo (vong tron)
static SemaphoreHandle_t s_mutex;

esp_err_t access_events_init(void)
{
    if (s_mutex) {
        return ESP_OK; // da khoi tao roi, cho phep goi lai nhieu lan an toan
    }
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_subscribers, 0, sizeof(s_subscribers));
    memset(s_history, 0, sizeof(s_history));
    s_history_count = 0;
    s_history_head = 0;
    return ESP_OK;
}

esp_err_t access_events_publish(const access_event_t *event)
{
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mutex) {
        ESP_LOGE(TAG, "access_events_publish goi truoc khi init");
        return ESP_ERR_INVALID_STATE;
    }

    access_event_t copy = *event;
    if (copy.timestamp_us == 0) {
        copy.timestamp_us = esp_timer_get_time();
    }
    if (copy.unix_time_s == 0) {
        time_t now = time(NULL);
        if (now > 1700000000) {   // chi nhan khi gio da duoc dong bo (khong phai 1970)
            copy.unix_time_s = (int64_t)now;
        }
    }


    subscriber_t local[MAX_SUBSCRIBERS];

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_history[s_history_head] = copy;
    s_history_head = (s_history_head + 1) % HISTORY_CAPACITY;
    if (s_history_count < HISTORY_CAPACITY) {
        s_history_count++;
    }
    memcpy(local, s_subscribers, sizeof(local));
    xSemaphoreGive(s_mutex);

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (local[i].in_use && local[i].cb) {
            local[i].cb(&copy, local[i].ctx);
        }
    }
    return ESP_OK;
}
esp_err_t access_events_restore(const access_event_t *events, size_t count)
{
    if (!events || !s_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (size_t i = 0; i < count; i++) {
        s_history[s_history_head] = events[i];
        s_history_head = (s_history_head + 1) % HISTORY_CAPACITY;
        if (s_history_count < HISTORY_CAPACITY) {
            s_history_count++;
        }
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t access_events_subscribe(access_events_cb_t cb, void *ctx)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!s_subscribers[i].in_use) {
            s_subscribers[i].in_use = true;
            s_subscribers[i].cb = cb;
            s_subscribers[i].ctx = ctx;
            result = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_mutex);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Het cho dang ky subscriber (MAX_SUBSCRIBERS=%d)", MAX_SUBSCRIBERS);
    }
    return result;
}

size_t access_events_get_recent(access_event_t *out, size_t max_count)
{
    if (!out || max_count == 0 || !s_mutex) {
        return 0;
    }

    size_t n;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    n = s_history_count < max_count ? s_history_count : max_count;
    // Lay n phan tu gan nhat, moi nhat truoc (duyet nguoc tu vi tri ghi cuoi cung).
    for (size_t i = 0; i < n; i++) {
        size_t idx = (s_history_head + HISTORY_CAPACITY - 1 - i) % HISTORY_CAPACITY;
        out[i] = s_history[idx];
    }
    xSemaphoreGive(s_mutex);
    return n;
}
