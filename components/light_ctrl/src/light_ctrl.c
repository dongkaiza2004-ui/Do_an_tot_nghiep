// light_ctrl.c

#include "light_ctrl.h"
#include "app_state.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


//==================================================
// Logging
//==================================================

static const char *TAG = "light_ctrl";

//==================================================
// Light Sensor Configuration
//==================================================

#define LIGHT_ADC_UNIT     ADC_UNIT_1
#define LIGHT_ADC_CHANNEL  ADC_CHANNEL_2  
#define LIGHT_HYSTERESIS   15

//==================================================
// Runtime State
//==================================================

static adc_oneshot_unit_handle_t s_adc_handle;
static bool s_light_on = false;

//==================================================
// Forward Declaration
//==================================================

static int light_ctrl_read_raw(void);
static void light_task(void *arg);

//==================================================
// Initialization
//==================================================

esp_err_t light_ctrl_init(void)
{
    gpio_config_t io_conf = {0};

    io_conf.pin_bit_mask = (1ULL << LIGHT_LED_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);
    gpio_set_level(LIGHT_LED_GPIO, 0);

    //==================================================
    // ADC Configuration
    //==================================================

    adc_oneshot_unit_init_cfg_t init_cfg = {.unit_id = LIGHT_ADC_UNIT};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, LIGHT_ADC_CHANNEL, &chan_cfg));
    ESP_LOGI(TAG, "Light sensor (LDR/ADC) init OK - chan biet doi neu dung cam bien I2C");

    return ESP_OK;
}

//==================================================
// Light Sensor Reading
//==================================================

static int light_ctrl_read_raw(void)
{
    int raw = 0;
    adc_oneshot_read(s_adc_handle, LIGHT_ADC_CHANNEL, &raw);
    return raw;
}

//==================================================
// Light Control Task
//==================================================

static void light_task(void *arg)
{
    while (1) {
        int level = light_ctrl_read_raw();
        int threshold = app_state_get_light_threshold();

        if (!s_light_on && level < (threshold - LIGHT_HYSTERESIS)) {
            gpio_set_level(LIGHT_LED_GPIO, 1);
            s_light_on = true;

            ESP_LOGI(TAG, "Troi toi (raw=%d) -> BAT den", level);
        }
        else if (s_light_on && level > (threshold + LIGHT_HYSTERESIS)) {
            gpio_set_level(LIGHT_LED_GPIO, 0);
            s_light_on = false;

            ESP_LOGI(TAG, "Du sang (raw=%d) -> TAT den", level);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

//==================================================
// Light Control
//==================================================

void light_ctrl_start(void)
{
    xTaskCreate(light_task, "light_ctrl_task", 2048, NULL, 3, NULL);
}