// sd_storage.c

#include "sd_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sd_storage";

#define IMG_DIR       SD_STORAGE_MOUNT_POINT "/img"
#define TMP_PATH      IMG_DIR "/tmp.tmp"
#define LOG_PATH      SD_STORAGE_MOUNT_POINT "/events.log"
#define LOG_OLD_PATH  SD_STORAGE_MOUNT_POINT "/events.old"

#define LOG_QUEUE_LEN     16
#define LOG_TASK_STACK    6144
#define LOG_TAIL_BYTES    8192      // doc toi da bay nhieu byte cuoi file log khi khoi dong
#define IMAGE_MAX_BYTES   (1024 * 1024)

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static SemaphoreHandle_t s_mutex = NULL;   // bao ve s_image_count + ghi/xoa anh
static QueueHandle_t s_log_queue = NULL;
static int s_image_count = 0;

//==================================================
// Anh
//==================================================

static void image_path(uint32_t event_id, char *out, size_t n)
{
    snprintf(out, n, IMG_DIR "/%08" PRIX32 ".jpg", event_id);
}

// Ten anh hop le: 8 ky tu hex + ".jpg" (FAT khong LFN tra ve CHU HOA, nen so sanh khong phan biet hoa thuong).
static bool is_image_name(const char *name)
{
    if (strlen(name) != 12) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (!isxdigit((unsigned char)name[i])) {
            return false;
        }
    }
    return strcasecmp(name + 8, ".jpg") == 0;
}

static int count_images(void)
{
    DIR *d = opendir(IMG_DIR);
    if (!d) {
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (is_image_name(e->d_name)) {
            n++;
        }
    }
    closedir(d);
    return n;
}

// Goi khi dang giu s_mutex. "Cu nhat" = ten (event_id) nho nhat.
static void delete_oldest_locked(void)
{
    DIR *d = opendir(IMG_DIR);
    if (!d) {
        return;
    }
    char oldest[16] = "";
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!is_image_name(e->d_name)) {
            continue;
        }
        if (oldest[0] == '\0' || strcasecmp(e->d_name, oldest) < 0) {
            memcpy(oldest, e->d_name, 12);   
            oldest[12] = '\0';
        }
    }
    closedir(d);

    if (oldest[0] != '\0') {
        char path[48];
        snprintf(path, sizeof(path), IMG_DIR "/%s", oldest);
        if (remove(path) == 0) {
            if (s_image_count > 0) {
                s_image_count--;
            }
            ESP_LOGI(TAG, "Da day: xoa anh cu nhat %s", oldest);
        }
    }
}

esp_err_t sd_storage_save_image(uint32_t event_id, const uint8_t *data, size_t len)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0 || len > IMAGE_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[48];
    image_path(event_id, path, sizeof(path));

    esp_err_t result = ESP_FAIL;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    struct stat st;
    bool existed = (stat(path, &st) == 0);
    if (!existed && s_image_count >= SD_STORAGE_MAX_IMAGES) {
        delete_oldest_locked();
    }

    FILE *f = fopen(TMP_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Khong mo duoc %s (errno=%d)", TMP_PATH, errno);
    } else {
        size_t written = fwrite(data, 1, len, f);
        int close_rc = fclose(f);
        if (written == len && close_rc == 0) {
            if (existed) {
                remove(path);
            }
            if (rename(TMP_PATH, path) == 0) {
                result = ESP_OK;
                if (!existed) {
                    s_image_count++;
                }
            } else {
                ESP_LOGE(TAG, "rename -> %s that bai (errno=%d)", path, errno);
                remove(TMP_PATH);
            }
        } else {
            ESP_LOGE(TAG, "Ghi anh that bai (%u/%u byte) - the day hoac loi?",
                     (unsigned)written, (unsigned)len);
            remove(TMP_PATH);
        }
    }

    xSemaphoreGive(s_mutex);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Da luu anh event_id=%u (%u byte)", (unsigned)event_id, (unsigned)len);
    }
    return result;
}

bool sd_storage_image_exists(uint32_t event_id)
{
    if (!s_mounted) {
        return false;
    }
    char path[48];
    image_path(event_id, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0;
}

esp_err_t sd_storage_read_image(uint32_t event_id, uint8_t **out, size_t *out_len)
{
    if (!s_mounted || !out || !out_len) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[48];
    image_path(event_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > IMAGE_MAX_BYTES) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }
    *out = buf;
    *out_len = got;
    return ESP_OK;
}

//==================================================
// Lich su su kien (CSV): unix,event_id,type,face_user,matter_idx,similarity*1000
// Khong dung printf/scanf float hay 64-bit de khong phu thuoc cau hinh newlib.
//==================================================

static void write_event_line(const access_event_t *e)
{
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > SD_STORAGE_LOG_ROTATE_BYTES) {
        remove(LOG_OLD_PATH);
        rename(LOG_PATH, LOG_OLD_PATH);
        ESP_LOGI(TAG, "Xoay vong log: events.log -> events.old");
    }

    FILE *f = fopen(LOG_PATH, "a");
    if (!f) {
        ESP_LOGE(TAG, "Khong mo duoc %s (errno=%d)", LOG_PATH, errno);
        return;
    }
    fprintf(f, "%" PRIu32 ",%" PRIu32 ",%d,%u,%u,%d\n",
            (uint32_t)e->unix_time_s, e->event_id, (int)e->type,
            (unsigned)e->face_user_id, (unsigned)e->matter_user_index,
            (int)(e->similarity * 1000.0f));
    fclose(f);   // dong file moi lan de du lieu xuong the ngay (an toan khi mat dien)
}

static void log_task(void *arg)
{
    (void)arg;
    access_event_t evt;
    while (true) {
        if (xQueueReceive(s_log_queue, &evt, portMAX_DELAY) == pdTRUE) {
            write_event_line(&evt);
        }
    }
}

esp_err_t sd_storage_log_event(const access_event_t *event)
{
    if (!s_mounted || !s_log_queue || !event) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_log_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Hang doi log day, bo qua 1 su kien");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// Doc toi da max_count su kien o cuoi file, THU TU CU -> MOI.
static size_t parse_tail(const char *path, access_event_t *out, size_t max_count)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long start = size > LOG_TAIL_BYTES ? size - LOG_TAIL_BYTES : 0;
    fseek(f, start, SEEK_SET);

    char *buf = (char *)malloc(LOG_TAIL_BYTES + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t got = fread(buf, 1, LOG_TAIL_BYTES, f);
    fclose(f);
    buf[got] = '\0';

    char *p = buf;
    if (start > 0) {                       // bo dong bi cat do
        char *nl = strchr(p, '\n');
        p = nl ? nl + 1 : buf + got;
    }

    size_t n = 0;
    while (*p) {
        char *eol = strchr(p, '\n');
        if (eol) {
            *eol = '\0';
        }
        unsigned t, id, face, matter;
        int type, sim;
        if (sscanf(p, "%u,%u,%d,%u,%u,%d", &t, &id, &type, &face, &matter, &sim) == 6 &&
            type >= 0 && type <= ACCESS_EVENT_LIVENESS_FAIL) {
            if (n == max_count) {          // day -> dich 1 o, giu phan moi nhat
                memmove(out, out + 1, (max_count - 1) * sizeof(access_event_t));
                n--;
            }
            access_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.event_id = id;
            ev.type = (access_event_type_t)type;
            ev.face_user_id = (uint16_t)face;
            ev.matter_user_index = (uint16_t)matter;
            ev.similarity = (float)sim / 1000.0f;
            ev.unix_time_s = t;
            ev.timestamp_us = 0;           // moc boot cu khong con y nghia
            out[n++] = ev;
        }
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    free(buf);
    return n;
}

size_t sd_storage_load_recent_events(access_event_t *out, size_t max_count)
{
    if (!s_mounted || !out || max_count == 0) {
        return 0;
    }

    size_t n_new = parse_tail(LOG_PATH, out, max_count);
    if (n_new >= max_count) {
        return n_new;
    }

    // Chua du -> lay them tu events.old, xep TRUOC phan moi.
    size_t need = max_count - n_new;
    access_event_t *old = (access_event_t *)malloc(need * sizeof(access_event_t));
    if (!old) {
        return n_new;
    }
    size_t n_old = parse_tail(LOG_OLD_PATH, old, need);
    if (n_old > 0) {
        memmove(out + n_old, out, n_new * sizeof(access_event_t));
        memcpy(out, old, n_old * sizeof(access_event_t));
    }
    free(old);
    return n_old + n_new;
}

//==================================================
// Mount
//==================================================

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;     // 20 MHz, an toan cho day dan tren PCB

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;                 // so do: chan card-detect khong noi
    slot.wp = SDMMC_SLOT_NO_WP;
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = SD_STORAGE_PIN_CLK;
    slot.cmd = SD_STORAGE_PIN_CMD;
    slot.d0  = SD_STORAGE_PIN_D0;
    slot.d1  = SD_STORAGE_PIN_D1;
    slot.d2  = SD_STORAGE_PIN_D2;
    slot.d3  = SD_STORAGE_PIN_D3;
#endif
    // Mach da co dien tro keo 10k ngoai cho cac duong tin hieu -> khong bat pull-up trong.

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,        // TUYET DOI khong tu format the cua nguoi dung
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_STORAGE_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount the SD that bai: %s. Kiem tra: da cam the? the la FAT32? day dan?",
                 esp_err_to_name(err));
        s_card = NULL;
        return err;
    }

    mkdir(IMG_DIR, 0777);   // da ton tai thi bo qua
    s_image_count = count_images();

    s_log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(access_event_t));
    if (!s_log_queue ||
        xTaskCreate(log_task, "sd_log", LOG_TASK_STACK, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc task/queue ghi log");
        esp_vfs_fat_sdcard_unmount(SD_STORAGE_MOUNT_POINT, s_card);
        s_card = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD san sang, dang co %d anh", s_image_count);
    return ESP_OK;
}
