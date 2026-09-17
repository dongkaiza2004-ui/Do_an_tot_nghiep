// comm_espnow.c
#include "comm_espnow.h"
#include "espnow_frame.h"
#include "face_engine.h"   // dung face_event_t (struct C thuong, an toan include tu file .c)
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "comm_espnow";
static QueueHandle_t s_event_queue = NULL;
static uint8_t s_peer_mac[6];
static uint16_t s_frame_id_counter = 0;

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Goi ESP-NOW gui that bai");
    }
}

esp_err_t comm_espnow_init(const uint8_t peer_mac[6])
{
    memcpy(s_peer_mac, peer_mac, 6);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_peer_mac, 6);
    peer.channel = 0;      // dung kenh WiFi hien tai
    peer.encrypt = false;  // TODO: bat encrypt + dat LMK/PMK neu can bao mat du lieu anh
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    s_event_queue = xQueueCreate(4, sizeof(face_event_t));

    ESP_LOGI(TAG, "ESP-NOW init OK, peer=" MACSTR, MAC2STR(s_peer_mac));
    return ESP_OK;
}

void *comm_espnow_get_event_queue(void)
{
    return (void *)s_event_queue;
}

static void send_fragmented_jpeg(const face_event_t *evt)
{
    uint16_t frame_id = s_frame_id_counter++;
    uint16_t total = (uint16_t)((evt->jpeg_len + ESPNOW_FRAGMENT_MAX_PAYLOAD - 1)
                                 / ESPNOW_FRAGMENT_MAX_PAYLOAD);

    ESP_LOGI(TAG, "Gui anh su kien: type=%d user_id=%d jpeg_len=%u fragments=%u",
             evt->type, evt->user_id, (unsigned)evt->jpeg_len, total);

    for (uint16_t i = 0; i < total; i++) {
        espnow_frame_fragment_t frag = {0};
        frag.frame_id = frame_id;
        frag.fragment_index = i;
        frag.fragment_total = total;

        size_t offset = (size_t)i * ESPNOW_FRAGMENT_MAX_PAYLOAD;
        size_t remain = evt->jpeg_len - offset;
        size_t chunk  = remain < ESPNOW_FRAGMENT_MAX_PAYLOAD ? remain : ESPNOW_FRAGMENT_MAX_PAYLOAD;
        frag.payload_len = (uint16_t)chunk;
        memcpy(frag.payload, evt->jpeg_data + offset, chunk);

        size_t send_size = offsetof(espnow_frame_fragment_t, payload) + chunk;
        esp_err_t err = esp_now_send(s_peer_mac, (uint8_t *)&frag, send_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_send loi (%s) tai fragment %u/%u", esp_err_to_name(err), i, total);
        }
        // Nghi ngan giua cac goi de khong lam nghen hang doi gui cua driver WiFi -
        // gia tri nay co the can dieu chinh thuc nghiem tren phan cung that.
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void espnow_tx_task(void *arg)
{
    face_event_t evt;
    while (1) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.jpeg_data && evt.jpeg_len > 0) {
                send_fragmented_jpeg(&evt);
                free(evt.jpeg_data); // face_engine cap phat, comm_espnow chiu trach nhiem giai phong
            }
        }
    }
}

void comm_espnow_start(void)
{
    xTaskCreatePinnedToCore(espnow_tx_task, "espnow_tx_task", 4096, NULL, 4, NULL, 0);
}
