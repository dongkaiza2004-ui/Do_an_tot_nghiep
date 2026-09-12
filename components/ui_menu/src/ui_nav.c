#include "ui_nav.h"
#include "nav_event.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "ui.h"
#include <string.h>

static const char *TAG = "ui_nav";

// ===== item có kèm tên để log cho dễ debug =====
typedef struct {
    lv_obj_t *obj;
    const char *name;
} nav_item_t;

typedef struct {
    lv_obj_t *screen;
    const char *screen_name;
    nav_item_t items[12];
    size_t item_count;
} nav_screen_t;

static nav_screen_t s_screens[8];
static size_t s_screen_count = 0;

static void reg_screen(lv_obj_t *screen, const char *screen_name, nav_item_t *items, size_t count)
{
    nav_screen_t *s = &s_screens[s_screen_count++];
    s->screen = screen;
    s->screen_name = screen_name;
    s->item_count = count;
    memcpy(s->items, items, sizeof(nav_item_t) * count);
}

// ===== Home: 2 panel =====
static nav_item_t s_home_g1[6];
static nav_item_t s_home_g2[6];
static bool   s_home_panel2 = false;
static size_t s_home_index = 0;

// ===== state chung =====
static int    s_cur_screen_idx = -1;   // -1 = đang ở Home
static size_t s_cur_item = 0;

static void apply_home_focus(void);

void ui_nav_init(void)
{
    nav_item_t g1[] = {
        { ui_Buttonsetting,   "Buttonsetting" },
        { ui_Buttonfacerecogn,"Buttonfacerecogn" },
        { ui_ButtonQR,        "ButtonQR" },
        { ui_ButtonCPU,       "ButtonCPU" },
        { ui_Buttonadduser,   "Buttonadduser" },
        { ui_Buttonsubuser,   "Buttonsubuser" },
    };
    nav_item_t g2[] = {
        { ui_Buttonkeypad,      "Buttonkeypad" },
        { ui_Buttontest,        "Buttontest" },
        { ui_Buttonfinger,      "Buttonfinger" },
        { ui_Buttonconnectwifi, "Buttonconnectwifi" },
        { ui_Buttonnouse1,      "Buttonnouse1" },
        { ui_buttonouse2,       "buttonouse2" },
    };
    memcpy(s_home_g1, g1, sizeof(g1));
    memcpy(s_home_g2, g2, sizeof(g2));

    nav_item_t setting_items[] = {
        { ui_setModel, "setModel" }, { ui_setThreshold, "setThreshold" },
        { ui_SwitchFPS, "SwitchFPS" }, { ui_Switchborder, "Switchborder" },
        { ui_setbacklight, "setbacklight" }, { ui_followCamera, "followCamera" },
        { ui_followchangepassword, "followchangepassword" }, { ui_backGUI1, "backGUI1" },
    };
    reg_screen(ui_setting, "setting", setting_items, 8);

    nav_item_t cam_items[] = {
        { ui_setBrightness, "setBrightness" }, { ui_setContrast, "setContrast" },
        { ui_SwitchFlip, "SwitchFlip" }, { ui_SwitchMirror, "SwitchMirror" },
        { ui_backsetting1, "backsetting1" },
    };
    reg_screen(ui_ConfigCamera, "ConfigCamera", cam_items, 5);

    nav_item_t pass_items[] = {
        { ui_oldpasswordopen, "oldpasswordopen" }, { ui_newpasswordopen, "newpasswordopen" },
        { ui_confirmpasswordopen, "confirmpasswordopen" }, { ui_Buttonsavepasswordopen, "Buttonsavepasswordopen" },
        { ui_oldadminpincode, "oldadminpincode" }, { ui_newadminpincode, "newadminpincode" },
        { ui_confirmadminpincode, "confirmadminpincode" }, { ui_Buttonsaveadminpincode, "Buttonsaveadminpincode" },
        { ui_backsetting2, "backsetting2" },
    };
    reg_screen(ui_Changepasswword, "Changepassword", pass_items, 9);

    nav_item_t face_items[] = { { ui_backGUI2, "backGUI2" } };
    reg_screen(ui_Facerecognition, "Facerecognition", face_items, 1);

    nav_item_t qr_items[] = { { ui_backGUI3, "backGUI3" } };
    reg_screen(ui_QRcode, "QRcode", qr_items, 1);

    nav_item_t cpu_items[] = { { ui_backGUI4, "backGUI4" } };
    reg_screen(ui_CPU, "CPU", cpu_items, 1);

    nav_item_t adduser_items[] = {
        { ui_Adduserfinger, "Adduserfinger" }, { ui_Adduserface, "Adduserface" },
        { ui_backGUI5, "backGUI5" }, { ui_backadduser, "backadduser" },
    };
    reg_screen(ui_Adduser, "Adduser", adduser_items, 4);

    nav_item_t subuser_items[] = {
        { ui_ListUser, "ListUser" }, { ui_Subuserfinger, "Subuserfinger" },
        { ui_Subuserface, "Subuserface" }, { ui_backGUI6, "backGUI6" },
    };
    reg_screen(ui_Subuser, "Subuser", subuser_items, 4);

    ESP_LOGI(TAG, "ui_nav_init: da dang ky %u man", (unsigned)s_screen_count);
    apply_home_focus();
}

// ===== focus helpers =====
static bool is_effectively_hidden(lv_obj_t *obj)
{
    for (lv_obj_t *cur = obj; cur != NULL; cur = lv_obj_get_parent(cur))
        if (lv_obj_has_flag(cur, LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}

static void clear_home_focus(void)
{
    for (int i = 0; i < 6; ++i) {
        lv_obj_clear_state(s_home_g1[i].obj, LV_STATE_FOCUSED);
        lv_obj_clear_state(s_home_g2[i].obj, LV_STATE_FOCUSED);
    }
}

static void apply_home_focus(void)
{
    clear_home_focus();
    nav_item_t *it = s_home_panel2 ? &s_home_g2[s_home_index] : &s_home_g1[s_home_index];
    lv_obj_add_state(it->obj, LV_STATE_FOCUSED);
    ESP_LOGI(TAG, "Focus -> Home/%s [%s]", s_home_panel2 ? "GUI2" : "GUI1", it->name);
}

static void apply_screen_focus(nav_screen_t *s)
{
    for (size_t i = 0; i < s->item_count; ++i) lv_obj_clear_state(s->items[i].obj, LV_STATE_FOCUSED);

    size_t checked = 0;
    while (checked < s->item_count && is_effectively_hidden(s->items[s_cur_item].obj)) {
        s_cur_item = (s_cur_item + 1) % s->item_count;
        ++checked;
    }
    if (checked == s->item_count) {
        ESP_LOGW(TAG, "Man %s: khong co item nao dang hien", s->screen_name);
        return;
    }
    lv_obj_add_state(s->items[s_cur_item].obj, LV_STATE_FOCUSED);
    ESP_LOGI(TAG, "Focus -> %s [%s]", s->screen_name, s->items[s_cur_item].name);
}

// Chỉ gọi khi CHẮC CHẮN màn đã đổi (so lv_scr_act() truoc/sau)
static void sync_to_screen(lv_obj_t *active)
{
    if (active == ui_GUI) {
        s_cur_screen_idx = -1;
        s_home_panel2 = false;
        s_home_index = 0;
        apply_home_focus();
        return;
    }
    for (size_t i = 0; i < s_screen_count; ++i) {
        if (s_screens[i].screen == active) {
            s_cur_screen_idx = (int)i;
            s_cur_item = 0;
            apply_screen_focus(&s_screens[i]);
            return;
        }
    }
    ESP_LOGW(TAG, "sync_to_screen: khong tim thay entry cho man dang active");
}

static void click_and_maybe_resync(lv_obj_t *target, const char *what)
{
    lv_obj_t *before = lv_scr_act();
    ESP_LOGI(TAG, "SELECT: gui CLICKED toi [%s]", what);
    lv_event_send(target, LV_EVENT_CLICKED, NULL);
    lv_obj_t *after = lv_scr_act();
    if (after != before) {
        ESP_LOGI(TAG, "Man hinh da doi, dong bo lai focus");
        sync_to_screen(after);
    } else {
        ESP_LOGI(TAG, "Man hinh khong doi, giu nguyen vi tri focus");
    }
}

// ===== xử lý widget đặc biệt: chỉ Enter (mở/đóng/toggle) và Inc/Dec (chỉnh giá trị) =====
// action: 'E' = enter, 'I' = increase, 'D' = decrease
// return true nếu đã xử lý xong (không cần lv_event_send CLICKED nữa)
static bool handle_special_widget(lv_obj_t *focused, char action, const char *name)
{
    if (lv_obj_check_type(focused, &lv_dropdown_class)) {
        bool open = lv_dropdown_is_open(focused);
        if (action == 'E') {
            if (!open) { lv_dropdown_open(focused); ESP_LOGI(TAG, "[%s] mo dropdown", name); }
            else {
                lv_dropdown_close(focused);
                lv_event_send(focused, LV_EVENT_VALUE_CHANGED, NULL);
                ESP_LOGI(TAG, "[%s] dong dropdown, xac nhan chon", name);
            }
            return true;
        }
        if (open && (action == 'I' || action == 'D')) {
            uint16_t cnt = lv_dropdown_get_option_cnt(focused);
            uint16_t sel = lv_dropdown_get_selected(focused);
            sel = (action == 'I') ? (sel + 1) % cnt : (sel + cnt - 1) % cnt;
            lv_dropdown_set_selected(focused, sel);
            ESP_LOGI(TAG, "[%s] dropdown chon index=%u", name, sel);
            return true;
        }
        // dropdown dong ma nhan I/D -> khong lam gi, de NAV_PREV/NEXT lo di chuyen
        return (action == 'I' || action == 'D');
    }

    if (lv_obj_check_type(focused, &lv_slider_class) && (action == 'I' || action == 'D')) {
        int32_t v = lv_slider_get_value(focused);
        v += (action == 'I') ? 5 : -5;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        lv_slider_set_value(focused, v, LV_ANIM_OFF);
        lv_event_send(focused, LV_EVENT_VALUE_CHANGED, NULL);
        ESP_LOGI(TAG, "[%s] slider = %ld", name, (long)v);
        return true;
    }

    if (lv_obj_check_type(focused, &lv_switch_class) && action == 'E') {
        bool checked = lv_obj_has_state(focused, LV_STATE_CHECKED);
        if (checked) lv_obj_clear_state(focused, LV_STATE_CHECKED);
        else lv_obj_add_state(focused, LV_STATE_CHECKED);
        lv_event_send(focused, LV_EVENT_VALUE_CHANGED, NULL);
        ESP_LOGI(TAG, "[%s] switch = %s", name, checked ? "OFF" : "ON");
        return true;
    }

    return false;
}

// ===== Home =====
static void handle_home_nav(nav_event_t ev)
{
    if (ev == NAV_SELECT) {
        nav_item_t *it = s_home_panel2 ? &s_home_g2[s_home_index] : &s_home_g1[s_home_index];
        click_and_maybe_resync(it->obj, it->name);
        return;
    }
    if (ev == NAV_BACK) {
        ESP_LOGI(TAG, "Home: khong co man truoc, bo qua BACK");
        return;
    }
    if (ev != NAV_PREV && ev != NAV_NEXT) return;   // A/D khong co tac dung o Home

    int flat = (s_home_panel2 ? 6 : 0) + (int)s_home_index;
    flat += (ev == NAV_NEXT) ? 1 : -1;
    if (flat < 0) flat += 12;
    if (flat >= 12) flat -= 12;

    bool new_panel2 = (flat >= 6);
    size_t new_index = (size_t)(flat % 6);

    if (new_panel2 != s_home_panel2) {
        lv_obj_add_flag(s_home_panel2 ? ui_GUI2 : ui_GUI1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(new_panel2 ? ui_GUI2 : ui_GUI1, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Home: chuyen panel -> %s", new_panel2 ? "GUI2" : "GUI1");
    }
    s_home_panel2 = new_panel2;
    s_home_index = new_index;
    apply_home_focus();
}

// ===== Màn con =====
static void handle_screen_nav(nav_event_t ev)
{
    nav_screen_t *s = &s_screens[s_cur_screen_idx];
    nav_item_t *cur = &s->items[s_cur_item];

    if (ev == NAV_SELECT) {
        if (!handle_special_widget(cur->obj, 'E', cur->name)) {
            click_and_maybe_resync(cur->obj, cur->name);
        }
        return;
    }
    if (ev == NAV_INC || ev == NAV_DEC) {
        handle_special_widget(cur->obj, (ev == NAV_INC) ? 'I' : 'D', cur->name);
        return;   // A/D KHONG BAO GIO di chuyen focus
    }
    if (ev == NAV_BACK) {
        // back luon la item cuoi trong danh sach theo quy uoc dang khai bao
        nav_item_t *back = &s->items[s->item_count - 1];
        click_and_maybe_resync(back->obj, back->name);
        return;
    }
    if (ev == NAV_PREV || ev == NAV_NEXT) {
        s_cur_item = (ev == NAV_NEXT)
            ? (s_cur_item + 1) % s->item_count
            : (s_cur_item + s->item_count - 1) % s->item_count;
        apply_screen_focus(s);
    }
}

static void handle_nav_event(nav_event_t ev)
{
    if (!lvgl_port_lock(1000)) return;
    if (s_cur_screen_idx < 0) handle_home_nav(ev);
    else handle_screen_nav(ev);
    lvgl_port_unlock();
}

void ui_nav_task(void *arg)
{
    QueueHandle_t nav_queue = (QueueHandle_t)arg;

    nav_event_t ev;
    while (true) {
        if (xQueueReceive(nav_queue, &ev, portMAX_DELAY)) {
            handle_nav_event(ev);
        }
    }
}