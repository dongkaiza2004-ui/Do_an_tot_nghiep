// time_sync.c

#include "time_sync.h"

#include <time.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

static const char *TAG = "time_sync";

static bool s_started = false;

static void start_sntp(void)
{
    if (s_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.wait_for_sync = false;   // khong chan, dong bo xong se tu cap nhat gio
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init that bai: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "Da bat SNTP (pool.ntp.org)");
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    start_sntp();
}

void time_sync_start(void)
{
    esp_err_t err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                ip_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong dang ky duoc IP event: %s", esp_err_to_name(err));
    }

    // Truong hop da co IP truoc khi ham nay duoc goi.
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (sta && esp_netif_get_ip_info(sta, &info) == ESP_OK && info.ip.addr != 0) {
        start_sntp();
    }
}

bool time_sync_is_valid(void)
{
    time_t now = time(NULL);
    return now > 1700000000;   // sau ~11/2023
}
