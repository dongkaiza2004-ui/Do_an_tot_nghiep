#include "sys_monitor.h"
#include "ui.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include <stdio.h>

static temperature_sensor_handle_t s_temp_sensor = NULL;

static float read_cpu_load_percent(void)
{
    static TaskStatus_t tasks[16];
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count > 16) task_count = 16;

    uint32_t total_runtime = 0;
    UBaseType_t n = uxTaskGetSystemState(tasks, task_count, &total_runtime);
    if (total_runtime == 0) return 0.0f;

    uint32_t idle_runtime = 0;
    for (UBaseType_t i = 0; i < n; ++i) {
        if (strncmp(tasks[i].pcTaskName, "IDLE", 4) == 0) {
            idle_runtime += tasks[i].ulRunTimeCounter;
        }
    }
    // ESP32-S3 co 2 core -> 2 task IDLE, tong thoi gian ranh toi da la 2x total_runtime
    float idle_pct = (float)idle_runtime / (2.0f * total_runtime) * 100.0f;
    float load_pct = 100.0f - idle_pct;
    if (load_pct < 0) load_pct = 0;
    if (load_pct > 100) load_pct = 100;
    return load_pct;
}

static void format_uptime(char *buf, size_t len)
{
    int64_t sec = esp_timer_get_time() / 1000000;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    snprintf(buf, len, "%02d:%02d:%02d", h, m, s);
}

static void update_sys_info_cb(lv_timer_t *timer)
{
    // Chi cap nhat khi dang thuc su xem man CPU - do so tam thoi vao text de tranh work thua
    if (lv_scr_act() != ui_CPU) return;

    char buf[32];

    float cpu = read_cpu_load_percent();
    snprintf(buf, sizeof(buf), "%.0f %%", cpu);
    lv_label_set_text(ui_CPULoad, buf);

    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ram_free / 1024));
    lv_label_set_text(ui_RAM, buf);

    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(psram_free / 1024));
    lv_label_set_text(ui_PSRAM, buf);

    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(min_free / 1024));
    lv_label_set_text(ui_Minfreeheap, buf);

    
    size_t spiffs_total = 0, spiffs_used = 0;
    if (esp_spiffs_info("storage", &spiffs_total, &spiffs_used) == ESP_OK) {
        snprintf(buf, sizeof(buf), "%u/%u KB", (unsigned)(spiffs_used / 1024), (unsigned)(spiffs_total / 1024));
    } else {
        snprintf(buf, sizeof(buf), "N/A");
    }
    lv_label_set_text(ui_Flash, buf);

    format_uptime(buf, sizeof(buf));
    lv_label_set_text(ui_uptime, buf);

    const esp_app_desc_t *app_desc = esp_app_get_description();
    lv_label_set_text(ui_Firmwareversion, app_desc->version);

    if (s_temp_sensor) {
        float temp_c = 0;
        if (temperature_sensor_get_celsius(s_temp_sensor, &temp_c) == ESP_OK) {
            snprintf(buf, sizeof(buf), "%.1f C", temp_c);
            lv_label_set_text(ui_Temperature, buf);
        }
    }
}

void sys_monitor_start(void)
{
    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&temp_cfg, &s_temp_sensor) == ESP_OK) {
        temperature_sensor_enable(s_temp_sensor);
    }

    lv_timer_create(update_sys_info_cb, 1000, NULL);
}