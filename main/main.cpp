#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "display_driver.h"
// Header UI xuat tu SquareLine Studio
#include "ui.h"

static const char *TAG = "TFT_TEST";

// ---- TFT ST7735 pins ----
#define TFT_CS   1
#define TFT_DC   2
#define TFT_RST  42
#define TFT_MOSI 40
#define TFT_SCLK 41
#define LCD_HOST SPI2_HOST

#define LCD_DRAW_W 128
#define LCD_DRAW_H 160

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

static lv_obj_t *focus_items[16];
static size_t focus_count = 0;
static size_t focus_index = 0;
static int active_screen = 1;
static bool serial_uart_ready = false;

static int read_serial_byte(TickType_t timeout)
{
    if (!serial_uart_ready) {
        return -1;
    }
    uint8_t byte = 0;
    return uart_read_bytes(UART_NUM_0, &byte, 1, timeout) == 1 ? byte : -1;
}

static void init_serial_input(void)
{
    esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        serial_uart_ready = true;
        ESP_LOGI(TAG, "Serial input UART0 ready");
    } else {
        ESP_LOGW(TAG, "Khong khoi tao duoc serial input: %s", esp_err_to_name(err));
    }
}

static bool is_effectively_hidden(lv_obj_t *object)
{
    for (lv_obj_t *current = object; current != NULL; current = lv_obj_get_parent(current)) {
        if (lv_obj_has_flag(current, LV_OBJ_FLAG_HIDDEN)) {
            return true;
        }
    }
    return false;
}

static void set_focus_items_for_screen(int screen)
{
    focus_count = 0;
    focus_index = 0;
    active_screen = screen;

    if (screen == 1) {
        focus_items[0] = ui_Button4;
        focus_items[1] = ui_Button1;
        focus_items[2] = ui_Button3;
        focus_items[3] = ui_Button5;
        focus_items[4] = ui_Button6;
        focus_items[5] = ui_Button7;
        focus_count = 6;
    } else if (screen == 2) {
        focus_items[0] = ui_Dropdown1;
        focus_items[1] = ui_Label21;
        focus_items[2] = ui_Label5;
        focus_items[3] = ui_Switch2;
        focus_items[4] = ui_Switch5;
        focus_items[5] = ui_Slider4;
        focus_items[6] = ui_Slider5;
        focus_items[7] = ui_Label36;
        focus_count = 8;
    } else if (screen == 3) {
        focus_items[0] = ui_Slider2;
        focus_items[1] = ui_Slider3;
        focus_items[2] = ui_Switch1;
        focus_items[3] = ui_Switch3;
        focus_items[4] = ui_Label39;
        focus_count = 5;
    } else {
        focus_items[0] = ui_Label23;
        focus_items[1] = ui_Label22;
        focus_items[2] = ui_Button9;
        focus_items[3] = ui_Label24;
        focus_items[4] = ui_Label29;
        focus_items[5] = ui_Label32;
        focus_items[6] = ui_Button8;
        focus_items[7] = ui_Label27;
        focus_items[8] = ui_Label33;
        focus_items[9] = ui_Label35;
        focus_items[10] = ui_Label40;
        focus_count = 11;
    }
}

static void apply_focus(void)
{
    for (size_t index = 0; index < focus_count; ++index) {
        lv_obj_clear_state(focus_items[index], LV_STATE_FOCUSED);
    }

    size_t checked = 0;
    while (checked < focus_count && is_effectively_hidden(focus_items[focus_index])) {
        focus_index = (focus_index + 1) % focus_count;
        ++checked;
    }
    if (checked == focus_count) {
        ESP_LOGW(TAG, "Khong co focus item dang hien tren Screen%d", active_screen);
        return;
    }
    lv_obj_add_state(focus_items[focus_index], LV_STATE_FOCUSED);
    ESP_LOGI(TAG, "Focus Screen%d item=%u", active_screen, (unsigned)(focus_index + 1));
}

static void load_test_screen(int screen)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Khong lay duoc LVGL mutex");
        return;
    }

    lv_obj_t *target = screen == 1 ? ui_Screen1 :
                       screen == 2 ? ui_Screen2 :
                       screen == 3 ? ui_Screen3 : ui_Screen4;
    lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    set_focus_items_for_screen(screen);
    apply_focus();
    ESP_LOGI(TAG, "Serial screen: Screen%d", screen);
    lvgl_port_unlock();
}

static void select_test_button(size_t index)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Khong lay duoc LVGL mutex");
        return;
    }

    focus_index = index % focus_count;
    apply_focus();
    lvgl_port_unlock();
}

static bool adjust_focused_control(int key)
{
    lv_obj_t *focused = focus_items[focus_index];

    if (focused == ui_Dropdown1) {
        if (!lv_dropdown_is_open(focused) && (key == '\n' || key == '\r')) {
            lv_dropdown_open(focused);
            return true;
        }
        if (lv_dropdown_is_open(focused)) {
            uint16_t option_count = lv_dropdown_get_option_cnt(focused);
            uint16_t selected = lv_dropdown_get_selected(focused);
            if (key == 'a' || key == 'A' || key == 'w' || key == 'W') {
                if (option_count > 0) {
                    lv_dropdown_set_selected(focused, (selected + option_count - 1) % option_count);
                }
                return true;
            }
            if (key == 'd' || key == 'D' || key == 's' || key == 'S') {
                if (option_count > 0) {
                    lv_dropdown_set_selected(focused, (selected + 1) % option_count);
                }
                return true;
            }
            if (key == '\n' || key == '\r') {
                lv_dropdown_close(focused);
                lv_event_send(focused, LV_EVENT_VALUE_CHANGED, NULL);
                return true;
            }
        }
    }

    if (lv_obj_check_type(focused, &lv_slider_class)) {
        int32_t value = lv_slider_get_value(focused);
        if (key == 'a' || key == 'A') {
            value -= 10;
        } else if (key == 'd' || key == 'D' || key == '\n' || key == '\r') {
            value += 10;
        } else {
            return false;
        }
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        lv_slider_set_value(focused, value, LV_ANIM_OFF);
        ESP_LOGI(TAG, "Slider value: %ld", (long)value);
        return true;
    }

    if (lv_obj_check_type(focused, &lv_switch_class) && (key == '\n' || key == '\r')) {
        lv_obj_t *switch_object = focused;
        if (lv_obj_has_state(switch_object, LV_STATE_CHECKED)) {
            lv_obj_clear_state(switch_object, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(switch_object, LV_STATE_CHECKED);
        }
        lv_event_send(switch_object, LV_EVENT_VALUE_CHANGED, NULL);
        ESP_LOGI(TAG, "Switch: %s", lv_obj_has_state(switch_object, LV_STATE_CHECKED) ? "ON" : "OFF");
        return true;
    }

    return false;
}

static bool is_enter_key(int key)
{
    return key == '\n' || key == '\r';
}

static bool ignore_lf_after_cr = false;

static void serial_button_task(void *)
{
    while (true) {
        int key = read_serial_byte(pdMS_TO_TICKS(20));
        if (key < 0) {
            continue;
        }
        if (key == '\n' && ignore_lf_after_cr) {
            ignore_lf_after_cr = false;
            continue;
        }
        if (ignore_lf_after_cr) {
            ignore_lf_after_cr = false;
        }
        if (key == '\r') {
            ignore_lf_after_cr = true;
        }

        size_t next = focus_index;
        if (lvgl_port_lock(1000)) {
            if (adjust_focused_control(key)) {
                lvgl_port_unlock();
                continue;
            }
            lvgl_port_unlock();
        }

        if (key == 'a' || key == 'A') {
            next = (focus_index + focus_count - 1) % focus_count;
        } else if (key == 'd' || key == 'D') {
            next = (focus_index + 1) % focus_count;
        } else if (key == 'w' || key == 'W') {
            next = (focus_index + focus_count - 1) % focus_count;
        } else if (key == 's' || key == 'S') {
            next = (focus_index + 1) % focus_count;
        } else if (is_enter_key(key)) {
            if (lvgl_port_lock(1000)) {
                lv_event_send(focus_items[focus_index], LV_EVENT_CLICKED, NULL);
                lvgl_port_unlock();
                vTaskDelay(pdMS_TO_TICKS(250));
                if (!lvgl_port_lock(1000)) {
                    ESP_LOGW(TAG, "Khong cap nhat focus sau khi chuyen screen");
                    continue;
                }
                int new_screen = lv_scr_act() == ui_Screen1 ? 1 :
                                 lv_scr_act() == ui_Screen2 ? 2 :
                                 lv_scr_act() == ui_Screen3 ? 3 : 4;
                set_focus_items_for_screen(new_screen);
                apply_focus();
                lvgl_port_unlock();
            }
            continue;
        } else if (key >= '1' && key <= '4') {
            load_test_screen(key - '0');
            continue;
        } else if (key == 'b' || key == 'B' || key == 0x08 || key == 0x7f) {
            load_test_screen(1);
            continue;
        } else if (key == 0x1b) {
            int bracket = read_serial_byte(pdMS_TO_TICKS(100));
            if (bracket != '[') {
                continue;
            }
            int arrow = read_serial_byte(pdMS_TO_TICKS(100));
            while (arrow >= '0' && arrow <= '9') {
                arrow = read_serial_byte(pdMS_TO_TICKS(100));
            }
            if (arrow == 'A' || arrow == 'D') next = (focus_index + focus_count - 1) % focus_count;
            if (arrow == 'B' || arrow == 'C') next = (focus_index + 1) % focus_count;
        } else {
            continue;
        }
        select_test_button(next);
    }
}

static void init_display(void)
{
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = TFT_SCLK;
    buscfg.mosi_io_num = TFT_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_DRAW_W * LCD_DRAW_H * 2 + 8;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = TFT_CS;
    io_config.dc_gpio_num = TFT_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 20 * 1000 * 1000; // 20 MHz
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = TFT_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));

    // Dat kich thuoc man hinh doc 128x160 cho khop voi thiet ke UI Menu
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Man hinh TFT ST7735 init OK");
}

extern "C" void app_main(void)
{
    // 1. Khoi tao phan cung màn hinh TFT
    init_display();
    display_lvgl_init(io_handle, panel_handle);
    // 2. Khoi tao giao dien LVGL UI xuat tu SquareLine
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Khong khoi tao duoc UI: LVGL mutex timeout");
        return;
    }
    ui_init();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL UI Init thanh cong!");
    ESP_LOGI(TAG, "LVGL color: depth=%d swap=%d sizeof=%u red=0x%04x blue=0x%04x",
             LV_COLOR_DEPTH, LV_COLOR_16_SWAP, (unsigned)sizeof(lv_color_t),
             (unsigned)lv_color_make(255, 0, 0).full,
             (unsigned)lv_color_make(0, 0, 255).full);

    set_focus_items_for_screen(1);
    select_test_button(0);
    init_serial_input();
    xTaskCreate(serial_button_task, "serial_button", 3072, NULL, 3, NULL);

    // LVGL port da co task rieng de xu ly timer va redraw.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}