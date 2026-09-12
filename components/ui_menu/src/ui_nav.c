#include "ui_nav.h"
#include "nav_event.h"
#include "comm_uart.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "ui.h"
#include <string.h>

static const char *TAG = "ui_nav";

// ===================== Kieu du lieu =====================
typedef struct {
    lv_obj_t *obj;
    const char *name;
    bool is_text_field;   // Enter -> bat che do nhap (ghi thang ky tu nhan duoc)
    int  subpanel_id;     // >=0: Enter -> mo subpanel nay; -1 = binh thuong
    bool closes_panel;    // dung trong subpanel: Enter -> click roi dong panel
} nav_item_t;

typedef struct {
    lv_obj_t *screen;
    const char *name;
    nav_item_t items[10];
    size_t item_count;
    bool has_back;
} nav_screen_t;

typedef struct {
    nav_item_t items[6];
    size_t count;
    size_t cur;
} subpanel_t;

static nav_screen_t s_screens[16];
static size_t s_screen_count = 0;
static int s_cur_screen_idx = 0;
static size_t s_cur_item = 0;

static subpanel_t s_subpanels[4];
static int s_subpanel_count = 0;
static int s_active_subpanel = -1;

#define ITEM(o,n)              (nav_item_t){ o, n, false, -1, false }
#define ITEM_TEXT(o,n)         (nav_item_t){ o, n, true,  -1, false }
#define ITEM_SUBPANEL(o,n,id)  (nav_item_t){ o, n, false, id, false }
#define ITEM_CLOSE(o,n)        (nav_item_t){ o, n, false, -1, true  }

static int reg_screen(lv_obj_t *screen, const char *name, nav_item_t *items, size_t count, bool has_back)
{
    nav_screen_t *s = &s_screens[s_screen_count];
    s->screen = screen;
    s->name = name;
    s->item_count = count;
    s->has_back = has_back;
    memcpy(s->items, items, sizeof(nav_item_t) * count);
    return (int)s_screen_count++;
}

static int reg_subpanel(nav_item_t *items, size_t count)
{
    subpanel_t *p = &s_subpanels[s_subpanel_count];
    p->count = count;
    p->cur = 0;
    memcpy(p->items, items, sizeof(nav_item_t) * count);
    return s_subpanel_count++;
}

static void apply_screen_focus_at(nav_screen_t *s, size_t start);
static void on_screen_loaded_cb(lv_event_t *e);

// ===================== Khoi tao =====================
void ui_nav_init(void)
{
    // ---- subpanel truoc (can id de gan vao item cua man cha) ----
    nav_item_t pass_open_fields[] = {
        ITEM_TEXT(ui_oldpasswordopen,     "oldpasswordopen"),
        ITEM_TEXT(ui_newpasswordopen,     "newpasswordopen"),
        ITEM_TEXT(ui_confirmpasswordopen, "confirmpasswordopen"),
        ITEM_CLOSE(ui_Buttonsavepasswordopen, "Buttonsavepasswordopen"),
    };
    int SP_PASS_OPEN = reg_subpanel(pass_open_fields, 4);

    nav_item_t pass_admin_fields[] = {
        ITEM_TEXT(ui_oldadminpincode,     "oldadminpincode"),
        ITEM_TEXT(ui_newadminpincode,     "newadminpincode"),
        ITEM_TEXT(ui_confirmadminpincode, "confirmadminpincode"),
        ITEM_CLOSE(ui_Buttonsaveadminpincode, "Buttonsaveadminpincode"),
    };
    int SP_PASS_ADMIN = reg_subpanel(pass_admin_fields, 4);

    nav_item_t adduser_face_fields[] = {
        ITEM_CLOSE(ui_backadduser, "backadduser"),
    };
    int SP_ADDUSER_FACE = reg_subpanel(adduser_face_fields, 1);

    // ---- Home ----
    nav_item_t home_items[] = {
        ITEM(ui_Buttonsetting,    "Buttonsetting"),
        ITEM(ui_Buttonfacerecogn, "Buttonfacerecogn"),
        ITEM(ui_Buttonkeypad,     "Buttonkeypad"),
        ITEM(ui_Buttonfinger,     "Buttonfinger"),
        ITEM(ui_Buttonadduser,    "Buttonadduser"),
        ITEM(ui_ButtonQR,         "ButtonQR"),
    };
    reg_screen(ui_GUI, "GUI", home_items, 6, false);

    // ---- setting ----
    nav_item_t setting_items[] = {
        ITEM(ui_setModel,             "setModel"),
        ITEM(ui_setThreshold,         "setThreshold"),
        ITEM(ui_followCamera,         "followCamera"),
        ITEM(ui_SwitchFPS,            "SwitchFPS"),
        ITEM(ui_Switchborder,         "Switchborder"),
        ITEM(ui_setbacklight,         "setbacklight"),
        ITEM(ui_followchangepassword, "followchangepassword"),
        ITEM(ui_followotherscreen,    "followotherscreen"),
        ITEM(ui_backGUI1,             "backGUI1"),
    };
    reg_screen(ui_setting, "setting", setting_items, 9, true);

    // ---- ConfigCamera ----
    nav_item_t cam_items[] = {
        ITEM(ui_setBrightness, "setBrightness"),
        ITEM(ui_setContrast,   "setContrast"),
        ITEM(ui_SwitchFlip,    "SwitchFlip"),
        ITEM(ui_SwitchMirror,  "SwitchMirror"),
        ITEM(ui_backsetting1,  "backsetting1"),
    };
    reg_screen(ui_ConfigCamera, "ConfigCamera", cam_items, 5, true);

    // ---- Changepasswword: 2 tab mo subpanel ----
    nav_item_t pass_items[] = {
        ITEM_SUBPANEL(ui_followpasswordopen, "followpasswordopen", SP_PASS_OPEN),
        ITEM_SUBPANEL(ui_followadminpincode, "followadminpincode", SP_PASS_ADMIN),
        ITEM(ui_backsetting2, "backsetting2"),
    };
    reg_screen(ui_Changepasswword, "Changepassword", pass_items, 3, true);

    // ---- Facerecognition / QRcode ----
    nav_item_t face_items[] = { ITEM(ui_backGUI2, "backGUI2") };
    reg_screen(ui_Facerecognition, "Facerecognition", face_items, 1, true);

    nav_item_t qr_items[] = { ITEM(ui_backGUI3, "backGUI3") };
    reg_screen(ui_QRcode, "QRcode", qr_items, 1, true);

    // ---- otherscreen (hub) ----
    nav_item_t other_items[] = {
        ITEM(ui_follownetwork, "follownetwork"),
        ITEM(ui_followCPU,     "followCPU"),
        ITEM(ui_followhelp,    "followhelp"),
        ITEM(ui_backsetting3,  "backsetting3"),
    };
    reg_screen(ui_otherscreen, "otherscreen", other_items, 4, true);

    // ---- network ----
    nav_item_t net_items[] = {
        ITEM_TEXT(ui_ConnectingSSID, "ConnectingSSID"),
        ITEM(ui_QRconnectnetwork,    "QRconnectnetwork"),
        ITEM(ui_backotherscreen3,    "backotherscreen3"),
    };
    reg_screen(ui_network, "network", net_items, 3, true);

    // ---- CPU / help ----
    nav_item_t cpu_items[] = { ITEM(ui_backotherscreen, "backotherscreen") };
    reg_screen(ui_CPU, "CPU", cpu_items, 1, true);

    nav_item_t help_items[] = { ITEM(ui_backotherscreen1, "backotherscreen1") };
    reg_screen(ui_help, "help", help_items, 1, true);

    // ---- User (hub) ----
    nav_item_t user_items[] = {
        ITEM(ui_followadduser,       "followadduser"),
        ITEM(ui_followsubuser,       "followsubuser"),
        ITEM(ui_followunlockhistory, "followunlockhistory"),
        ITEM(ui_backGUI9,            "backGUI9"),
    };
    reg_screen(ui_User, "User", user_items, 4, true);

    // ---- Adduser ----
    nav_item_t adduser_items[] = {
        ITEM_TEXT(ui_NameAdduser,     "NameAdduser"),
        ITEM_TEXT(ui_Addusercode,     "Addusercode"),
        ITEM(ui_Adduserfinger,        "Adduserfinger"),
        ITEM_SUBPANEL(ui_Adduserface, "Adduserface", SP_ADDUSER_FACE),
        ITEM(ui_backuser2,            "backuser2"),
    };
    reg_screen(ui_Adduser, "Adduser", adduser_items, 5, true);

    // ---- Subuser ----
    nav_item_t subuser_items[] = {
        ITEM(ui_ListUserfinger, "ListUserfinger"),
        ITEM(ui_Subuserfinger,  "Subuserfinger"),
        ITEM(ui_ListUserface,   "ListUserface"),
        ITEM(ui_Subuserface,    "Subuserface"),
        ITEM(ui_backuser3,      "backuser3"),
    };
    reg_screen(ui_Subuser, "Subuser", subuser_items, 5, true);

    // ---- UnlockHistory ----
    nav_item_t hist_items[] = { ITEM(ui_backuser1, "backuser1") };
    reg_screen(ui_UnlockHistory, "UnlockHistory", hist_items, 1, true);

    // ---- Keypad ----
    nav_item_t keypad_items[] = {
        ITEM_TEXT(ui_inputopencode, "inputopencode"),
        ITEM(ui_backGUI7, "backGUI7"),
    };
    reg_screen(ui_Keypad, "Keypad", keypad_items, 2, true);

    // ---- fingerprint ----
    nav_item_t finger_items[] = { ITEM(ui_backGUI8, "backGUI8") };
    reg_screen(ui_fingerprint, "fingerprint", finger_items, 1, true);

    for (size_t i = 0; i < s_screen_count; ++i) {
        lv_obj_add_event_cb(s_screens[i].screen, on_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    }

    ESP_LOGI(TAG, "ui_nav_init: %u man, %u subpanel", (unsigned)s_screen_count, (unsigned)s_subpanel_count);
    apply_screen_focus_at(&s_screens[0], 0);   // Home la index 0
}

// ===================== Focus helpers =====================
static bool is_effectively_hidden(lv_obj_t *obj)
{
    for (lv_obj_t *cur = obj; cur != NULL; cur = lv_obj_get_parent(cur))
        if (lv_obj_has_flag(cur, LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}

static void apply_screen_focus_at(nav_screen_t *s, size_t start)
{
    for (size_t i = 0; i < s->item_count; ++i) lv_obj_clear_state(s->items[i].obj, LV_STATE_FOCUSED);
    s_cur_item = start;
    size_t checked = 0;
    while (checked < s->item_count && is_effectively_hidden(s->items[s_cur_item].obj)) {
        s_cur_item = (s_cur_item + 1) % s->item_count;
        ++checked;
    }
    if (checked == s->item_count) { ESP_LOGW(TAG, "%s: khong co item hien", s->name); return; }
    lv_obj_add_state(s->items[s_cur_item].obj, LV_STATE_FOCUSED);
    ESP_LOGI(TAG, "Focus -> %s [%s]", s->name, s->items[s_cur_item].name);
}

static void apply_subpanel_focus(subpanel_t *p)
{
    for (size_t i = 0; i < p->count; ++i) lv_obj_clear_state(p->items[i].obj, LV_STATE_FOCUSED);
    lv_obj_add_state(p->items[p->cur].obj, LV_STATE_FOCUSED);
    ESP_LOGI(TAG, "Focus (subpanel) -> [%s]", p->items[p->cur].name);
}

static void sync_to_screen(lv_obj_t *active)
{
    for (size_t i = 0; i < s_screen_count; ++i) {
        if (s_screens[i].screen == active) {
            s_cur_screen_idx = (int)i;
            s_active_subpanel = -1;
            apply_screen_focus_at(&s_screens[i], 0);
            return;
        }
    }
    ESP_LOGW(TAG, "sync_to_screen: khong khop man nao");
}

static void on_screen_loaded_cb(lv_event_t *e)
{
    lv_obj_t *act = lv_scr_act();
    ESP_LOGI(TAG, "Man moi da load xong, dong bo focus");
    sync_to_screen(act);
}

// ===================== Nhap 1 o (ky tu nhan tu UART da la ky tu that) =====================
static lv_obj_t *s_text_target = NULL;
static char   s_text_buf[32];
static size_t s_text_len = 0;

static void text_entry_start(lv_obj_t *label, const char *name)
{
    s_text_target = label;
    s_text_len = 0;
    s_text_buf[0] = '\0';
    lv_label_set_text(label, "");
    comm_uart_set_mode(UART_MODE_TEXT);
    ESP_LOGI(TAG, "[%s] bat dau nhap", name);
}

static void text_entry_key(char c)
{
    if (!s_text_target) return;
    if (s_text_len < sizeof(s_text_buf) - 1) {
        s_text_buf[s_text_len++] = c;
        s_text_buf[s_text_len] = '\0';
        lv_label_set_text(s_text_target, s_text_buf);
        ESP_LOGI(TAG, "Nhap: %s", s_text_buf);
    }
}

static void text_entry_del(void)
{
    if (!s_text_target) return;
    if (s_text_len > 0) {
        s_text_buf[--s_text_len] = '\0';
        lv_label_set_text(s_text_target, s_text_buf);
        ESP_LOGI(TAG, "Xoa, con lai: %s", s_text_buf);
    }
}

static void text_entry_finish(void)
{
    ESP_LOGI(TAG, "Xac nhan xong: %s", s_text_buf);
    comm_uart_set_mode(UART_MODE_NAV);
    s_text_target = NULL;
}

// ===================== Widget dac biet: dropdown / slider / switch =====================
static bool handle_special_widget(lv_obj_t *focused, char action, const char *name)
{
    if (lv_obj_check_type(focused, &lv_dropdown_class)) {
        bool open = lv_dropdown_is_open(focused);
        if (action == 'E') {
            if (!open) lv_dropdown_open(focused);
            else {
                lv_dropdown_close(focused);
                lv_event_send(focused, LV_EVENT_VALUE_CHANGED, NULL);
            }
            return true;
        }
        if (open && (action == 'I' || action == 'D')) {
            uint16_t cnt = lv_dropdown_get_option_cnt(focused);
            uint16_t sel = lv_dropdown_get_selected(focused);
            sel = (action == 'I') ? (sel + 1) % cnt : (sel + cnt - 1) % cnt;
            lv_dropdown_set_selected(focused, sel);
            return true;
        }
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
        return true;
    }
    return false;
}

// ===================== Man con (Home dung chung ham nay) =====================
static void handle_screen_nav(nav_event_t ev)
{
    nav_screen_t *s = &s_screens[s_cur_screen_idx];
    nav_item_t *cur = &s->items[s_cur_item];

    if (ev == NAV_SELECT) {
        if (cur->subpanel_id >= 0) {
            lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);
            s_active_subpanel = cur->subpanel_id;
            s_subpanels[s_active_subpanel].cur = 0;
            apply_subpanel_focus(&s_subpanels[s_active_subpanel]);
            return;
        }
        if (cur->is_text_field) { text_entry_start(cur->obj, cur->name); return; }
        if (!handle_special_widget(cur->obj, 'E', cur->name)) {
            ESP_LOGI(TAG, "SELECT: click [%s]", cur->name);
            lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);
        }
        return;
    }
    if (ev == NAV_INC || ev == NAV_DEC) {
        handle_special_widget(cur->obj, (ev == NAV_INC) ? 'I' : 'D', cur->name);
        return;
    }
    if (ev == NAV_BACK) {
        if (!s->has_back) { ESP_LOGI(TAG, "%s: khong co man truoc", s->name); return; }
        nav_item_t *back = &s->items[s->item_count - 1];
        ESP_LOGI(TAG, "BACK: click [%s]", back->name);
        lv_event_send(back->obj, LV_EVENT_CLICKED, NULL);
        return;
    }
    if (ev == NAV_PREV || ev == NAV_NEXT) {
        s_cur_item = (ev == NAV_NEXT)
            ? (s_cur_item + 1) % s->item_count
            : (s_cur_item + s->item_count - 1) % s->item_count;
        apply_screen_focus_at(s, s_cur_item);
    }
}

// ===================== Subpanel (Changepassword + Adduser->Panel1) =====================
static void handle_subpanel_nav(nav_event_t ev)
{
    subpanel_t *p = &s_subpanels[s_active_subpanel];
    nav_item_t *cur = &p->items[p->cur];

    if (ev == NAV_SELECT) {
        if (cur->is_text_field) { text_entry_start(cur->obj, cur->name); return; }
        if (!handle_special_widget(cur->obj, 'E', cur->name)) {
            ESP_LOGI(TAG, "SELECT (panel): click [%s]", cur->name);
            lv_event_send(cur->obj, LV_EVENT_CLICKED, NULL);
        }
        if (cur->closes_panel) {
            s_active_subpanel = -1;
            apply_screen_focus_at(&s_screens[s_cur_screen_idx], s_cur_item);
        }
        return;
    }
    if (ev == NAV_INC || ev == NAV_DEC) {
        handle_special_widget(cur->obj, (ev == NAV_INC) ? 'I' : 'D', cur->name);
        return;
    }
    if (ev == NAV_BACK) {
        ESP_LOGI(TAG, "BACK (panel): huy, quay ve man cha");
        s_active_subpanel = -1;
        apply_screen_focus_at(&s_screens[s_cur_screen_idx], s_cur_item);
        return;
    }
    if (ev == NAV_PREV || ev == NAV_NEXT) {
        p->cur = (ev == NAV_NEXT) ? (p->cur + 1) % p->count : (p->cur + p->count - 1) % p->count;
        apply_subpanel_focus(p);
    }
}

// ===================== Vong lap chinh =====================
static void handle_nav_msg(nav_msg_t msg)
{
    if (!lvgl_port_lock(1000)) return;

    if (s_text_target != NULL) {
        if (msg.type == NAV_CHAR)        text_entry_key(msg.ch);
        else if (msg.type == NAV_BACK)   text_entry_del();
        else if (msg.type == NAV_SELECT) text_entry_finish();
        lvgl_port_unlock();
        return;
    }

    if (s_active_subpanel >= 0) handle_subpanel_nav(msg.type);
    else handle_screen_nav(msg.type);

    lvgl_port_unlock();
}

void ui_nav_task(void *arg)
{
    QueueHandle_t nav_queue = (QueueHandle_t)arg;
    nav_msg_t msg;
    while (true) {
        if (xQueueReceive(nav_queue, &msg, portMAX_DELAY)) handle_nav_msg(msg);
    }
}