// stub_impl.c - cai dat cac ham gia lap khai bao trong stub_esp.h
#define _GNU_SOURCE
#include "stub_esp.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "ESP_OK" : "ESP_ERR"; }

int stub_log_warn = 0, stub_log_err = 0;
void stub_log(char level, const char *tag, const char *fmt, ...)
{
    if (level == 'W') stub_log_warn++;
    if (level == 'E') stub_log_err++;
    if (getenv("STUB_VERBOSE")) {
        va_list ap; va_start(ap, fmt);
        fprintf(stderr, "[%c] %s: ", level, tag);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        va_end(ap);
    }
}

esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t mode) { (void)pin; (void)mode; return ESP_OK; }

void (*stub_task_fn)(void *) = NULL;
const uint8_t *stub_rx = NULL;
size_t stub_rx_len = 0, stub_rx_pos = 0;
jmp_buf stub_rx_exit;
uint8_t stub_tx[8192];
size_t stub_tx_len = 0;

esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int q, void *qh, int flags)
{ (void)p; (void)rx; (void)tx; (void)q; (void)qh; (void)flags; return ESP_OK; }
esp_err_t uart_param_config(uart_port_t p, const uart_config_t *c) { (void)p; (void)c; return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t p, int tx, int rx, int rts, int cts)
{ (void)p; (void)tx; (void)rx; (void)rts; (void)cts; return ESP_OK; }

int uart_write_bytes(uart_port_t p, const void *src, size_t n)
{
    (void)p;
    if (stub_tx_len + n <= sizeof(stub_tx)) {
        memcpy(stub_tx + stub_tx_len, src, n);
        stub_tx_len += n;
    }
    return (int)n;
}

// Code that goi uart_read_bytes(...,1,...) trong vong while(1). Het du lieu thi
// nhay ra ngoai bang longjmp de bai test lay duoc quyen dieu khien.
int uart_read_bytes(uart_port_t p, void *buf, uint32_t n, uint32_t ticks)
{
    (void)p; (void)ticks;
    if (stub_rx_pos >= stub_rx_len) longjmp(stub_rx_exit, 1);
    size_t k = stub_rx_len - stub_rx_pos;
    if (k > n) k = n;
    memcpy(buf, stub_rx + stub_rx_pos, k);
    stub_rx_pos += k;
    return (int)k;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
// Hang doi FIFO that. Rong -> tra "timeout" toi da stub_idle_polls lan, het thi longjmp
// ra khoi vong while(1) cua task (de bai test lay lai quyen dieu khien).
typedef struct { int len, item, head, count; uint8_t *data; } stub_queue_t;
int stub_idle_polls = 0;
QueueHandle_t xQueueCreate(int len, int item)
{
    stub_queue_t *q = calloc(1, sizeof(*q));
    q->len = len; q->item = item; q->data = calloc((size_t)len, (size_t)item);
    return q;
}
int xQueueSend(QueueHandle_t h, const void *item, TickType_t t)
{
    (void)t; stub_queue_t *q = h;
    if (!q || q->count >= q->len) return pdFALSE;
    memcpy(q->data + (size_t)((q->head + q->count) % q->len) * (size_t)q->item, item, (size_t)q->item);
    q->count++;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t h, void *item, TickType_t t)
{
    (void)t; stub_queue_t *q = h;
    if (!q || q->count == 0) {
        if (stub_idle_polls > 0) { stub_idle_polls--; return pdFALSE; }
        longjmp(stub_rx_exit, 1);
    }
    memcpy(item, q->data + (size_t)q->head * (size_t)q->item, (size_t)q->item);
    q->head = (q->head + 1) % q->len; q->count--;
    return pdTRUE;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)1; }
int64_t stub_now_us = 0;
int64_t esp_timer_get_time(void) { return stub_now_us; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *h)
{ (void)name; (void)stack; (void)arg; (void)prio; (void)h; stub_task_fn = fn; return pdPASS; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *h, int core)
{ (void)name; (void)stack; (void)arg; (void)prio; (void)h; (void)core; stub_task_fn = fn; return pdPASS; }

esp_err_t esp_vfs_fat_sdmmc_mount(const char *mp, const sdmmc_host_t *h, const void *slot,
                                  const esp_vfs_fat_mount_config_t *cfg, sdmmc_card_t **card)
{
    static sdmmc_card_t c;
    (void)h; (void)slot; (void)cfg;
    mkdir(mp, 0777);              // "gan the SD" = tao thu muc tren dia PC
    *card = &c;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char *mp, sdmmc_card_t *card) { (void)mp; (void)card; return ESP_OK; }
void sdmmc_card_print_info(FILE *f, const sdmmc_card_t *c) { (void)f; (void)c; }

// ---- ESP-NOW / Wi-Fi gia --------------------------------------------------------------
esp_now_recv_cb_t stub_espnow_recv_cb = NULL;
uint8_t stub_sent_buf[STUB_MAX_SENT][250];
size_t stub_sent_len[STUB_MAX_SENT];
int stub_sent_count = 0;
esp_err_t stub_espnow_send_rc = ESP_OK;
esp_err_t esp_now_init(void) { return ESP_OK; }
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) { stub_espnow_recv_cb = cb; return ESP_OK; }
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb) { (void)cb; return ESP_OK; }
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer) { (void)peer; return ESP_OK; }
esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len)
{
    (void)mac;
    if (stub_sent_count < STUB_MAX_SENT && len <= 250) {
        memcpy(stub_sent_buf[stub_sent_count], data, len);
        stub_sent_len[stub_sent_count++] = len;
    }
    return stub_espnow_send_rc;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int s) { (void)s; return ESP_OK; }
esp_err_t esp_wifi_set_mode(int m) { (void)m; return ESP_OK; }
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_set_ps(int ps) { (void)ps; return ESP_OK; }
esp_err_t esp_wifi_get_mac(int ifx, uint8_t mac[6]) { (void)ifx; memset(mac, 0xAB, 6); return ESP_OK; }
int stub_wifi_last_channel = 0;
esp_err_t stub_wifi_channel_rc = ESP_OK;
esp_err_t esp_wifi_set_channel(uint8_t ch, int second)
{ (void)second; stub_wifi_last_channel = ch; return stub_wifi_channel_rc; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }

// ---- NVS gia: namespace/key -> so nguyen, giu trong RAM -------------------------------
#define NVS_MAX_ENT 64
static struct { char ns[16], key[16]; long long v; bool used; } nvs_tab[NVS_MAX_ENT];
static char nvs_hns[8][16]; static int nvs_hn = 0;
void stub_nvs_reset(void) { memset(nvs_tab, 0, sizeof(nvs_tab)); nvs_hn = 0; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { stub_nvs_reset(); return ESP_OK; }
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    bool exists = false;
    for (int i = 0; i < NVS_MAX_ENT; i++) if (nvs_tab[i].used && !strcmp(nvs_tab[i].ns, ns)) exists = true;
    if (mode == NVS_READONLY && !exists) return ESP_ERR_NVS_NOT_FOUND;   // dung nhu NVS that
    if (nvs_hn >= 8) return ESP_FAIL;
    snprintf(nvs_hns[nvs_hn], 16, "%s", ns);
    *h = (nvs_handle_t)nvs_hn++;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static esp_err_t nvs_get(nvs_handle_t h, const char *k, long long *v)
{
    for (int i = 0; i < NVS_MAX_ENT; i++)
        if (nvs_tab[i].used && !strcmp(nvs_tab[i].ns, nvs_hns[h]) && !strcmp(nvs_tab[i].key, k)) { *v = nvs_tab[i].v; return ESP_OK; }
    return ESP_ERR_NVS_NOT_FOUND;
}
static esp_err_t nvs_put(nvs_handle_t h, const char *k, long long v)
{
    int freeslot = -1;
    for (int i = 0; i < NVS_MAX_ENT; i++) {
        if (nvs_tab[i].used && !strcmp(nvs_tab[i].ns, nvs_hns[h]) && !strcmp(nvs_tab[i].key, k)) { nvs_tab[i].v = v; return ESP_OK; }
        if (!nvs_tab[i].used && freeslot < 0) freeslot = i;
    }
    if (freeslot < 0) return ESP_FAIL;
    nvs_tab[freeslot].used = true; nvs_tab[freeslot].v = v;
    snprintf(nvs_tab[freeslot].ns, 16, "%s", nvs_hns[h]); snprintf(nvs_tab[freeslot].key, 16, "%s", k);
    return ESP_OK;
}
#define NVS_GET(name, T) esp_err_t name(nvs_handle_t h, const char *k, T *o) { long long v; esp_err_t e = nvs_get(h, k, &v); if (e == ESP_OK) *o = (T)v; return e; }
#define NVS_SET(name, T) esp_err_t name(nvs_handle_t h, const char *k, T v) { return nvs_put(h, k, (long long)v); }
NVS_GET(nvs_get_u8, uint8_t) NVS_GET(nvs_get_i8, int8_t) NVS_GET(nvs_get_i32, int32_t) NVS_GET(nvs_get_u32, uint32_t)
NVS_SET(nvs_set_u8, uint8_t) NVS_SET(nvs_set_i8, int8_t) NVS_SET(nvs_set_i32, int32_t) NVS_SET(nvs_set_u32, uint32_t)

// ---- Theo doi free() --------------------------------------------------------------------
#undef free
static const void *freed_ring[64]; static int freed_n = 0;
void stub_track_free(void *p) { freed_ring[freed_n++ % 64] = p; free(p); }
bool stub_was_freed(const void *p)
{
    for (int i = 0; i < 64 && i < freed_n; i++) if (freed_ring[i] == p) return true;
    return false;
}
void stub_free_forget(void) { freed_n = 0; memset(freed_ring, 0, sizeof(freed_ring)); }
