// servo_door.cpp

#include "servo_door.h"

#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace {

const char *TAG = "servo_door";

constexpr ledc_mode_t      kMode       = LEDC_LOW_SPEED_MODE;  // ESP32-S3 chi co low-speed
constexpr ledc_timer_t     kTimer      = LEDC_TIMER_1;
constexpr ledc_channel_t   kChannel    = LEDC_CHANNEL_1;
constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_14_BIT;
constexpr uint32_t         kFreqHz     = 50;
constexpr uint32_t         kPeriodUs   = 1000000 / kFreqHz;    // 20000us

bool s_ready = false;
esp_timer_handle_t s_idle_timer = nullptr;

uint32_t AngleToDuty(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t pulseUs = kServoMinPulseUs + (kServoMaxPulseUs - kServoMinPulseUs) * angle / 180;
    return (pulseUs * (1u << kResolution)) / kPeriodUs;
}

// Tat PWM sau khi servo da quay xong (chay tren esp_timer task).
void IdleTimerCb(void * /*arg*/)
{
    ledc_stop(kMode, kChannel, 0);
}

void MoveTo(int angle)
{
    esp_timer_stop(s_idle_timer);   // huy lich tat cu (loi ESP_ERR_INVALID_STATE neu chua chay -> bo qua)
    ledc_set_duty(kMode, kChannel, AngleToDuty(angle));
    ledc_update_duty(kMode, kChannel);
    esp_timer_start_once(s_idle_timer, (uint64_t)kServoHoldMs * 1000);
}

} // namespace

esp_err_t servo_door_init(void)
{
    ledc_timer_config_t timerCfg = {};
    timerCfg.speed_mode      = kMode;
    timerCfg.duty_resolution = kResolution;
    timerCfg.timer_num       = kTimer;
    timerCfg.freq_hz         = kFreqHz;
    timerCfg.clk_cfg         = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timerCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config that bai: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t chCfg = {};
    chCfg.gpio_num   = kServoPin;
    chCfg.speed_mode = kMode;
    chCfg.channel    = kChannel;
    chCfg.timer_sel  = kTimer;
    chCfg.duty       = 0;
    chCfg.hpoint     = 0;
    err = ledc_channel_config(&chCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config that bai: %s", esp_err_to_name(err));
        return err;
    }

    esp_timer_create_args_t tArgs = {};
    tArgs.callback = IdleTimerCb;
    tArgs.name = "servo_idle";
    err = esp_timer_create(&tArgs, &s_idle_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create that bai: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "servo init OK (GPIO%d, dong=%d do, mo=%d do)",
             (int)kServoPin, kServoClosedAngle, kServoOpenAngle);
    MoveTo(kServoClosedAngle);   // khoi dong o trang thai KHOA
    return ESP_OK;
}

void servo_door_set_open(bool open)
{
    if (!s_ready) {
        return;
    }
    ESP_LOGI(TAG, "servo -> %s", open ? "MO" : "DONG");
    MoveTo(open ? kServoOpenAngle : kServoClosedAngle);
}
