// test_a_uart.c - Test BO XU LY LENH UART cua NODE A (comm_uart.c THAT cua nhanh nodeA).
// face_engine / app_state / cam_driver / esp_wifi duoc THAY BANG HAM GIA ghi lai loi goi,
// de kiem tra: lenh nao -> goi ham nao, voi tham so nao, tra ACK/NACK gi.
#include "mini_test.h"
#include "comm_uart.h"
#include "uart_protocol.h"
#include "app_state.h"
#include "face_engine.h"
#include "cam_driver.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ===================== HAM GIA (mock) ======================================================
static struct {
    int switch_model, set_threshold, add_start, add_capture, add_cancel, del_user, clear_all, list_users;
    recog_model_t model; float threshold; uint16_t add_id; char add_name[FACE_ENGINE_NAME_LEN];
    uint16_t del_id;
    bool capture_ok, delete_ok, clear_ok;
    user_info_t users[20]; int nusers;
    // app_state / cam / wifi
    int cam_param_calls, sensor_calls; int8_t br, ct; bool flip, mirror;
    int8_t sensor_br, sensor_ct; bool sensor_flip, sensor_mirror;
    bool show_fps, streaming, liveness; int stream_calls, fps_calls, liveness_calls;
    enroll_state_t enroll;
    int wifi_calls;
} M;

static void mock_reset(void)
{
    memset(&M, 0, sizeof(M));
    M.capture_ok = M.delete_ok = M.clear_ok = true;
    stub_wifi_channel_rc = ESP_OK; stub_wifi_last_channel = 0;
}
void face_engine_switch_model(recog_model_t m) { M.switch_model++; M.model = m; }
void face_engine_set_threshold(float t) { M.set_threshold++; M.threshold = t; }
void face_engine_add_user_start(uint16_t id, const char *name)
{ M.add_start++; M.add_id = id; memcpy(M.add_name, name, FACE_ENGINE_NAME_LEN); }
bool face_engine_add_user_capture(void) { M.add_capture++; return M.capture_ok; }
void face_engine_add_user_cancel(void) { M.add_cancel++; }
bool face_engine_delete_user(uint16_t id) { M.del_user++; M.del_id = id; return M.delete_ok; }
bool face_engine_clear_all_users(void) { M.clear_all++; return M.clear_ok; }
int face_engine_list_users(user_info_t *out, int max)
{ M.list_users++; int n = M.nusers < max ? M.nusers : max; memcpy(out, M.users, (size_t)n * sizeof(user_info_t)); return n; }

void app_state_set_cam_param(int8_t b, int8_t c, bool f, bool m)
{ M.cam_param_calls++; M.br = b; M.ct = c; M.flip = f; M.mirror = m; }
int8_t app_state_get_brightness(void) { return M.br; }
int8_t app_state_get_contrast(void) { return M.ct; }
bool app_state_get_flip(void) { return M.flip; }
bool app_state_get_mirror(void) { return M.mirror; }
void app_state_set_show_fps(bool e) { M.fps_calls++; M.show_fps = e; }
void app_state_set_streaming(bool e) { M.stream_calls++; M.streaming = e; }
void app_state_set_liveness_enabled(bool e) { M.liveness_calls++; M.liveness = e; }
enroll_state_t app_state_get_enroll_state(void) { return M.enroll; }
void cam_driver_apply_sensor_config(int8_t b, int8_t c, bool f, bool m)
{ M.sensor_calls++; M.sensor_br = b; M.sensor_ct = c; M.sensor_flip = f; M.sensor_mirror = m; }

static int total_calls(void)
{
    return M.switch_model + M.set_threshold + M.add_start + M.add_capture + M.add_cancel + M.del_user +
           M.clear_all + M.cam_param_calls + M.sensor_calls + M.stream_calls + M.fps_calls +
           M.liveness_calls + M.wifi_calls;
}

// ===================== Khung + phan hoi ====================================================
static uint8_t ref_xor(uint8_t cmd, uint8_t len, const uint8_t *p)
{ uint8_t c = cmd ^ len; for (int i = 0; i < len; i++) c ^= p[i]; return c; }
static size_t make_frame(uint8_t *out, uint8_t cmd, const void *pl, uint8_t len)
{
    size_t k = 0; out[k++] = 0xAA; out[k++] = cmd; out[k++] = len;
    if (len) { memcpy(out + k, pl, len); k += len; }
    out[k++] = ref_xor(cmd, len, (const uint8_t *)pl); out[k++] = 0x55; return k;
}
typedef struct { uint8_t cmd, len; uint8_t p[64]; } rsp_t;
static rsp_t rsp[64]; static int nrsp; static bool tx_valid;
static void parse_tx(void)                        // tach cac khung Node A DA GUI, kiem tra dinh dang tung khung
{
    nrsp = 0; tx_valid = true; size_t i = 0;
    while (i < stub_tx_len) {
        if (stub_tx[i] != 0xAA || i + 5 > stub_tx_len) { tx_valid = false; return; }
        uint8_t cmd = stub_tx[i + 1], len = stub_tx[i + 2];
        if (len > 64 || i + 5 + len > stub_tx_len) { tx_valid = false; return; }
        if (stub_tx[i + 3 + len] != ref_xor(cmd, len, &stub_tx[i + 3]) || stub_tx[i + 4 + len] != 0x55) tx_valid = false;
        if (nrsp < 64) { rsp[nrsp].cmd = cmd; rsp[nrsp].len = len; memcpy(rsp[nrsp].p, &stub_tx[i + 3], len); nrsp++; }
        i += 5 + len;
    }
}
static void feed(const uint8_t *b, size_t n)
{
    stub_tx_len = 0; stub_rx = b; stub_rx_len = n; stub_rx_pos = 0;
    if (setjmp(stub_rx_exit) == 0) stub_task_fn(NULL);
    parse_tx();
}
static void setup(void)
{
    static bool inited = false;
    if (!inited) {
        CHECK(comm_uart_init(UART_NUM_1, 17, 18, 115200) == ESP_OK);
        comm_uart_start();
        inited = true;
    }
    CHECK(stub_task_fn != NULL);
    mock_reset();
}
// Gui 1 lenh, doc phan hoi.
static void cmd(uint8_t c, const void *pl, uint8_t len)
{
    uint8_t f[80]; size_t n = make_frame(f, c, pl, len); feed(f, n);
}
static bool is_ack(void)  { return nrsp == 1 && rsp[0].cmd == RSP_ACK && rsp[0].len == 0; }
static bool is_nack(void) { return nrsp == 1 && rsp[0].cmd == RSP_NACK && rsp[0].len == 0; }

// ===================== Test: parser =========================================================
static void t_valid_command_executes_and_acks(void)
{
    setup();
    payload_set_model_t p = {1}; cmd(CMD_SET_MODEL, &p, 1);
    CHECK(M.switch_model == 1 && M.model == RECOG_MODEL_MBF_S8_V1);
    CHECK(is_ack() && tx_valid);
}
static void t_corrupted_frames_execute_nothing(void)
{
    setup();
    uint8_t pl[1] = {0}, f[16];
    size_t n = make_frame(f, CMD_CLEAR_ALL_USERS, NULL, 0);
    f[n - 2] ^= 0x01; feed(f, n);                                    // sai checksum
    CHECK(total_calls() == 0 && nrsp == 0);
    n = make_frame(f, CMD_CLEAR_ALL_USERS, NULL, 0);
    f[n - 1] = 0x00; feed(f, n);                                     // sai byte END
    CHECK(total_calls() == 0 && nrsp == 0);
    uint8_t g[6] = {0xAA, CMD_CLEAR_ALL_USERS, 65, 0, 0, 0x55}; feed(g, 6);   // LEN > 64
    CHECK(total_calls() == 0);
    (void)pl;
}
static void t_garbage_never_triggers_destructive_commands(void)
{
    setup();
    static uint8_t junk[300000]; srand(31337);
    for (size_t i = 0; i < sizeof(junk); i++) junk[i] = (rand() % 5 == 0) ? 0xAA : (uint8_t)rand();
    feed(junk, sizeof(junk));
    INFO("300000 byte rac vao Node A: %d lenh bi kich hoat (xoa/doi cau hinh)", total_calls());
    CHECK(M.clear_all == 0 && M.del_user == 0);                      // KHONG BAO GIO xoa nguoi dung vi nhieu
}
static void t_single_bit_flip_never_executes_other_command(void)
{
    setup();
    srand(77); int tested = 0, leaked = 0;
    for (int iter = 0; iter < 200; iter++) {
        uint8_t pl[8], f[32]; uint8_t len = (uint8_t)(rand() % 9);
        for (int j = 0; j < len; j++) pl[j] = (uint8_t)rand();
        uint8_t c = (uint8_t)(1 + rand() % 0x11);
        size_t n = make_frame(f, c, pl, len);
        for (int bit = 0; bit < 8; bit++) for (size_t pos = 1; pos < n - 2; pos++) {
            if (pos == 2) continue;
            uint8_t g[32]; memcpy(g, f, n); g[pos] ^= (uint8_t)(1u << bit);
            mock_reset(); feed(g, n); tested++;
            if (total_calls() != 0 || nrsp != 0) leaked++;
        }
    }
    INFO("lat %d bit don le trong CMD/PAYLOAD: %d lan van chay lenh (dieu nay nghia la 1 bit loi co the doi lenh)", tested, leaked);
    CHECK(leaked == 0);
}

// ===================== Test: tung lenh ======================================================
static void t_set_model_and_threshold(void)
{
    setup();
    payload_set_model_t m = {0}; cmd(CMD_SET_MODEL, &m, 1);
    CHECK(is_ack() && M.model == RECOG_MODEL_MFN_S8_V1);
    cmd(CMD_SET_MODEL, NULL, 0); CHECK(is_nack() && M.switch_model == 1);   // thieu payload -> NACK, khong goi them
    payload_set_threshold_t t = {0.72f}; cmd(CMD_SET_THRESHOLD, &t, 4);
    CHECK(is_ack() && M.set_threshold == 1 && M.threshold == 0.72f);
    uint8_t short3[3] = {1, 2, 3}; cmd(CMD_SET_THRESHOLD, short3, 3);
    CHECK(is_nack() && M.set_threshold == 1);                                // 3 byte < sizeof(float)
}
static void t_camera_params_keep_other_values(void)
{
    setup();
    payload_set_int_param_t b = {-2}; cmd(CMD_SET_BRIGHTNESS, &b, 1);
    CHECK(is_ack() && M.br == -2 && M.sensor_calls == 1 && M.sensor_br == -2);
    payload_set_int_param_t c = {1}; cmd(CMD_SET_CONTRAST, &c, 1);
    CHECK(is_ack() && M.br == -2 && M.ct == 1);                              // do sang cu duoc GIU
    payload_bool_t f = {1}; cmd(CMD_SET_FLIP, &f, 1);
    CHECK(is_ack() && M.flip && M.br == -2 && M.ct == 1);
    payload_bool_t mi = {1}; cmd(CMD_SET_MIRROR, &mi, 1);
    CHECK(is_ack() && M.mirror && M.flip && M.sensor_mirror && M.sensor_flip && M.sensor_br == -2 && M.sensor_ct == 1);
    payload_bool_t two = {2}; cmd(CMD_SET_FLIP, &two, 1);
    CHECK(is_ack() && M.flip == true);                                       // bat ky gia tri != 0 la true
    payload_bool_t zero = {0}; cmd(CMD_SET_FLIP, &zero, 1);
    CHECK(is_ack() && M.flip == false);
    int before = M.sensor_calls;
    cmd(CMD_SET_BRIGHTNESS, NULL, 0); CHECK(is_nack() && M.sensor_calls == before);
    cmd(CMD_SET_CONTRAST, NULL, 0);   CHECK(is_nack() && M.sensor_calls == before);
    cmd(CMD_SET_FLIP, NULL, 0);       CHECK(is_nack() && M.sensor_calls == before);
    cmd(CMD_SET_MIRROR, NULL, 0);     CHECK(is_nack() && M.sensor_calls == before);
}
static void t_show_fps_and_stream_and_liveness(void)
{
    setup();
    payload_bool_t on = {1}, off = {0};
    cmd(CMD_SHOW_FPS, &on, 1);  CHECK(is_ack() && M.show_fps);
    cmd(CMD_SHOW_FPS, &off, 1); CHECK(is_ack() && !M.show_fps);
    cmd(CMD_SHOW_FPS, NULL, 0); CHECK(is_nack());
    cmd(CMD_START_STREAM, NULL, 0); CHECK(is_ack() && M.streaming);
    cmd(CMD_STOP_STREAM, NULL, 0);  CHECK(is_ack() && !M.streaming);
    cmd(CMD_ENABLE_LIVENESS_CHECK, &on, 1);  CHECK(is_ack() && M.liveness);
    cmd(CMD_ENABLE_LIVENESS_CHECK, &off, 1); CHECK(is_ack() && !M.liveness);
    cmd(CMD_ENABLE_LIVENESS_CHECK, NULL, 0); CHECK(is_nack() && M.liveness_calls == 2);
}
static void t_enrol_flow(void)
{
    setup();
    payload_add_user_start_t s; memset(&s, 0, sizeof(s)); s.user_id = 0x1234; strcpy(s.name, "Nguyen Duy Dong");
    cmd(CMD_ADD_USER_START, &s, sizeof(s));
    CHECK(is_ack() && M.add_start == 1 && M.add_id == 0x1234 && strcmp(M.add_name, "Nguyen Duy Dong") == 0);
    cmd(CMD_ADD_USER_START, &s, 10); CHECK(is_nack() && M.add_start == 1);   // payload qua ngan

    M.enroll.in_progress = true; M.enroll.user_id = 0x1234; strcpy(M.enroll.user_name, "Nguyen Duy Dong");
    M.capture_ok = true; cmd(CMD_ADD_USER_CAPTURE, NULL, 0);
    CHECK(nrsp == 1 && rsp[0].cmd == RSP_ADD_USER_RESULT && rsp[0].len == sizeof(payload_add_user_result_t));
    payload_add_user_result_t r; memcpy(&r, rsp[0].p, sizeof(r));
    CHECK(r.success == 1 && r.user_id == 0x1234 && strcmp(r.name, "Nguyen Duy Dong") == 0);
    M.capture_ok = false; cmd(CMD_ADD_USER_CAPTURE, NULL, 0);
    memcpy(&r, rsp[0].p, sizeof(r)); CHECK(r.success == 0 && r.user_id == 0x1234);   // that bai van tra ve ID + ten
    cmd(CMD_ADD_USER_CANCEL, NULL, 0); CHECK(is_ack() && M.add_cancel == 1);
}
static void t_user_management(void)
{
    setup();
    payload_sub_user_t d = {0x0102};
    M.delete_ok = true; cmd(CMD_SUB_USER, &d, 2);
    CHECK(nrsp == 1 && rsp[0].cmd == RSP_SUB_USER_RESULT && M.del_id == 0x0102);
    payload_sub_user_result_t r; memcpy(&r, rsp[0].p, sizeof(r)); CHECK(r.success == 1 && r.user_id == 0x0102);
    M.delete_ok = false; cmd(CMD_SUB_USER, &d, 2);
    memcpy(&r, rsp[0].p, sizeof(r)); CHECK(r.success == 0 && r.user_id == 0x0102);
    int before = M.del_user; cmd(CMD_SUB_USER, NULL, 0); CHECK(is_nack() && M.del_user == before);
    M.clear_ok = true;  cmd(CMD_CLEAR_ALL_USERS, NULL, 0); CHECK(is_ack() && M.clear_all == 1);
    M.clear_ok = false; cmd(CMD_CLEAR_ALL_USERS, NULL, 0); CHECK(is_nack() && M.clear_all == 2);
}
static void t_list_users(void)
{
    setup();
    cmd(CMD_LIST_USER_REQ, NULL, 0);
    CHECK(nrsp == 1 && rsp[0].cmd == RSP_USER_LIST_END && rsp[0].len == 0);          // rong -> LIST_END
    for (int n = 1; n <= 3; n++) {
        mock_reset(); M.nusers = n;
        for (int i = 0; i < n; i++) { M.users[i].user_id = (uint16_t)(10 + i); snprintf(M.users[i].name, 24, "user%d", i); }
        cmd(CMD_LIST_USER_REQ, NULL, 0);
        CHECK_MSG(nrsp == n, "%d user -> %d khung", n, nrsp);
        for (int i = 0; i < nrsp && i < n; i++) {
            payload_user_list_item_t it; memcpy(&it, rsp[i].p, sizeof(it));
            CHECK(rsp[i].cmd == RSP_USER_LIST_ITEM && it.user_id == 10 + i);
            CHECK(it.is_last == (i == n - 1 ? 1 : 0));                                 // CHI khung cuoi co is_last
        }
    }
    mock_reset(); M.nusers = 20;                                                        // nhieu hon bo dem 16
    cmd(CMD_LIST_USER_REQ, NULL, 0);
    CHECK_MSG(nrsp == 16, "20 user nhung chi gui 16 khung, thuc te %d (gioi han 16 cua handler)", nrsp);
    INFO("gioi han: handler chi liet ke toi da 16 nguoi dung moi lan (user_info_t list[16])");
}
static void t_espnow_channel(void)
{
    setup();
    for (int ch = 1; ch <= 13; ch++) {
        payload_set_channel_t p = {(uint8_t)ch}; cmd(CMD_SET_ESPNOW_CHANNEL, &p, 1);
        CHECK(is_ack() && stub_wifi_last_channel == ch);
    }
    stub_wifi_last_channel = 99;
    uint8_t bad[] = {0, 14, 15, 100, 255};
    for (unsigned i = 0; i < sizeof(bad); i++) {
        payload_set_channel_t p = {bad[i]}; cmd(CMD_SET_ESPNOW_CHANNEL, &p, 1);
        CHECK_MSG(is_nack() && stub_wifi_last_channel == 99, "kenh %u phai bi NACK va khong goi esp_wifi_set_channel", bad[i]);
    }
    stub_wifi_channel_rc = ESP_FAIL; payload_set_channel_t p = {6}; cmd(CMD_SET_ESPNOW_CHANNEL, &p, 1);
    CHECK_MSG(is_nack(), "esp_wifi_set_channel loi -> NACK");
    cmd(CMD_SET_ESPNOW_CHANNEL, NULL, 0); CHECK(is_nack());
}
static void t_unknown_command_nacked(void)
{
    setup();
    uint8_t unk[] = {0x00, 0x12, 0x40, 0x7F, 0x7E, 0xFF};
    for (unsigned i = 0; i < sizeof(unk); i++) { cmd(unk[i], NULL, 0); CHECK_MSG(is_nack() && total_calls() == 0, "lenh 0x%02X phai bi NACK", unk[i]); }
}
static void t_all_commands_with_max_payload_no_overflow(void)
{
    setup();
    uint8_t big[64]; memset(big, 0xFF, sizeof(big));
    for (int c = 0; c <= 0x20; c++) { mock_reset(); cmd((uint8_t)c, big, 64); CHECK(tx_valid); }   // moi lenh voi payload 64 byte
}

// ===================== Gioi han (REPRO) =====================================================
static void t_limit_out_of_range_values_forwarded(void)
{
    setup();
    payload_set_model_t m = {99}; cmd(CMD_SET_MODEL, &m, 1);
    bool model_fwd = M.switch_model == 1 && (int)M.model == 99;
    float bads[] = {NAN, -5.0f, 7.5f, INFINITY}; int thr_fwd = 0;
    for (unsigned i = 0; i < 4; i++) { mock_reset(); payload_set_threshold_t t = {bads[i]}; cmd(CMD_SET_THRESHOLD, &t, 4); if (M.set_threshold == 1) thr_fwd++; }
    mock_reset(); payload_set_int_param_t b = {120}; cmd(CMD_SET_BRIGHTNESS, &b, 1);
    bool br_fwd = M.sensor_calls == 1 && M.sensor_br == 120;
    INFO("handler KHONG kiem tra mien gia tri: model_id=99 %s, nguong NaN/-5/7.5/inf chuyen xuong %d/4, do sang 120 %s",
         model_fwd ? "duoc chuyen xuong" : "bi chan", thr_fwd, br_fwd ? "duoc chuyen xuong" : "bi chan");
    INFO("(chua biet face_engine/cam_driver co tu chan o tang duoi khong - can kiem tra hoac them kiem tra o handler)");
    if (model_fwd || thr_fwd > 0 || br_fwd) mt_bug_seen = 1;
}

// ===================== Node B -> Node A: moi lenh B gui ra, A phai hieu =====================
static void t_every_command_from_node_b_is_understood(void)
{
    setup();
    FILE *f = fopen("b_to_a.bin", "rb");
    CHECK_MSG(f != NULL, "chua co b_to_a.bin (phai chay test_uart cua Node B truoc)");
    if (!f) return;
    static uint8_t buf[2048]; size_t n = fread(buf, 1, sizeof(buf), f); fclose(f);
    CHECK(n > 0);
    M.capture_ok = true; M.delete_ok = true; M.clear_ok = true;
    M.enroll.in_progress = true; M.enroll.user_id = 42; strcpy(M.enroll.user_name, "Dong");
    feed(buf, n);
    CHECK_MSG(tx_valid, "phan hoi cua Node A sai dinh dang");
    uint8_t expect[] = {RSP_ACK, RSP_ACK, RSP_ACK, RSP_ACK, RSP_ACK, RSP_ACK, RSP_ACK,       // model,thr,bright,contrast,flip,mirror,fps
                        RSP_ACK, RSP_ADD_USER_RESULT, RSP_ACK, RSP_SUB_USER_RESULT, RSP_ACK,  // add_start, capture, cancel, sub, clear
                        RSP_USER_LIST_END, RSP_ACK, RSP_ACK, RSP_ACK, RSP_ACK};              // list, start, stop, liveness, channel
    CHECK_MSG(nrsp == (int)sizeof(expect), "Node B gui 17 lenh, Node A tra %d phan hoi", nrsp);
    for (int i = 0; i < nrsp && i < (int)sizeof(expect); i++)
        CHECK_MSG(rsp[i].cmd == expect[i], "lenh thu %d: phan hoi 0x%02X, ky vong 0x%02X", i + 1, rsp[i].cmd, expect[i]);
    CHECK(M.model == RECOG_MODEL_MBF_S8_V1 && M.threshold == 0.7f);
    CHECK(M.sensor_br == -2 && M.sensor_ct == 1 && M.flip == true && M.mirror == false);
    CHECK(M.add_id == 42 && strcmp(M.add_name, "Dong") == 0 && M.del_id == 42);
    CHECK(M.liveness == true && stub_wifi_last_channel == 6);
}

int main(void)
{
    SUITE("node_a_uart");
    RUN(t_valid_command_executes_and_acks,               "lenh hop le -> thuc thi + tra ACK dung dinh dang");
    RUN(t_corrupted_frames_execute_nothing,              "sai checksum / sai END / LEN>64 -> khong thuc thi lenh nao");
    RUN(t_garbage_never_triggers_destructive_commands,   "300000 byte rac KHONG bao gio kich hoat xoa nguoi dung");
    RUN(t_single_bit_flip_never_executes_other_command,  "lat 1 bit trong CMD/PAYLOAD khong the doi thanh lenh khac");
    RUN(t_set_model_and_threshold,                       "SET_MODEL / SET_THRESHOLD: dung tham so, thieu payload -> NACK");
    RUN(t_camera_params_keep_other_values,               "SET_BRIGHTNESS/CONTRAST/FLIP/MIRROR: giu nguyen thong so khac, ap len cam");
    RUN(t_show_fps_and_stream_and_liveness,              "SHOW_FPS / START,STOP_STREAM / LIVENESS: dung trang thai, thieu payload -> NACK");
    RUN(t_enrol_flow,                                    "them nguoi dung: START -> CAPTURE (ket qua kem ID+ten) -> CANCEL");
    RUN(t_user_management,                               "SUB_USER tra ket qua kem ID, CLEAR_ALL ACK/NACK theo ket qua");
    RUN(t_list_users,                                    "LIST: rong -> LIST_END; 1..3 user -> is_last chi o khung cuoi; toi da 16");
    RUN(t_espnow_channel,                                "doi kenh ESP-NOW: 1..13 ACK; 0/14/255 NACK; loi wifi -> NACK");
    RUN(t_unknown_command_nacked,                        "lenh la -> NACK, khong goi ham nao");
    RUN(t_all_commands_with_max_payload_no_overflow,     "moi ma lenh 0..0x20 voi payload 64 byte: khong tran bo nho, phan hoi dung dinh dang");
    RUN_REPRO(t_limit_out_of_range_values_forwarded,     "HAN CHE: model_id=99 / nguong NaN,-5 / do sang 120 duoc chuyen nguyen xuong ham ben duoi");
    SUITE("node_b_to_a_uart");
    RUN(t_every_command_from_node_b_is_understood,       "17 lenh do Node B THAT sinh ra -> Node A hieu dung, goi dung ham, phan hoi dung");
    return mt_exit_code();
}
