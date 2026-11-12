// test_uart.c - Test bo parse khung UART CUA NODE B (comm_uart.c THAT, bien dich tren PC).
// Khung: 0xAA | CMD | LEN | PAYLOAD | CHECKSUM(XOR cmd,len,payload) | 0x55
#include "mini_test.h"
#include "comm_uart.h"
#include "espnow_frame.h"
#include <stdlib.h>
#include <string.h>

// ---- nhan khung qua listener ------------------------------------------------------
typedef struct { uint8_t cmd, len; uint8_t payload[UART_MAX_PAYLOAD]; } rx_frame_t;
#define MAXGOT 4096
static rx_frame_t got[MAXGOT];
static int ngot;

static void on_rsp(uint8_t cmd, const uint8_t *p, uint8_t len, void *ctx)
{
    (void)ctx;
    if (ngot < MAXGOT) {
        got[ngot].cmd = cmd;
        got[ngot].len = len;
        if (len) memcpy(got[ngot].payload, p, len);
    }
    ngot++;
}

// Nap `n` byte vao "cong UART", chay task parse that cho den het du lieu.
static void feed(const uint8_t *b, size_t n)
{
    ngot = 0;
    stub_rx = b; stub_rx_len = n; stub_rx_pos = 0;
    if (setjmp(stub_rx_exit) == 0) stub_task_fn(NULL);
}

// Tinh checksum DOC LAP (khong goi uart_calc_checksum) de tranh test "tu khen minh".
static uint8_t ref_xor(uint8_t cmd, uint8_t len, const uint8_t *p)
{
    uint8_t c = cmd ^ len;
    for (int i = 0; i < len; i++) c ^= p[i];
    return c;
}
static size_t make_frame(uint8_t *out, uint8_t cmd, const uint8_t *p, uint8_t len)
{
    size_t k = 0;
    out[k++] = 0xAA; out[k++] = cmd; out[k++] = len;
    for (int i = 0; i < len; i++) out[k++] = p[i];
    out[k++] = ref_xor(cmd, len, p);
    out[k++] = 0x55;
    return k;
}

static void setup(void)
{
    stub_log_warn = 0;
    CHECK(comm_uart_init(UART_NUM_1, 17, 18, 115200) == ESP_OK);
    comm_uart_start();                                    // bat lay ham uart_rx_task
    CHECK(stub_task_fn != NULL);
    CHECK(comm_uart_register_listener(on_rsp, NULL) >= 0);
    stub_tx_len = 0;
}

// ---- Test --------------------------------------------------------------------------
static void t_valid_frame_with_payload(void)
{
    setup();
    uint8_t pl[] = {1, 2, 3, 4, 5}, f[80];
    size_t n = make_frame(f, RSP_FACE_MATCHED, pl, sizeof(pl));
    feed(f, n);
    CHECK(ngot == 1);
    CHECK(got[0].cmd == RSP_FACE_MATCHED && got[0].len == 5);
    CHECK(memcmp(got[0].payload, pl, 5) == 0);
}

static void t_valid_frame_zero_length(void)
{
    setup();
    uint8_t f[8];
    size_t n = make_frame(f, RSP_USER_LIST_END, NULL, 0);
    feed(f, n);
    CHECK(ngot == 1 && got[0].cmd == RSP_USER_LIST_END && got[0].len == 0);
}

static void t_max_payload_64(void)
{
    setup();
    uint8_t pl[64], f[80];
    for (int i = 0; i < 64; i++) pl[i] = (uint8_t)(i * 7 + 1);
    size_t n = make_frame(f, RSP_USER_LIST_ITEM, pl, 64);
    feed(f, n);
    CHECK(ngot == 1 && got[0].len == 64 && memcmp(got[0].payload, pl, 64) == 0);
}

static void t_payload_containing_start_and_end_bytes(void)
{
    setup();
    uint8_t pl[] = {0xAA, 0x55, 0xAA, 0x55, 0x00, 0xAA}, f[32];
    size_t n = make_frame(f, RSP_FACE_UNKNOWN, pl, sizeof(pl));
    feed(f, n);
    CHECK_MSG(ngot == 1, "payload chua 0xAA/0x55 phai van parse dung (parse theo LEN), nhan %d khung", ngot);
    if (ngot == 1) CHECK(memcmp(got[0].payload, pl, sizeof(pl)) == 0);
}

static void t_bad_checksum_is_dropped(void)
{
    setup();
    uint8_t pl[] = {9, 8, 7}, f[32];
    size_t n = make_frame(f, RSP_ACK, pl, 3);
    f[n - 2] ^= 0x01;                                     // hong checksum
    feed(f, n);
    CHECK(ngot == 0);
    CHECK(stub_log_warn >= 1);                            // phai co canh bao
}

static void t_missing_end_byte_is_dropped(void)
{
    setup();
    uint8_t pl[] = {1}, f[16];
    size_t n = make_frame(f, RSP_ACK, pl, 1);
    f[n - 1] = 0x00;                                      // END sai
    feed(f, n);
    CHECK(ngot == 0);
}

static void t_oversize_len_dropped_then_recovers(void)
{
    setup();
    uint8_t bad[] = {0xAA, RSP_ACK, 65}, good[16], buf[64];
    size_t n = 0;
    memcpy(buf, bad, 3); n = 3;
    n += make_frame(good, RSP_NACK, NULL, 0);
    memcpy(buf + 3, good, n - 3);
    feed(buf, n);
    CHECK_MSG(ngot == 1 && got[0].cmd == RSP_NACK, "sau LEN>64 phai tu hoi phuc, nhan %d khung", ngot);
}

static void t_garbage_before_frame_is_ignored(void)
{
    setup();
    uint8_t buf[64] = {0x00, 0x13, 0x37, 0xFF, 0x55, 0x55, 0x01};
    size_t n = 7;
    n += make_frame(buf + n, RSP_ACK, NULL, 0);
    feed(buf, n);
    CHECK(ngot == 1 && got[0].cmd == RSP_ACK);
}

static void t_back_to_back_frames_1000_random(void)
{
    setup();
    static uint8_t stream[1000 * 70];
    static uint8_t cmds[1000], lens[1000], pls[1000][64];
    size_t n = 0;
    srand(7);
    for (int i = 0; i < 1000; i++) {
        cmds[i] = (uint8_t)(0x80 + rand() % 16);
        lens[i] = (uint8_t)(rand() % 65);
        for (int j = 0; j < lens[i]; j++) pls[i][j] = (uint8_t)rand();   // gom ca 0xAA/0x55 ngau nhien
        n += make_frame(stream + n, cmds[i], pls[i], lens[i]);
    }
    feed(stream, n);
    CHECK_MSG(ngot == 1000, "gui 1000 khung lien tiep, nhan %d", ngot);
    for (int i = 0; i < ngot && i < 1000; i++) {
        if (got[i].cmd != cmds[i] || got[i].len != lens[i] || memcmp(got[i].payload, pls[i], lens[i]) != 0) {
            CHECK_MSG(0, "khung #%d sai noi dung/thu tu", i);
            break;
        }
    }
}

static void t_single_bit_flip_in_cmd_or_payload_always_detected(void)
{
    setup();
    srand(99);
    int tested = 0, leaked = 0;
    for (int iter = 0; iter < 300; iter++) {
        uint8_t pl[64], f[80];
        uint8_t len = (uint8_t)(1 + rand() % 64);
        for (int j = 0; j < len; j++) pl[j] = (uint8_t)rand();
        size_t n = make_frame(f, (uint8_t)(0x80 + rand() % 16), pl, len);
        // Byte 1 = CMD, byte 3..3+len-1 = PAYLOAD (khong dong vao LEN vi se lech khung)
        for (int bit = 0; bit < 8; bit++) {
            for (size_t pos = 1; pos < n - 2; pos++) {
                if (pos == 2) continue;
                uint8_t g[80]; memcpy(g, f, n);
                g[pos] ^= (uint8_t)(1u << bit);
                feed(g, n);
                tested++;
                if (ngot != 0) leaked++;
            }
        }
    }
    INFO("da lat %d bit don le, %d lan loi lot qua checksum", tested, leaked);
    CHECK(leaked == 0);
}

static void t_truncated_frame_recovers_on_later_frame(void)
{
    setup();
    uint8_t pl[] = {1, 2, 3}, a[32], b[32], c[32], s[128];
    size_t na = make_frame(a, RSP_ACK, pl, 3) - 2;        // khung A bi CAT mat checksum + END
    size_t nb = make_frame(b, RSP_NACK, NULL, 0);          // khung B day du, ngay sau A
    size_t nc = make_frame(c, RSP_FPS_REPORT, pl, 3);      // khung C day du, sau B
    size_t n = 0;
    memcpy(s + n, a, na); n += na;
    memcpy(s + n, b, nb); n += nb;
    memcpy(s + n, c, nc); n += nc;
    feed(s, n);
    int has_b = 0, has_c = 0;
    for (int i = 0; i < ngot && i < MAXGOT; i++) {
        if (got[i].cmd == RSP_NACK) has_b = 1;
        if (got[i].cmd == RSP_FPS_REPORT) has_c = 1;
    }
    INFO("khung A bi cut: khung B ngay sau %s, khung C sau do %s",
         has_b ? "VAN nhan duoc" : "BI MAT (han che da biet: byte 0xAA cua B bi nuot lam checksum cua A)",
         has_c ? "nhan duoc" : "bi mat");
    CHECK_MSG(has_c, "bo parse phai hoi phuc de nhan khung C");
}

static void t_fuzz_random_bytes_no_crash(void)
{
    setup();
    static uint8_t junk[200000];
    srand(2026);
    for (size_t i = 0; i < sizeof(junk); i++) {
        junk[i] = (rand() % 6 == 0) ? 0xAA : (uint8_t)rand();   // nhieu 0xAA de kich hoat parser
    }
    feed(junk, sizeof(junk));
    int ok = 1;
    for (int i = 0; i < ngot && i < MAXGOT; i++) if (got[i].len > UART_MAX_PAYLOAD) ok = 0;
    INFO("200000 byte rac -> %d khung duoc chap nhan (khong crash, sanitizer sach)", ngot);
    CHECK(ok);
}

// ---- Chieu GUI: kiem tra tung byte ----------------------------------------------------
static void t_send_threshold_exact_bytes(void)
{
    setup();
    CHECK(comm_uart_cmd_set_threshold(0.5f) == ESP_OK);
    uint8_t exp[16]; float t = 0.5f;
    size_t n = make_frame(exp, CMD_SET_THRESHOLD, (const uint8_t *)&t, 4);
    CHECK(stub_tx_len == n && memcmp(stub_tx, exp, n) == 0);
    CHECK(stub_tx[0] == 0xAA && stub_tx[1] == 0x02 && stub_tx[2] == 4 && stub_tx[n - 1] == 0x55);
}

static void t_send_zero_length_cmds(void)
{
    setup();
    CHECK(comm_uart_cmd_list_users_request() == ESP_OK);
    const uint8_t exp[] = {0xAA, CMD_LIST_USER_REQ, 0x00, (uint8_t)(CMD_LIST_USER_REQ ^ 0), 0x55};
    CHECK(stub_tx_len == sizeof(exp) && memcmp(stub_tx, exp, sizeof(exp)) == 0);
}

static void t_send_add_user_name_truncated_to_23_chars(void)
{
    setup();
    const char *longname = "NguyenDuyDongNguyenDuyDongNguyenDuyDong";   // > 23 ky tu
    CHECK(comm_uart_cmd_add_user_start(0x1234, longname) == ESP_OK);
    CHECK(stub_tx[2] == sizeof(payload_add_user_start_t));
    CHECK(stub_tx[3] == 0x34 && stub_tx[4] == 0x12);                     // little-endian user_id
    CHECK(strlen((const char *)&stub_tx[5]) == 23);                      // cat con 23 + NUL cuoi
    CHECK(stub_tx[5 + 23] == 0);
}

static void t_send_rejects_oversize_payload(void)
{
    setup();
    uint8_t big[65] = {0};
    CHECK(comm_uart_send_cmd(CMD_SET_MODEL, big, 65) == ESP_ERR_INVALID_ARG);
    CHECK(stub_tx_len == 0);                                             // khong gui gi ra day
}

static void t_roundtrip_sent_frame_is_parsed_back(void)
{
    setup();
    CHECK(comm_uart_cmd_sub_user(777) == ESP_OK);
    uint8_t copy[64]; size_t n = stub_tx_len; memcpy(copy, stub_tx, n);
    feed(copy, n);
    CHECK(ngot == 1 && got[0].cmd == CMD_SUB_USER && got[0].len == 2);
    CHECK(got[0].payload[0] == (777 & 0xFF) && got[0].payload[1] == (777 >> 8));
}

static void t_payload_struct_sizes_fit_frame(void)
{
    CHECK(sizeof(payload_add_user_start_t) == 26);
    CHECK(sizeof(payload_add_user_result_t) == 27);
    CHECK(sizeof(payload_user_list_item_t) == 27);
    CHECK(sizeof(payload_face_matched_t) == 10);
    CHECK(sizeof(payload_face_event_id_t) == 4);
    CHECK(sizeof(payload_add_user_start_t) <= UART_MAX_PAYLOAD);
    CHECK(sizeof(payload_user_list_item_t) <= UART_MAX_PAYLOAD);
}

static void t_espnow_fragment_fits_250_bytes(void)
{
    // ESP-NOW gioi han 250 byte/goi. Neu ai do tang ESPNOW_FRAGMENT_MAX_PAYLOAD qua muc nay
    // thi anh se mat tren thuc te ma khong co thong bao loi nao.
    INFO("sizeof(espnow_frame_fragment_t) = %u byte (gioi han 250)", (unsigned)sizeof(espnow_frame_fragment_t));
    CHECK(sizeof(espnow_frame_fragment_t) <= 250);
    CHECK(sizeof(espnow_frame_fragment_t) == 4 + 2 + 2 + 4 + 2 + 2 + 2 + ESPNOW_FRAGMENT_MAX_PAYLOAD);  // packed, khong co padding
}

static void t_export_all_commands_for_node_a(void)
{
    // Ghi MOI lenh ma Node B co the gui, theo thu tu co dinh, ra b_to_a.bin cho test_a_uart doc.
    setup();
    FILE *f = fopen("b_to_a.bin", "wb");
    CHECK(f != NULL);
#define DUMP(call) do { stub_tx_len = 0; CHECK((call) == ESP_OK); if (f) fwrite(stub_tx, 1, stub_tx_len, f); } while (0)
    DUMP(comm_uart_cmd_set_model(1));
    DUMP(comm_uart_cmd_set_threshold(0.7f));
    DUMP(comm_uart_cmd_set_brightness(-2));
    DUMP(comm_uart_cmd_set_contrast(1));
    DUMP(comm_uart_cmd_set_flip(true));
    DUMP(comm_uart_cmd_set_mirror(false));
    DUMP(comm_uart_cmd_show_fps(true));
    DUMP(comm_uart_cmd_add_user_start(42, "Dong"));
    DUMP(comm_uart_cmd_add_user_capture());
    DUMP(comm_uart_cmd_add_user_cancel());
    DUMP(comm_uart_cmd_sub_user(42));
    DUMP(comm_uart_cmd_clear_all_users());
    DUMP(comm_uart_cmd_list_users_request());
    DUMP(comm_uart_cmd_start_stream());
    DUMP(comm_uart_cmd_stop_stream());
    DUMP(comm_uart_cmd_enable_liveness_check(true));
    DUMP(comm_uart_cmd_set_espnow_channel(6));
#undef DUMP
    if (f) fclose(f);
}

// ---- Listener --------------------------------------------------------------------------
static void t_listener_limit_and_reuse(void)
{
    CHECK(comm_uart_init(UART_NUM_1, 17, 18, 115200) == ESP_OK);          // reset danh sach
    int h[8];
    for (int i = 0; i < 6; i++) { h[i] = comm_uart_register_listener(on_rsp, NULL); CHECK(h[i] >= 0); }
    CHECK(comm_uart_register_listener(on_rsp, NULL) < 0);                 // thu 7 phai bi tu choi
    CHECK(comm_uart_unregister_listener(h[2]) == ESP_OK);
    CHECK(comm_uart_register_listener(on_rsp, NULL) == h[2]);             // slot duoc tai su dung
    CHECK(comm_uart_unregister_listener(-1) == ESP_ERR_INVALID_ARG);
    CHECK(comm_uart_unregister_listener(6) == ESP_ERR_INVALID_ARG);
    CHECK(comm_uart_register_listener(NULL, NULL) < 0);
}

static void t_unregistered_listener_gets_nothing(void)
{
    setup();
    int h2 = comm_uart_register_listener(on_rsp, NULL);                   // 2 listener cung nhan
    uint8_t f[8]; size_t n = make_frame(f, RSP_ACK, NULL, 0);
    feed(f, n);
    CHECK(ngot == 2);
    comm_uart_unregister_listener(h2);
    feed(f, n);
    CHECK(ngot == 1);
}

int main(void)
{
    SUITE("uart_node_b");
    RUN(t_valid_frame_with_payload,                  "khung hop le co payload -> chuyen den listener dung noi dung");
    RUN(t_valid_frame_zero_length,                   "khung LEN=0 hop le");
    RUN(t_max_payload_64,                            "payload toi da 64 byte");
    RUN(t_payload_containing_start_and_end_bytes,    "payload chua 0xAA/0x55 van parse dung (khong do END trong luong)");
    RUN(t_bad_checksum_is_dropped,                   "sai checksum -> bo khung + co canh bao");
    RUN(t_missing_end_byte_is_dropped,               "sai byte END -> bo khung");
    RUN(t_oversize_len_dropped_then_recovers,        "LEN > 64 -> bo khung, khung sau van nhan duoc");
    RUN(t_garbage_before_frame_is_ignored,           "byte rac truoc khung bi bo qua");
    RUN(t_back_to_back_frames_1000_random,           "1000 khung lien tiep ngau nhien (gom 0xAA/0x55 trong payload)");
    RUN(t_single_bit_flip_in_cmd_or_payload_always_detected, "lat 1 bit bat ky trong CMD/PAYLOAD luon bi checksum phat hien");
    RUN(t_truncated_frame_recovers_on_later_frame,   "khung bi cut -> bo parse hoi phuc o khung sau");
    RUN(t_fuzz_random_bytes_no_crash,                "200000 byte rac khong lam crash/tran bo nho");
    RUN(t_send_threshold_exact_bytes,                "gui CMD_SET_THRESHOLD: dung tung byte (AA,02,04,float,chk,55)");
    RUN(t_send_zero_length_cmds,                     "gui lenh khong payload: dung dinh dang");
    RUN(t_send_add_user_name_truncated_to_23_chars,  "ten user dai bi cat con 23 ky tu + NUL");
    RUN(t_send_rejects_oversize_payload,             "payload > 64 bi tu choi, khong gui gi ra UART");
    RUN(t_roundtrip_sent_frame_is_parsed_back,       "khung Node B gui ra, parser doc lai dung (round-trip)");
    RUN(t_payload_struct_sizes_fit_frame,            "kich thuoc cac struct payload khop (packed) va <= 64");
    RUN(t_export_all_commands_for_node_a,            "xuat 17 lenh Node B co the gui (de Node A kiem tra hieu dung)");
    RUN(t_espnow_fragment_fits_250_bytes,            "goi ESP-NOW (espnow_frame_fragment_t) <= 250 byte, khong co padding");
    RUN(t_listener_limit_and_reuse,                  "toi da 6 listener, slot tai su dung, tham so sai bi tu choi");
    RUN(t_unregistered_listener_gets_nothing,        "listener da go dang ky khong nhan khung nua");
    return mt_exit_code();
}
