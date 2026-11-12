// stub_esp.h - Gia lap TOI THIEU ESP-IDF de bien dich code that tren PC.
// Moi header trong stubs/ chi include file nay. KHONG co logic nghiep vu o day.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <setjmp.h>

// ---- esp_err ----------------------------------------------------------------
typedef int esp_err_t;
#define ESP_OK                0
#define ESP_FAIL             -1
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE  0x104
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#include <stdlib.h>
#define ESP_ERROR_CHECK(x) do { esp_err_t _e = (x); if (_e != ESP_OK) { \
    fprintf(stderr, "ESP_ERROR_CHECK that bai (%d) tai %s:%d\n", _e, __FILE__, __LINE__); abort(); } } while (0)
const char *esp_err_to_name(esp_err_t e);

// ---- esp_log (dem so canh bao/loi; in ra neu dat bien moi truong STUB_VERBOSE=1)
extern int stub_log_warn, stub_log_err;
void stub_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf,3,4)));
#define ESP_LOGI(t, f, ...) stub_log('I', t, f, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) stub_log('W', t, f, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) stub_log('E', t, f, ##__VA_ARGS__)
#define ESP_LOGD(t, f, ...) ((void)0)

// ---- gpio -------------------------------------------------------------------
typedef int gpio_num_t;
enum { GPIO_NUM_2 = 2, GPIO_NUM_38 = 38, GPIO_NUM_39 = 39, GPIO_NUM_40 = 40,
       GPIO_NUM_41 = 41, GPIO_NUM_42 = 42 };
typedef enum { GPIO_PULLUP_ONLY = 1 } gpio_pull_mode_t;
esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t mode);

// ---- uart -------------------------------------------------------------------
typedef int uart_port_t;
#define UART_NUM_1 1
#define UART_PIN_NO_CHANGE (-1)
enum { UART_DATA_8_BITS = 3, UART_PARITY_DISABLE = 0, UART_STOP_BITS_1 = 1,
       UART_HW_FLOWCTRL_DISABLE = 0, UART_SCLK_DEFAULT = 0 };
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int q, void *qh, int flags);
esp_err_t uart_param_config(uart_port_t p, const uart_config_t *c);
esp_err_t uart_set_pin(uart_port_t p, int tx, int rx, int rts, int cts);
int uart_write_bytes(uart_port_t p, const void *src, size_t n);
int uart_read_bytes(uart_port_t p, void *buf, uint32_t n, uint32_t ticks);

// ---- FreeRTOS (don luong, khong chay task that) ------------------------------
typedef uint32_t TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t t);
int xSemaphoreGive(SemaphoreHandle_t s);
QueueHandle_t xQueueCreate(int len, int item);
int xQueueSend(QueueHandle_t q, const void *item, TickType_t t);
int xQueueReceive(QueueHandle_t q, void *item, TickType_t t);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *h);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *h, int core);

// ---- Dieu khien cua bai test (chi co o PC) -------------------------------------
extern void (*stub_task_fn)(void *);          // ham task cuoi cung duoc tao (vd uart_rx_task)
extern const uint8_t *stub_rx;                // du lieu "nhan tu UART" do test nap vao
extern size_t stub_rx_len, stub_rx_pos;
extern jmp_buf stub_rx_exit;                  // het du lieu -> longjmp ra khoi vong while(1) cua task
extern uint8_t stub_tx[8192];                 // moi byte code that gui ra UART
extern size_t stub_tx_len;

// ---- SDMMC / VFS FAT ----------------------------------------------------------
#define SOC_SDMMC_USE_GPIO_MATRIX 1
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_SLOT_NO_CD (-1)
#define SDMMC_SLOT_NO_WP (-1)
typedef struct { int dummy; } sdmmc_card_t;
typedef struct { int max_freq_khz; } sdmmc_host_t;
typedef struct { int width, cd, wp, clk, cmd, d0, d1, d2, d3; } sdmmc_slot_config_t;
typedef struct { bool format_if_mount_failed; int max_files; size_t allocation_unit_size; } esp_vfs_fat_mount_config_t;
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){0})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
esp_err_t esp_vfs_fat_sdmmc_mount(const char *mp, const sdmmc_host_t *h, const void *slot,
                                  const esp_vfs_fat_mount_config_t *cfg, sdmmc_card_t **card);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *mp, sdmmc_card_t *card);
void sdmmc_card_print_info(FILE *f, const sdmmc_card_t *c);

// ---- Dong ho gia, hang doi FIFO that, semaphore nhi phan ---------------------------
extern int64_t stub_now_us;                   // thoi gian "esp_timer" do test dieu khien
int64_t esp_timer_get_time(void);
extern int stub_idle_polls;                   // so lan xQueueReceive tra "timeout" truoc khi thoat task
SemaphoreHandle_t xSemaphoreCreateBinary(void);

// ---- ESP-NOW / Wi-Fi / netif / event ----------------------------------------------
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
typedef struct { const uint8_t *src_addr; const uint8_t *des_addr; void *rx_ctrl; } esp_now_recv_info_t;
typedef enum { ESP_NOW_SEND_SUCCESS = 0, ESP_NOW_SEND_FAIL } esp_now_send_status_t;
typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *, const uint8_t *, int);
typedef void (*esp_now_send_cb_t)(const uint8_t *, esp_now_send_status_t);
typedef struct { uint8_t peer_addr[6]; uint8_t lmk[16]; uint8_t channel; int ifidx; bool encrypt; void *priv; } esp_now_peer_info_t;
esp_err_t esp_now_init(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len);
extern esp_now_recv_cb_t stub_espnow_recv_cb;            // ham nhan ma code that dang ky
#define STUB_MAX_SENT 2048
extern uint8_t stub_sent_buf[STUB_MAX_SENT][250];        // moi goi ESP-NOW code that gui di
extern size_t  stub_sent_len[STUB_MAX_SENT];
extern int     stub_sent_count;
extern esp_err_t stub_espnow_send_rc;                    // ma tra ve cua esp_now_send

typedef struct { int dummy; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
enum { WIFI_STORAGE_RAM = 0, WIFI_MODE_STA = 1, WIFI_PS_NONE = 0, WIFI_IF_STA = 0, WIFI_SECOND_CHAN_NONE = 0 };
esp_err_t esp_wifi_init(const wifi_init_config_t *c);
esp_err_t esp_wifi_set_storage(int s);
esp_err_t esp_wifi_set_mode(int m);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_ps(int ps);
esp_err_t esp_wifi_get_mac(int ifx, uint8_t mac[6]);
esp_err_t esp_wifi_set_channel(uint8_t ch, int second);
extern int stub_wifi_last_channel;
extern esp_err_t stub_wifi_channel_rc;
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);

// ---- NVS gia (RAM) ------------------------------------------------------------------
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v);
esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *v);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v);
esp_err_t nvs_set_i8(nvs_handle_t h, const char *k, int8_t v);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v);
void stub_nvs_reset(void);

// ---- Theo doi free(): dung de kiem tra "code co giai phong bo nho khong" --------------
#ifdef STUB_TRACK_FREE
void stub_track_free(void *p);
#define free(p) stub_track_free(p)
#endif
bool stub_was_freed(const void *p);            // true neu code that da free(p) (chi voi STUB_TRACK_FREE)
void stub_free_forget(void);
