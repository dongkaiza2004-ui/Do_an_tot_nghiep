#include "ui_nav.h"
#include "nav_event.h"
#include "comm_uart.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "ui.h"
#include <string.h>

static const char *TAG = "ui_nav";

// ===================== Kiểu dữ liệu =====================
typedef struct {
    lv_obj_t *obj;
    const char *name;
    bool is_digit_field;   // true = SELECT vao day se chuyen sang che do nhap so
} nav_item_t;

typedef struct {
    lv_obj_t *screen;
    const char *screen_name;
    nav_item_t items[12];
    size_t item_count;
} nav_screen_t;
typedef struct {
    nav_item_t fields[4];   // Old, New, Confirm, Save (Save luon o cuoi)
    size_t field_count;
    size_t cur_field;
} pass_panel_t;

static pass_panel_t s_pass_panel_open;    // Panel8: cac field ...passwordopen
static pass_panel_t s_pass_panel_admin;   // Panel5: cac field ...adminpincode
static pass_panel_t *s_pass_active = NULL; // NULL = dang o tang 1 (danh sach tab)

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

// ===================== Home: 2 panel =====================
static nav_item_t s_home_g1[6];
static nav_item_t s_home_g2[6];
static bool   s_home_panel2 = false;
static size_t s_home_index = 0;

// ===================== State chung =====================
static int    s_cur_screen_idx = -1;   // -1 = dang o Home
static size_t s_cur_item = 0;

// ===================== State nhap so (dung chung cho moi label co is_digit_field) =====================
static lv_obj_t *s_digit_target = NULL;
static char   s_digit_buf[16];
static size_t s_digit_len = 0;

// Forward declare
static void apply_home_focus(void);
static void apply_screen_focus(nav_screen_t *s);
static void on_screen_loaded_cb(lv_event_t *e);

// ===================== Khoi tao bang dieu huong =====================
void ui_nav_init(void)
{
    nav_item_t g1[] = {
        { ui_Buttonsetting,    "Buttonsetting",    false },
        { ui_Buttonfacerecogn, "Buttonfacerecogn", false },
        { ui_ButtonQR,         "ButtonQR",          false },
        { ui_ButtonCPU,        "ButtonCPU",         false },
        { ui_Buttonadduser,    "Buttonadduser",     false },
        { ui_Buttonsubuser,    "Buttonsubuser",     false },
    };
    nav_item_t g2[] = {
        { ui_Buttonkeypad,      "Buttonkeypad",      false },
        { ui_Buttontest,        "Buttontest",        false },
        { ui_Buttonfinger,      "Buttonfinger",      false },
        { ui_Buttonconnectwifi, "Buttonconnectwifi", false },
        { ui_Buttonnouse1,      "Buttonnouse1",      false },
        { ui_buttonouse2,       "buttonouse2",       false },
    };
    memcpy(s_home_g1, g1, sizeof(g1));
    memcpy(s_home_g2, g2, sizeof(g2));

    // ---- ui_setting ----
    nav_item_t setting_items[] = {
        { ui_setModel,             "setModel",             false },
        { ui_setThreshold,         "setThreshold",         false },
        { ui_followCamera,         "followCamera",         false },
        { ui_SwitchFPS,            "SwitchFPS",            false },
        { ui_Switchborder,         "Switchborder",         false },
        { ui_setbacklight,         "setbacklight",         false },
        { ui_followchangepassword, "followchangepassword", false },
        { ui_backGUI1,             "backGUI1",             false },
    };
    reg_screen(ui_setting, "setting", setting_items, 8);

    // ---- ui_ConfigCamera ----
    nav_item_t cam_items[] = {
        { ui_setBrightness, "setBrightness", false },
        { ui_setContrast,   "setContrast",   false },
        { ui_SwitchFlip,    "SwitchFlip",    false },
        { ui_SwitchMirror,  "SwitchMirror",  false },
        { ui_backsetting1,  "backsetting1",  false },
    };
    reg_screen(ui_ConfigCamera, "ConfigCamera", cam_items, 5);

    // ---- ui_Changepasswword ----l.
    nav_item_t pass_items[] = {
        { ui_followpasswordopen, "followpasswordopen", false },
        { ui_followadminpincode, "followadminpincode", false },
        { ui_backsetting2,       "backsetting2",        false },
    };
    reg_screen(ui_Changepasswword, "Changepassword", pass_items, 3);

    nav_item_t open_fields[] = {
        { ui_oldpasswordopen,        "oldpasswordopen",        true },
        { ui_newpasswordopen,        "newpasswordopen",        true },
        { ui_confirmpasswordopen,    "confirmpasswordopen",    true },
        { ui_Buttonsavepasswordopen, "Buttonsavepasswordopen", false },
    };
    memcpy(s_pass_panel_open.fields, open_fields, sizeof(open_fields));
    s_pass_panel_open.field_count = 4;

    nav_item_t admin_fields[] = {
        { ui_oldadminpincode,        "oldadminpincode",        true },
        { ui_newadminpincode,        "newadminpincode",        true },
        { ui_confirmadminpincode,    "confirmadminpincode",    true },
        { ui_Buttonsaveadminpincode, "Buttonsaveadminpincode", false },
    };
    memcpy(s_pass_panel_admin.fields, admin_fields, sizeof(admin_fields));
    s_pass_panel_admin.field_count = 4;

    // ---- ui_Facerecognition ----
    nav_item_t face_items[] = { { ui_backGUI2, "backGUI2", false } };
    reg_screen(ui_Facerecognition, "Facerecognition", face_items, 1);

    // ---- ui_QRcode ----
    nav_item_t qr_items[] = { { ui_backGUI3, "backGUI3", false } };
    reg_screen(ui_QRcode, "QRcode", qr_items, 1);

    // ---- ui_CPU ----
    nav_item_t cpu_items[] = { { ui_backGUI4, "backGUI4", false } };
    reg_screen(ui_CPU, "CPU", cpu_items, 1);

    // ---- ui_Adduser ---- (tam thoi giu don gian, chua lam phan Name/Code)
    nav_item_t adduser_items[] = {
        { ui_Adduserfinger, "Adduserfinger", false },
        { ui_Adduserface,   "Adduserface",   false },
        { ui_backGUI5,      "backGUI5",      false },
    };
    reg_screen(ui_Adduser, "Adduser", adduser_items, 3);

    // ---- ui_Subuser ----
    nav_item_t subuser_items[] = {
        { ui_ListUser,      "ListUser",      false },
        { ui_Subuserfinger, "Subuserfinger", false },
        { ui_Subuserface,   "Subuserface",   false },
        { ui_backGUI6,      "backGUI6",      false },
    };
    reg_screen(ui_Subuser, "Subuser", subuser_items, 4);

    // Bat su kien "man da load xong" de tu dong dong bo lai focus,
    // tranh loi 200ms animation lam sai lech (nhu ban da phat hien).
    lv_obj_add_event_cb(ui_GUI, on_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    for (size_t i = 0; i < s_screen_count; ++i) {
        lv_obj_add_event_cb(s_screens[i].screen, on_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    }

    ESP_LOGI(TAG, "ui_nav_init: da dang ky %u man", (unsigned)s_screen_count);
    apply_home_focus();
}

// ===================== Focus helpers =====================
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

// Goi khi man hinh vua load xong hoan toan (het animation) - LVGL tu ban
// LV_EVENT_SCREEN_LOADED dung luc, khong bi lech 200ms nhu cach check
// truoc/sau lv_scr_act() thu cong.
static void on_screen_loaded_cb(lv_event_t *e)
{
    lv_obj_t *act = lv_scr_act();
    ESP_LOGI(TAG, "Man hinh moi da load xong (%p), dong bo lai focus", (void *)act);
    sync_to_screen(act);
}

// ===================== Nhap so dung chung =====================
static void digit_entry_start(lv_obj_t *label, const char *name)
{
    s_digit_target = label;
    s_digit_len = 0;
    s_digit_buf[0] = '\0';
    lv_label_set_text(label, "");
    comm_uart_set_mode(UART_MODE_DIGIT);
    ESP_LOGI(TAG, "[%s] bat dau nhap so", name);
}

static void digit_entry_add(char ch)
{
    if (!s_digit_target) return;
    if (s_digit_len < sizeof(s_digit_buf) - 1) {
        s_digit_buf[s_digit_len++] = ch;
        s_digit_buf[s_digit_len] = '\0';
        lv_label_set_text(s_digit_target, s_digit_buf);
        ESP_LOGI(TAG, "Nhap: %s", s_digit_buf);
    }
}

static void digit_entry_del(void)
{
    if (!s_digit_target) return;
    if (s_digit_len > 0) {
        s_digit_buf[--s_digit_len] = '\0';
        lv_label_set_text(s_digit_target, s_digit_buf);
        ESP_LOGI(TAG, "Xoa, con lai: %s", s_digit_buf);
    }
}

static void digit_entry_finish(void)
{
    ESP_LOGI(TAG, "Xac nhan xong gia tri: %s", s_digit_buf);
    comm_uart_set_mode(UART_MODE_NAV);
    s_digit_target = NULL;
}

// ===================== Widget dac biet: dropdown / slider / switch =====================
// action: 'E' = enter, 'I' = tang, 'D' = giam
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
        return (action == 'I' || action == 'D');  // dong ma nhan I/D -> khong lam gi
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
static void apply_pass_panel_focus(pass_panel_t *p)
{
    for (size_t i = 0; i < p->field_count; ++i) lv_obj_clear_state(p->fields[i].obj, LV_STATE_FOCUSED);
    lv_obj_add_state(p->fields[p->cur_field].obj, LV_STATE_FOCUSED);
    ESP_LOGI(TAG, "Focus (panel) -> [%s]", p->fields[p->cur_field].name);
}

static void handle_pass_panel_nav(nav_event_t ev)
{
    pass_panel_t *p = s_pass_active;
    nav_item_t *cur = &p->fields[p->cur_field];

    if (ev == NAV_SELECT) {
        if (cur->is_digit_field) {
            digit_entry_start(cur->obj, cur->name);
            return;
        }
        // Day la nut Save -> click (event co san se tu an panel), roi thoat ve tang 1
        ESP_LOGI(TAG, "SELECT (panel): click [%s] -> thoat panel", cur->name);
        lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);
        s_pass_active = NULL;
        apply_screen_focus(&s_screens[s_cur_screen_idx]);   // quay lai focus tang 1
        return;
    }
    if (ev == NAV_BACK) {
        // Chua co yeu cau ro rang cho Back trong panel - tam thoi coi nhu
        // "huy, thoat panel" (khong luu, khong bam Save) - bao lai neu ban
        // muon Back lam viec khac (vi du: chi xoa 1 field dang go).
        ESP_LOGI(TAG, "BACK (panel): thoat panel, ve tang 1");
        s_pass_active = NULL;
        apply_screen_focus(&s_screens[s_cur_screen_idx]);
        return;
    }
    if (ev == NAV_PREV || ev == NAV_NEXT) {
        // Vong tron trong panel - qua Save thi ve lai Old, KHONG thoat panel
        p->cur_field = (ev == NAV_NEXT)
            ? (p->cur_field + 1) % p->field_count
            : (p->cur_field + p->field_count - 1) % p->field_count;
        apply_pass_panel_focus(p);
    }
}

// ===================== Home =====================
static void handle_home_nav(nav_event_t ev)
{
    if (ev == NAV_SELECT) {
        nav_item_t *it = s_home_panel2 ? &s_home_g2[s_home_index] : &s_home_g1[s_home_index];
        ESP_LOGI(TAG, "SELECT: gui CLICKED toi [%s]", it->name);
        lv_event_send(it->obj, LV_EVENT_CLICKED, NULL);
        // Khong resync ngay tai day - on_screen_loaded_cb se tu lo neu doi man
        return;
    }
    if (ev == NAV_BACK) {
        ESP_LOGI(TAG, "Home: khong co man truoc, bo qua BACK");
        return;
    }
    if (ev != NAV_PREV && ev != NAV_NEXT) return;

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

// ===================== Man con =====================
static void handle_screen_nav(nav_event_t ev)
{
    nav_screen_t *s = &s_screens[s_cur_screen_idx];
    nav_item_t *cur = &s->items[s_cur_item];

    if (ev == NAV_SELECT) {
        // ---- Truong hop dac biet: 2 tab cua Changepassword ----
        if (cur->obj == ui_followpasswordopen) {
            lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);   // theo wiring THUC TE: hien Panel5
            s_pass_active = &s_pass_panel_open;
            s_pass_active->cur_field = 0;
            apply_pass_panel_focus(s_pass_active);
            return;
        }
        if (cur->obj == ui_followadminpincode) {
            lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);   // theo wiring THUC TE: hien Panel8
            s_pass_active = &s_pass_panel_admin;
            s_pass_active->cur_field = 0;
            apply_pass_panel_focus(s_pass_active);
            return;
        }
        if (cur->is_digit_field) {
            digit_entry_start(cur->obj, cur->name);
            return;
        }
        if (!handle_special_widget(cur->obj, 'E', cur->name)) {
            ESP_LOGI(TAG, "SELECT: gui CLICKED toi [%s]", cur->name);
            lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);
        }
        return;
    }
    if (ev == NAV_INC || ev == NAV_DEC) {
        handle_special_widget(cur->obj, (ev == NAV_INC) ? 'I' : 'D', cur->name);
        return;
    }
    if (ev == NAV_BACK) {
        nav_item_t *back = &s->items[s->item_count - 1];
        ESP_LOGI(TAG, "BACK: gui CLICKED toi [%s]", back->name);
        lv_event_send(back->obj, LV_EVENT_CLICKED, NULL);
        return;
    }
    if (ev == NAV_PREV || ev == NAV_NEXT) {
        s_cur_item = (ev == NAV_NEXT)
            ? (s_cur_item + 1) % s->item_count
            : (s_cur_item + s->item_count - 1) % s->item_count;
        apply_screen_focus(s);
    }
}

// ===================== Vong lap chinh =====================
static void handle_nav_msg(nav_msg_t msg)
{
    if (!lvgl_port_lock(1000)) return;

    // Uu tien che do nhap so neu dang bat
    if (s_digit_target != NULL) {
        if (msg.type == NAV_CHAR)        digit_entry_add(msg.ch);
        else if (msg.type == NAV_BACK)   digit_entry_del();
        else if (msg.type == NAV_SELECT) digit_entry_finish();
        lvgl_port_unlock();
        return;
    }
        if (s_pass_active != NULL) {             
        handle_pass_panel_nav(msg.type);         
        lvgl_port_unlock();                       
        return;                                 
    }           

    if (s_cur_screen_idx < 0) handle_home_nav(msg.type);
    else handle_screen_nav(msg.type);

    lvgl_port_unlock();
}

void ui_nav_task(void *arg)
{
    QueueHandle_t nav_queue = (QueueHandle_t)arg;

    nav_msg_t msg;
    while (true) {
        if (xQueueReceive(nav_queue, &msg, portMAX_DELAY)) {
            handle_nav_msg(msg);
        }
    }
}