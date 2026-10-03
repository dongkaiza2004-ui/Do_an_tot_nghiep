// door_sensor.cpp

#include "door_sensor.h"

#include <esp_log.h>
#include <driver/gpio.h>

#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <system/SystemClock.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using chip::System::Clock::Milliseconds32;
using chip::DeviceLayer::PlatformMgr;

namespace {

const char *TAG = "door_sensor";

constexpr int kPollIntervalMs = 50;
constexpr int kDebounceStableCount = 3;

gpio_num_t s_pin = GPIO_NUM_NC;

volatile bool s_isClosed = true;

// ------------------------------------------------------------------------
// s_hasPendingLock / s_pendingSource duoc doc/ghi tu 2 ngu canh khac nhau:
//   - door_sensor_task (mot task FreeRTOS tran, KHONG chay tren Matter
//     event loop) - ghi khi phat hien cua vua dong, va can DOC de biet co
//     dang cho hay khong.
//   - door_sensor_request_lock_when_closed() - duoc goi tu cac callback cua
//     Matter (lenh LockDoor tu app, nut bam qua ScheduleWork...) - ghi khi
//     co yeu cau khoa moi trong luc cua dang mo.
// Vi 2 ben o 2 thread khac nhau, PHAI dung critical section khi doc/ghi,
// khong duoc coi volatile la du (volatile chi tranh compiler toi uu sai,
// khong dam bao doc-sua-ghi la nguyen tu giua 2 core/2 task).
// ------------------------------------------------------------------------
portMUX_TYPE s_pendingLockMux = portMUX_INITIALIZER_UNLOCKED;
bool s_hasPendingLock = false;
BoltLockManager::OperationSource s_pendingSource = BoltLockManager::OperationSource::kUnspecified;

BoltLockManager::OperationSource s_autoRelockSource = BoltLockManager::OperationSource::kAuto;

void AutoRelockTimerHandler(chip::System::Layer *systemLayer, void *context)
{
    // Ham nay duoc chinh chip::System::Layer goi, tuc la DA CHAY tren Matter
    // event loop (giong het co che ActuatorTimerHandler cua BoltLockManager) -
    // an toan de goi thang door_sensor_request_lock_when_closed() (va qua do
    // la BoltLockManager::Lock()) ma khong can ScheduleWork them.
    ESP_LOGI(TAG, "Auto-relock (tu quan ly): het gio, thu khoa lai");
    door_sensor_request_lock_when_closed(s_autoRelockSource);
}

// Duoc goi tu Matter event loop (thong qua PlatformMgr().ScheduleWork trong
// door_sensor_task) khi cam bien vua bao cua dong va co lenh khoa dang cho.
// Day la CACH DUY NHAT DUNG de goi BoltLockManager::Lock() tu ket qua cua
// door_sensor_task, vi task do khong chay tren Matter thread - goi thang se
// vi pham assertion cua CHIP stack va lam sap ung dung (xem AssertChipStackLockedByCurrentThread).
void DoScheduledLockFromSensor(intptr_t arg)
{
    auto source = static_cast<BoltLockManager::OperationSource>(arg);
    BoltLockManager::Instance().Lock(source);
}

} // namespace

esp_err_t door_sensor_init(gpio_num_t pin)
{
    s_pin = pin;

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(pin);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config that bai: %s", esp_err_to_name(err));
        return err;
    }

    s_isClosed = (gpio_get_level(pin) == 0);
    ESP_LOGI(TAG, "door_sensor init OK (pin=%d), trang thai ban dau: %s",
             static_cast<int>(pin), s_isClosed ? "DONG" : "MO");
    return ESP_OK;
}

static void door_sensor_task(void *arg)
{
    int lastRaw = gpio_get_level(s_pin);
    int stableCount = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
        int raw = gpio_get_level(s_pin);

        if (raw == lastRaw) {
            if (stableCount < kDebounceStableCount) {
                stableCount++;
            }
        } else {
            stableCount = 0;
            lastRaw = raw;
        }

        if (stableCount == kDebounceStableCount) {
            bool nowClosed = (raw == 0);
            if (nowClosed != s_isClosed) {
                s_isClosed = nowClosed;
                ESP_LOGI(TAG, "Cua chuyen sang: %s", nowClosed ? "DONG" : "MO");

                if (nowClosed) {
                    bool shouldLock = false;
                    BoltLockManager::OperationSource source = BoltLockManager::OperationSource::kUnspecified;

                    portENTER_CRITICAL(&s_pendingLockMux);
                    if (s_hasPendingLock) {
                        shouldLock = true;
                        source = s_pendingSource;
                        s_hasPendingLock = false;
                    }
                    portEXIT_CRITICAL(&s_pendingLockMux);

                    if (shouldLock) {
                        ESP_LOGI(TAG, "Cua vua dong, len lich khoa da bi hoan lai truoc do");
                        intptr_t arg = static_cast<intptr_t>(source);
                        CHIP_ERROR err = PlatformMgr().ScheduleWork(DoScheduledLockFromSensor, arg);
                        if (err != CHIP_NO_ERROR) {
                            ESP_LOGE(TAG, "Khong the len lich khoa tu cam bien: %" CHIP_ERROR_FORMAT, err.Format());
                        }
                    }
                }
            }
        }
    }
}

void door_sensor_start()
{
    xTaskCreate(door_sensor_task, "door_sensor", 3072, nullptr, 4, nullptr);
}

bool door_sensor_is_closed()
{
    return s_isClosed;
}

bool door_sensor_request_lock_when_closed(BoltLockManager::OperationSource source)
{
    // Ham nay chi duoc goi tu cac ngu canh DA CHAY tren Matter event loop
    // (lenh LockDoor tu app, nut bam qua ScheduleWork, AutoRelockTimerHandler
    // o tren) - an toan de goi thang BoltLockManager::Lock() o day.
    if (s_isClosed) {
        BoltLockManager::Instance().Lock(source);
        return true;
    }

    ESP_LOGW(TAG, "Cua dang MO, hoan lenh khoa den khi cam bien bao DONG");
    portENTER_CRITICAL(&s_pendingLockMux);
    s_hasPendingLock = true;
    s_pendingSource = source;
    portEXIT_CRITICAL(&s_pendingLockMux);
    return false;
}

void door_sensor_arm_auto_relock(uint32_t delayMs, BoltLockManager::OperationSource source)
{
    chip::DeviceLayer::SystemLayer().CancelTimer(AutoRelockTimerHandler, nullptr);
    s_autoRelockSource = source;
    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(Milliseconds32(delayMs), AutoRelockTimerHandler, nullptr);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Khong dat duoc lich auto-relock: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void door_sensor_cancel_auto_relock()
{
    chip::DeviceLayer::SystemLayer().CancelTimer(AutoRelockTimerHandler, nullptr);
}