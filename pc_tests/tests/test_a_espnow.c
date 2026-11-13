// test_a_espnow.c - Test PHIA GUI anh cua NODE A (comm_espnow.c THAT cua nhanh nodeA).
// Anh JPEG duoc chia thanh manh 230 byte; moi manh gui qua esp_now_send (gia lap, ghi lai).
// Dong thoi GHI cac manh ra file a_frags.bin de test_b_espnow nap vao bo ghep cua Node B.
#include "mini_test.h"
#include "comm_espnow.h"
#include "face_engine.h"
#include "espnow_frame.h"
#include "xnode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static QueueHandle_t q;

static void setup(void)
{
    static bool inited = false;                    // tren chip chi init 1 lan luc khoi dong
    if (!inited) {
        const uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x22};
        CHECK(comm_espnow_init(peer) == ESP_OK);
        q = comm_espnow_get_event_queue();
        comm_espnow_start();
        inited = true;
    }
    CHECK(q != NULL);
    CHECK(stub_task_fn != NULL);
}

// Dua 1 su kien co anh vao hang doi, chay task gui cho den het, tra ve so goi da gui.
static uint8_t *last_buf;                         // buffer test da giao cho task (task phai giai phong)
static int send_event(uint32_t id, const uint8_t *jpeg, uint32_t len, uint16_t w, uint16_t h)
{
    face_event_t evt; memset(&evt, 0, sizeof(evt));
    evt.event_id = id; evt.width = w; evt.height = h;
    last_buf = NULL;
    if (jpeg) { evt.jpeg_data = malloc(len ? len : 1); memcpy(evt.jpeg_data, jpeg, len); last_buf = evt.jpeg_data; }   // task TU GIAI PHONG
    stub_free_forget();
    evt.jpeg_len = len;
    stub_sent_count = 0;
    CHECK(xQueueSend(q, &evt, 0) == pdTRUE);
    stub_idle_polls = 0;
    if (setjmp(stub_rx_exit) == 0) stub_task_fn(NULL);
    return stub_sent_count;
}

// Kiem tra tung manh cua 1 anh da gui + ghep lai phai ra dung anh goc.
static void verify_fragments(uint32_t id, const uint8_t *jpeg, uint32_t len, uint16_t w, uint16_t h)
{
    uint32_t total = (len + 229) / 230;
    CHECK_MSG((uint32_t)stub_sent_count == total, "anh %u byte: %d goi, ky vong %u", len, stub_sent_count, total);
    uint8_t *re = calloc(1, len ? len : 1); size_t off = 0;
    for (int i = 0; i < stub_sent_count && i < STUB_MAX_SENT; i++) {
        espnow_frame_fragment_t f;
        size_t hdr = offsetof(espnow_frame_fragment_t, payload);
        CHECK(stub_sent_len[i] >= hdr && stub_sent_len[i] <= 250);
        memset(&f, 0, sizeof(f)); memcpy(&f, stub_sent_buf[i], stub_sent_len[i]);
        CHECK(f.event_id == id && f.width == w && f.height == h && f.jpeg_len == len);
        CHECK(f.fragment_index == i && f.fragment_total == total);
        CHECK(f.payload_len <= ESPNOW_FRAGMENT_MAX_PAYLOAD);
        CHECK_MSG(stub_sent_len[i] == hdr + f.payload_len, "goi %d: do dai %zu != header+payload", i, stub_sent_len[i]);
        uint32_t expect = (i == (int)total - 1) ? len - 230u * (total - 1) : 230u;
        CHECK_MSG(f.payload_len == expect, "manh %d: payload_len=%u, ky vong %u", i, f.payload_len, expect);
        if (off + f.payload_len <= len) memcpy(re + off, f.payload, f.payload_len);
        off += f.payload_len;
        if (mt_fail) break;
    }
    CHECK(off == len && memcmp(re, jpeg, len) == 0);
    free(re);
}

static void t_fragment_sizes_edge_cases(void)
{
    setup();
    uint32_t lens[] = {1, 229, 230, 231, 460, 461, 4097};
    for (unsigned k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        uint8_t *j = malloc(lens[k]); xn_jpeg(500 + k, lens[k], j);
        send_event(500 + k, j, lens[k], 320, 240);
        verify_fragments(500 + k, j, lens[k], 320, 240);
        free(j);
        if (mt_fail) return;
    }
}

static void t_export_cases_for_node_b(void)
{
    setup();
    FILE *out = fopen(XN_FILE, "wb");
    CHECK(out != NULL);
    for (unsigned c = 0; c < XN_NCASES; c++) {
        uint8_t *j = malloc(XN_CASES[c].len); xn_jpeg(XN_CASES[c].id, XN_CASES[c].len, j);
        send_event(XN_CASES[c].id, j, XN_CASES[c].len, XN_CASES[c].w, XN_CASES[c].h);
        verify_fragments(XN_CASES[c].id, j, XN_CASES[c].len, XN_CASES[c].w, XN_CASES[c].h);
        for (int i = 0; out && i < stub_sent_count; i++) {
            uint16_t n = (uint16_t)stub_sent_len[i];
            fwrite(&n, 2, 1, out); fwrite(stub_sent_buf[i], 1, n, out);
        }
        free(j);
    }
    if (out) fclose(out);
}

static void t_no_jpeg_sends_nothing(void)
{
    setup();
    CHECK(send_event(900, NULL, 0, 320, 240) == 0);            // khong co anh -> khong gui
    uint8_t x = 1;
    CHECK(send_event(901, &x, 0, 320, 240) == 0);              // jpeg_len = 0 -> khong gui
    if (!stub_was_freed(last_buf)) free(last_buf);             // (xem t_zero_length_buffer_is_leaked)
}

static void t_task_frees_buffer_after_sending(void)
{
    setup();
    uint8_t *j = malloc(1000); xn_jpeg(960, 1000, j);
    send_event(960, j, 1000, 320, 240);
    CHECK_MSG(stub_was_freed(last_buf), "task gui xong phai free(jpeg_data) (face_engine cap phat, comm_espnow giai phong)");
    free(j);
}

static void t_zero_length_buffer_is_leaked(void)
{
    // jpeg_data != NULL nhung jpeg_len == 0: task bo qua VA KHONG free -> ro bo nho.
    setup();
    uint8_t x = 1;
    send_event(961, &x, 0, 320, 240);
    if (!stub_was_freed(last_buf)) {
        mt_bug_seen = 1;
        free(last_buf);                            // don dep de LeakSanitizer khong bao nham
    }
}

static void t_send_failures_do_not_stop_remaining_fragments(void)
{
    setup();
    uint8_t *j = malloc(2300); xn_jpeg(950, 2300, j);
    stub_espnow_send_rc = ESP_FAIL;                            // moi lan gui deu bao loi
    int n = send_event(950, j, 2300, 320, 240);
    stub_espnow_send_rc = ESP_OK;
    free(j);
    CHECK_MSG(n == 10, "gui loi van phai thu du 10 manh, thuc te %d", n);
    INFO("khi esp_now_send loi: code van thu het cac manh, KHONG gui lai manh loi (anh se mat va bi bo ben B sau timeout)");
}

static void t_back_to_back_events_do_not_mix(void)
{
    setup();
    for (uint32_t id = 700; id < 705; id++) {
        uint32_t len = 500 + (id - 700) * 333;
        uint8_t *j = malloc(len); xn_jpeg(id, len, j);
        send_event(id, j, len, 320, 240);
        verify_fragments(id, j, len, 320, 240);
        free(j);
    }
}

int main(void)
{
    SUITE("node_a_espnow_send");
    RUN(t_fragment_sizes_edge_cases,                  "chia manh dung o cac bien 1/229/230/231/460/461/4097 byte");
    RUN(t_export_cases_for_node_b,                    "8 anh (1 byte -> 150 KB): header, do dai, ghep lai == anh goc");
    RUN(t_no_jpeg_sends_nothing,                      "khong co anh / jpeg_len=0 -> khong gui goi nao");
    RUN(t_task_frees_buffer_after_sending,            "gui xong -> task giai phong jpeg_data (khong ro bo nho)");
    RUN_REPRO(t_zero_length_buffer_is_leaked,         "HAN CHE: jpeg_data != NULL nhung jpeg_len = 0 -> KHONG duoc free (ro bo nho)");
    RUN(t_send_failures_do_not_stop_remaining_fragments, "esp_now_send loi -> van thu het cac manh con lai");
    RUN(t_back_to_back_events_do_not_mix,             "5 su kien lien tiep khong tron noi dung");
    return mt_exit_code();
}
