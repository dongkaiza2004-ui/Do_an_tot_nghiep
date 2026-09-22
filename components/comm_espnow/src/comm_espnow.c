// comm_espnow.c
//
// Nhan cac fragment JPEG gui tu camera (Node A) qua ESP-NOW va ghep lai theo
// event_id. Kenh nay la "best-effort": khong co ACK/gui lai manh bi rot (dung
// nhu thiet ke goc da ghi ro trong tai lieu §6.4/§7.2), nen o day phai tu xu
// ly truong hop thieu manh bang timeout thay vi cho mai mai.

#include "comm_espnow.h"
#include "espnow_frame.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

static const char *TAG = "comm_espnow";

#define MAX_INFLIGHT_IMAGES     2      // So anh dang ghep dong thoi toi da
#define MAX_IMAGE_LISTENERS     2
#define REASSEMBLY_TIMEOUT_MS   4000   // Qua thoi gian nay ma chua du manh -> huy
#define FRAG_QUEUE_LEN          16

typedef struct {
    bool in_use;
    uint32_t event_id;
    uint16_t width;
    uint16_t height;
    uint32_t jpeg_len;
    uint16_t fragment_total;
    uint16_t fragments_received;
    bool *fragment_seen;       // mang danh dau da nhan manh thu i chua (cap phat dong theo fragment_total)
    uint8_t *jpeg_buffer;      // cap phat dong theo jpeg_len
    int64_t last_update_us;
} reassembly_slot_t;

typedef struct {
    bool in_use;
    comm_espnow_image_cb_t cb;
    void *ctx;
} image_listener_t;

static QueueHandle_t s_frag_queue = NULL;
static reassembly_slot_t s_slots[MAX_INFLIGHT_IMAGES];
static image_listener_t s_listeners[MAX_IMAGE_LISTENERS];
static SemaphoreHandle_t s_listener_mutex;

//==================================================
// Quan ly slot ghep anh
//==================================================

static void free_slot(reassembly_slot_t *slot)
{
    free(slot->jpeg_buffer);
    free(slot->fragment_seen);
    memset(slot, 0, sizeof(*slot));
}

static reassembly_slot_t *find_slot_for_event(uint32_t event_id)
{
    for (int i = 0; i < MAX_INFLIGHT_IMAGES; i++) {
        if (s_slots[i].in_use && s_slots[i].event_id == event_id) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static reassembly_slot_t *allocate_slot(const espnow_frame_fragment_t *frag)
{
    reassembly_slot_t *slot = NULL;

    for (int i = 0; i < MAX_INFLIGHT_IMAGES; i++) {
        if (!s_slots[i].in_use) {
            slot = &s_slots[i];
            break;
        }
    }

    if (!slot) {
        // Het cho dong thoi - danh doi 1 anh dang ghep do (co the do rot manh
        // giua chung, phu hop voi ban chat best-effort cua kenh nay).
        int oldest = 0;
        for (int i = 1; i < MAX_INFLIGHT_IMAGES; i++) {
            if (s_slots[i].last_update_us < s_slots[oldest].last_update_us) {
                oldest = i;
            }
        }
        ESP_LOGW(TAG, "Het slot ghep anh, huy anh dang do event_id=%u de nhuong cho event_id=%u",
                 (unsigned)s_slots[oldest].event_id, (unsigned)frag->event_id);
        free_slot(&s_slots[oldest]);
        slot = &s_slots[oldest];
    }

    slot->jpeg_buffer = (uint8_t *)malloc(frag->jpeg_len);
    slot->fragment_seen = (bool *)calloc(frag->fragment_total, sizeof(bool));

    if (!slot->jpeg_buffer || !slot->fragment_seen) {
        ESP_LOGE(TAG, "Khong du bo nho de ghep anh event_id=%u (jpeg_len=%u, fragments=%u)",
                 (unsigned)frag->event_id, (unsigned)frag->jpeg_len, frag->fragment_total);
        free_slot(slot);
        return NULL;
    }

    slot->in_use = true;
    slot->event_id = frag->event_id;
    slot->width = frag->width;
    slot->height = frag->height;
    slot->jpeg_len = frag->jpeg_len;
    slot->fragment_total = frag->fragment_total;
    slot->fragments_received = 0;
    slot->last_update_us = esp_timer_get_time();

    return slot;
}

//==================================================
// Thong bao cho listener khi ghep xong
//==================================================

static void notify_listeners(uint32_t event_id, uint16_t width, uint16_t height,
                              const uint8_t *jpeg_data, size_t jpeg_len)
{
    image_listener_t local[MAX_IMAGE_LISTENERS];

    if (!s_listener_mutex || xSemaphoreTake(s_listener_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Khong lay duoc listener mutex, bo qua thong bao anh event_id=%u", (unsigned)event_id);
        return;
    }
    memcpy(local, s_listeners, sizeof(local));
    xSemaphoreGive(s_listener_mutex);

    for (int i = 0; i < MAX_IMAGE_LISTENERS; i++) {
        if (local[i].in_use && local[i].cb) {
            local[i].cb(event_id, width, height, jpeg_data, jpeg_len, local[i].ctx);
        }
    }
}

//==================================================
// Xu ly 1 fragment nhan duoc
//==================================================

static void handle_fragment(const espnow_frame_fragment_t *frag)
{
    // Kiem tra hop le TRUOC KHI dung du lieu tu mang de tinh offset/con tro -
    // day la du lieu nhan tu mot thiet bi khac, khong duoc tin tuong tuyet doi.
    if (frag->fragment_total == 0 || frag->fragment_index >= frag->fragment_total) {
        ESP_LOGW(TAG, "Fragment khong hop le (index=%u/%u), bo qua",
                 frag->fragment_index, frag->fragment_total);
        return;
    }
    if (frag->payload_len > ESPNOW_FRAGMENT_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "payload_len vuot gioi han (%u), bo qua fragment", frag->payload_len);
        return;
    }

    size_t offset = (size_t)frag->fragment_index * ESPNOW_FRAGMENT_MAX_PAYLOAD;
    if (offset + frag->payload_len > frag->jpeg_len) {
        ESP_LOGW(TAG, "Fragment vuot qua kich thuoc anh khai bao (event_id=%u), bo qua",
                 (unsigned)frag->event_id);
        return;
    }

    reassembly_slot_t *slot = find_slot_for_event(frag->event_id);
    if (!slot) {
        slot = allocate_slot(frag);
        if (!slot) {
            return;
        }
    }

    // Neu jpeg_len/fragment_total bao cao khac voi lan dau ghi nhan cho cung
    // event_id nay thi co gi do sai (du lieu hong hoac dung lai event_id) ->
    // huy de tranh ghi de sai vung nho da cap phat theo kich thuoc cu.
    if (slot->jpeg_len != frag->jpeg_len || slot->fragment_total != frag->fragment_total) {
        ESP_LOGW(TAG, "Thong tin anh khong khop giua cac fragment cua event_id=%u, huy ghep",
                 (unsigned)frag->event_id);
        free_slot(slot);
        return;
    }

    if (!slot->fragment_seen[frag->fragment_index]) {
        memcpy(slot->jpeg_buffer + offset, frag->payload, frag->payload_len);
        slot->fragment_seen[frag->fragment_index] = true;
        slot->fragments_received++;
    }
    slot->last_update_us = esp_timer_get_time();

    if (slot->fragments_received >= slot->fragment_total) {
        notify_listeners(slot->event_id, slot->width, slot->height, slot->jpeg_buffer, slot->jpeg_len);
        free_slot(slot);
    }
}

static void sweep_stale_slots(void)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_INFLIGHT_IMAGES; i++) {
        if (s_slots[i].in_use &&
            (now - s_slots[i].last_update_us) > (int64_t)REASSEMBLY_TIMEOUT_MS * 1000) {
            ESP_LOGW(TAG, "Anh su kien event_id=%u khong nhan du manh (%u/%u), huy",
                     (unsigned)s_slots[i].event_id, s_slots[i].fragments_received,
                     s_slots[i].fragment_total);
            free_slot(&s_slots[i]);
        }
    }
}

//==================================================
// Task xu ly nen (tach khoi callback ESP-NOW/Wi-Fi de khong lam nghen stack Wi-Fi)
//==================================================

static void espnow_rx_task(void *arg)
{
    espnow_frame_fragment_t frag;

    while (1) {
        if (xQueueReceive(s_frag_queue, &frag, pdMS_TO_TICKS(500)) == pdTRUE) {
            handle_fragment(&frag);
        }
        sweep_stale_slots();
    }
}

//==================================================
// Callback ESP-NOW (chay trong context task Wi-Fi - phai nhanh, khong block)
//==================================================

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    (void)recv_info;

    if (len <= 0 || (size_t)len < offsetof(espnow_frame_fragment_t, payload)) {
        ESP_LOGW(TAG, "Goi ESP-NOW qua ngan (%d byte), bo qua", len);
        return;
    }

    espnow_frame_fragment_t frag = {0};
    size_t copy_len = (size_t)len > sizeof(frag) ? sizeof(frag) : (size_t)len;
    memcpy(&frag, data, copy_len);

    if (xQueueSend(s_frag_queue, &frag, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Hang doi fragment day, rot 1 goi (event_id=%u)", (unsigned)frag.event_id);
    }
}

//==================================================
// Khoi tao / Bat dau
//==================================================

esp_err_t comm_espnow_init(void)
{
    // Khong goi esp_netif_init()/esp_wifi_init()/esp_wifi_start() o day - xem
    // ghi chu trong comm_espnow.h. Matter da lo phan nay roi.
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init that bai: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_recv_cb that bai: %s", esp_err_to_name(err));
        return err;
    }

    s_frag_queue = xQueueCreate(FRAG_QUEUE_LEN, sizeof(espnow_frame_fragment_t));
    if (!s_frag_queue) {
        return ESP_ERR_NO_MEM;
    }

    s_listener_mutex = xSemaphoreCreateMutex();
    if (!s_listener_mutex) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_slots, 0, sizeof(s_slots));
    memset(s_listeners, 0, sizeof(s_listeners));

    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        ESP_LOGI(TAG, "comm_espnow init OK. MAC STA cua Node B (dien vao s_peer_mac ben camera): " MACSTR,
                 MAC2STR(mac));
    } else {
        ESP_LOGI(TAG, "comm_espnow init OK");
    }

    ESP_LOGW(TAG, "Nho co dinh kenh Wi-Fi cua router trung voi kenh ESP-NOW ben camera "
                  "(camera mac dinh o kenh 1 vi khong ket noi AP nao) - xem comm_espnow.h");

    return ESP_OK;
}

void comm_espnow_start(void)
{
    xTaskCreatePinnedToCore(espnow_rx_task, "comm_espnow_rx", 4096, NULL, 4, NULL, 0);
}

esp_err_t comm_espnow_register_image_handler(comm_espnow_image_cb_t cb, void *ctx)
{
    if (!cb || !s_listener_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_listener_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_IMAGE_LISTENERS; i++) {
        if (!s_listeners[i].in_use) {
            s_listeners[i].in_use = true;
            s_listeners[i].cb = cb;
            s_listeners[i].ctx = ctx;
            result = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_listener_mutex);
    return result;
}
