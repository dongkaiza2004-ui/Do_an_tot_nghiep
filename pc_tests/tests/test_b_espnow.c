// test_b_espnow.c - Test PHIA NHAN/GHEP anh cua NODE B (comm_espnow.c THAT cua nhanh nodeB).
// Dong thoi nap cac manh do NODE A that gui (a_frags.bin) de kiem tra A -> B tron ven.
#include "mini_test.h"
#include "comm_espnow.h"
#include "espnow_frame.h"
#include "xnode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Ghi lai anh ghep xong -----------------------------------------------------------
typedef struct { uint32_t id; uint16_t w, h; size_t len; uint8_t *data; } img_t;
#define MAXIMG 64
static img_t imgs[MAXIMG];
static int nimg;
static int cb2_count;

static void on_image(uint32_t id, uint16_t w, uint16_t h, const uint8_t *d, size_t len, void *ctx)
{
    (void)ctx;
    if (nimg < MAXIMG) {
        imgs[nimg].id = id; imgs[nimg].w = w; imgs[nimg].h = h; imgs[nimg].len = len;
        imgs[nimg].data = malloc(len ? len : 1); memcpy(imgs[nimg].data, d, len);
    }
    nimg++;
}
static void on_image2(uint32_t id, uint16_t w, uint16_t h, const uint8_t *d, size_t len, void *ctx)
{ (void)id; (void)w; (void)h; (void)d; (void)len; (void)ctx; cb2_count++; }

static void forget_images(void)
{
    for (int i = 0; i < nimg && i < MAXIMG; i++) free(imgs[i].data);
    nimg = 0; cb2_count = 0;
}

// ---- Dong ho + chay task --------------------------------------------------------------
static int64_t now_us = 1000000;
static void run_task(void)                       // rut can hang doi, roi quet timeout 1 lan
{
    stub_now_us = now_us;
    stub_idle_polls = 1;
    if (setjmp(stub_rx_exit) == 0) stub_task_fn(NULL);
}
static void advance_ms(int ms) { now_us += (int64_t)ms * 1000; }

static void setup(void)
{
    static bool inited = false;
    if (!inited) {
        CHECK(comm_espnow_init() == ESP_OK);
        comm_espnow_start();
        CHECK(comm_espnow_register_image_handler(on_image, NULL) == ESP_OK);
        inited = true;
    }
    CHECK(stub_task_fn != NULL && stub_espnow_recv_cb != NULL);
    advance_ms(10000); run_task();               // xoa moi anh dang ghep do test truoc de lai
    forget_images();
}

// ---- Tao & gui manh --------------------------------------------------------------------
static int build_frag(uint8_t *buf, uint32_t id, uint16_t w, uint16_t h, uint32_t jlen,
                      uint16_t idx, uint16_t total, const uint8_t *pl, uint16_t plen)
{
    espnow_frame_fragment_t f; memset(&f, 0, sizeof(f));
    f.event_id = id; f.width = w; f.height = h; f.jpeg_len = jlen;
    f.fragment_index = idx; f.fragment_total = total; f.payload_len = plen;
    if (plen) memcpy(f.payload, pl, plen > 230 ? 230 : plen);
    int n = (int)offsetof(espnow_frame_fragment_t, payload) + (plen > 230 ? 230 : plen);
    memcpy(buf, &f, (size_t)n);
    return n;
}
static void deliver(const uint8_t *buf, int n) { stub_espnow_recv_cb(NULL, buf, n); }

// Gui manh `idx` cua anh (id,len) sinh tu xn_jpeg; `src` la du lieu goc.
static void send_frag(uint32_t id, const uint8_t *src, uint32_t len, uint16_t idx, bool drain)
{
    uint16_t total = (uint16_t)((len + 229) / 230);
    uint32_t off = (uint32_t)idx * 230;
    uint32_t pl = len - off < 230 ? len - off : 230;
    uint8_t buf[300];
    int n = build_frag(buf, id, 320, 240, len, idx, total, src + off, (uint16_t)pl);
    deliver(buf, n);
    if (drain) run_task();
}
static void send_image(uint32_t id, const uint8_t *src, uint32_t len)
{
    uint16_t total = (uint16_t)((len + 229) / 230);
    for (uint16_t i = 0; i < total; i++) send_frag(id, src, len, i, true);
}
static uint8_t *mk(uint32_t id, uint32_t len) { uint8_t *p = malloc(len ? len : 1); xn_jpeg(id, len, p); return p; }
static bool img_is(int k, uint32_t id, const uint8_t *src, uint32_t len)
{
    return k < nimg && k < MAXIMG && imgs[k].id == id && imgs[k].len == len && memcmp(imgs[k].data, src, len) == 0;
}

// ================================ Test binh thuong ========================================
static void t_single_fragment_image(void)
{
    setup();
    uint8_t *j = mk(11, 100); send_image(11, j, 100);
    CHECK(nimg == 1 && img_is(0, 11, j, 100));
    CHECK(imgs[0].w == 320 && imgs[0].h == 240);
    free(j);
}

static void t_multi_fragment_in_order(void)
{
    setup();
    uint8_t *j = mk(12, 3000); send_image(12, j, 3000);          // 14 manh
    CHECK(nimg == 1 && img_is(0, 12, j, 3000));
    free(j);
}

static void t_reverse_order(void)
{
    setup();
    uint8_t *j = mk(13, 2300); uint16_t total = 10;
    for (int i = total - 1; i >= 0; i--) send_frag(13, j, 2300, (uint16_t)i, true);
    CHECK(nimg == 1 && img_is(0, 13, j, 2300));
    free(j);
}

static void t_duplicates_ignored(void)
{
    setup();
    uint8_t *j = mk(14, 1000); uint16_t total = 5;
    for (uint16_t i = 0; i < total; i++) { send_frag(14, j, 1000, i, true); send_frag(14, j, 1000, i, false); }
    run_task();
    CHECK_MSG(nimg == 1, "manh gui 2 lan: anh phai duoc bao dung 1 lan, nhan %d", nimg);
    CHECK(img_is(0, 14, j, 1000));
    free(j);
}

static void t_missing_fragment_times_out_then_frees_slot(void)
{
    setup();
    uint8_t *j = mk(15, 1000);
    for (uint16_t i = 0; i < 5; i++) if (i != 2) send_frag(15, j, 1000, i, true);   // thieu manh 2
    CHECK(nimg == 0);
    advance_ms(3000); run_task();
    int w0 = stub_log_warn;
    CHECK_MSG(nimg == 0, "chua qua 4s: chua duoc huy");
    advance_ms(1500); run_task();                                  // tong 4.5s > 4s
    CHECK_MSG(stub_log_warn > w0, "qua 4s phai co canh bao huy anh thieu manh");
    send_frag(15, j, 1000, 2, true);                               // manh den tre sau khi bi huy
    CHECK_MSG(nimg == 0, "manh den tre khong duoc tu tao ra anh hoan chinh");
    advance_ms(10000); run_task();
    uint8_t *k = mk(15, 1000); send_image(15, k, 1000);            // cung event_id, gui lai du -> OK
    CHECK(nimg == 1 && img_is(0, 15, k, 1000));
    free(j); free(k);
}

static void t_two_interleaved_images(void)
{
    setup();
    uint8_t *a = mk(21, 1500), *b = mk(22, 1800);                  // 7 manh va 8 manh
    for (uint16_t i = 0; i < 8; i++) {
        if (i < 7) send_frag(21, a, 1500, i, true);
        send_frag(22, b, 1800, i, true);
    }
    CHECK(nimg == 2);
    bool a_ok = false, b_ok = false;
    for (int k = 0; k < nimg; k++) { if (img_is(k, 21, a, 1500)) a_ok = true; if (img_is(k, 22, b, 1800)) b_ok = true; }
    CHECK(a_ok && b_ok);
    free(a); free(b);
}

static void t_third_inflight_image_evicts_oldest(void)
{
    setup();
    uint8_t *a = mk(31, 920), *b = mk(32, 920), *c = mk(33, 920);   // moi anh 4 manh; toi da 2 anh ghep cung luc
    send_frag(31, a, 920, 0, true); advance_ms(10);
    send_frag(32, b, 920, 0, true); advance_ms(10);
    send_frag(33, c, 920, 0, true); advance_ms(10);                 // het slot -> huy anh 31 (cu nhat)
    for (uint16_t i = 1; i < 4; i++) { send_frag(32, b, 920, i, true); send_frag(33, c, 920, i, true); }
    bool b_ok = false, c_ok = false, a_done = false;
    for (int k = 0; k < nimg; k++) { if (img_is(k, 32, b, 920)) b_ok = true; if (img_is(k, 33, c, 920)) c_ok = true; if (imgs[k].id == 31) a_done = true; }
    CHECK(b_ok && c_ok);
    CHECK_MSG(!a_done, "anh cu nhat (31) phai bi huy de nhuong slot");
    INFO("toi da 2 anh ghep dong thoi: su kien thu 3 se lam MAT anh cu nhat dang do (neu 3 nguoi quet lien tiep, anh dau co the mat)");
    free(a); free(b); free(c);
}

static void t_invalid_fragments_rejected_and_receiver_survives(void)
{
    setup();
    uint8_t buf[300], pl[230]; memset(pl, 0x5A, sizeof(pl));
    int n;
    n = build_frag(buf, 41, 1, 1, 1000, 0, 0, pl, 100);   deliver(buf, n);   // total = 0
    n = build_frag(buf, 41, 1, 1, 1000, 5, 5, pl, 100);   deliver(buf, n);   // index >= total
    n = build_frag(buf, 41, 1, 1, 1000, 9, 5, pl, 100);   deliver(buf, n);   // index > total
    n = build_frag(buf, 41, 1, 1, 1000, 0, 5, pl, 100);
    { espnow_frame_fragment_t f; memcpy(&f, buf, (size_t)n); f.payload_len = 231; memcpy(buf, &f, (size_t)n); }
    deliver(buf, n);                                                           // payload_len > 230
    n = build_frag(buf, 41, 1, 1, 100, 1, 2, pl, 100);    deliver(buf, n);   // offset+payload > jpeg_len
    deliver(buf, 5);                                                           // goi qua ngan
    deliver(buf, 0);                                                           // goi rong
    run_task();
    CHECK(nimg == 0);
    // Goi dai hon struct: bi cat, khong tran bo nho
    uint8_t big[600]; memset(big, 0, sizeof(big));
    n = build_frag(big, 42, 1, 1, 500, 0, 3, pl, 230); deliver(big, 600); run_task();
    // Mot anh hop le ngay sau do van phai chay binh thuong
    uint8_t *j = mk(43, 700); send_image(43, j, 700);
    bool ok = false; for (int k = 0; k < nimg; k++) if (img_is(k, 43, j, 700)) ok = true;
    CHECK_MSG(ok, "sau cac goi hong, anh hop le van phai nhan duoc");
    free(j);
}

static void t_mismatched_info_between_fragments_discards_image(void)
{
    setup();
    uint8_t *j = mk(51, 1000);
    send_frag(51, j, 1000, 0, true); send_frag(51, j, 1000, 1, true);
    uint8_t buf[300]; uint8_t pl[230] = {0};
    int n = build_frag(buf, 51, 320, 240, 1000, 2, 6, pl, 230);              // total khac (6 != 5)
    deliver(buf, n); run_task();
    send_frag(51, j, 1000, 3, true); send_frag(51, j, 1000, 4, true);
    CHECK_MSG(nimg == 0, "thong tin anh khong khop giua cac manh -> khong duoc bao anh hoan chinh");
    free(j);
}

static void t_queue_overflow_drops_and_recovers(void)
{
    setup();
    uint8_t *j = mk(61, 230 * 20); int w0 = stub_log_warn;
    for (uint16_t i = 0; i < 20; i++) send_frag(61, j, 230 * 20, i, false);   // 20 goi lien tuc, hang doi chi 16
    int dropped_warns = stub_log_warn - w0;
    run_task();
    CHECK_MSG(dropped_warns == 4, "hang doi 16: goi thu 17..20 bi rot kem canh bao, thay %d canh bao", dropped_warns);
    CHECK(nimg == 0);                                                         // thieu 4 manh -> chua xong
    advance_ms(5000); run_task();
    uint8_t *k = mk(62, 500); send_image(62, k, 500);
    CHECK(nimg == 1 && img_is(0, 62, k, 500));
    INFO("burst > 16 manh ma task chua kip rut: mat manh, anh bi huy sau 4s (callback ESP-NOW khong cho)");
    free(j); free(k);
}

static void t_listeners_limit_and_multicast(void)
{
    setup();
    CHECK(comm_espnow_register_image_handler(NULL, NULL) != ESP_OK);
    CHECK(comm_espnow_register_image_handler(on_image2, NULL) == ESP_OK);     // listener thu 2
    CHECK(comm_espnow_register_image_handler(on_image2, NULL) != ESP_OK);     // thu 3 bi tu choi
    uint8_t *j = mk(71, 300); send_image(71, j, 300);
    CHECK(nimg == 1 && cb2_count == 1);                                       // CA HAI listener deu nhan
    free(j);
}

// ============================ NODE A -> NODE B (nap manh THAT) ==========================
typedef struct { uint8_t d[250]; int n; } wire_t;
static wire_t wire[3000];
static int nwire;
static int load_wire(void)
{
    FILE *f = fopen(XN_FILE, "rb"); nwire = 0;
    if (!f) return 0;
    uint16_t n;
    while (nwire < 3000 && fread(&n, 2, 1, f) == 1 && n <= 250 && fread(wire[nwire].d, 1, n, f) == n) wire[nwire++].n = n;
    fclose(f);
    return nwire;
}
static void t_frames_from_real_node_a_reassemble_exactly(void)
{
    setup();
    int total_frags = load_wire();
    CHECK_MSG(total_frags > 0, "chua co " XN_FILE " (phai chay test_a_espnow truoc)");
    if (!total_frags) return;
    int expect = 0; for (unsigned c = 0; c < XN_NCASES; c++) expect += (int)((XN_CASES[c].len + 229) / 230);
    CHECK_MSG(total_frags == expect, "so manh Node A gui = %d, ky vong %d", total_frags, expect);
    for (int i = 0; i < total_frags; i++) { deliver(wire[i].d, wire[i].n); run_task(); }
    CHECK_MSG(nimg == (int)XN_NCASES, "Node B ghep duoc %d/%u anh", nimg, (unsigned)XN_NCASES);
    for (unsigned c = 0; c < XN_NCASES && (int)c < nimg; c++) {
        uint8_t *j = mk(XN_CASES[c].id, XN_CASES[c].len);
        CHECK_MSG(img_is((int)c, XN_CASES[c].id, j, XN_CASES[c].len), "anh %u (%u byte) ghep SAI", XN_CASES[c].id, XN_CASES[c].len);
        CHECK(imgs[c].w == XN_CASES[c].w && imgs[c].h == XN_CASES[c].h);
        free(j);
    }
}
static void t_real_frames_shuffled_and_duplicated(void)
{
    // Anh 30011 byte (131 manh): xao tron thu tu + moi manh gui 2 lan.
    setup();
    if (!load_wire()) { CHECK_MSG(0, "chua co " XN_FILE); return; }
    int first = 0;
    for (unsigned c = 0; c < XN_NCASES && XN_CASES[c].id != 107; c++) first += (int)((XN_CASES[c].len + 229) / 230);
    int cnt = (int)((30011 + 229) / 230);
    int order[400], m = 0;
    for (int i = 0; i < cnt; i++) { order[m++] = first + i; order[m++] = first + i; }
    srand(5);
    for (int i = m - 1; i > 0; i--) { int r = rand() % (i + 1); int t = order[i]; order[i] = order[r]; order[r] = t; }
    for (int i = 0; i < m; i++) { deliver(wire[order[i]].d, wire[order[i]].n); if (i % 8 == 7) run_task(); }
    run_task();
    uint8_t *j = mk(107, 30011);
    CHECK_MSG(nimg == 1 && img_is(0, 107, j, 30011), "xao tron + trung lap: nhan %d anh", nimg);
    free(j);
}

// ============ HAN CHE tren code hien tai (REPRO = tai hien duoc, khong phai that bai) =========
static void t_limit_event_id_collision_different_length_loses_new_image(void)
{
    // Node A reboot khi anh cu chua ghep xong, anh moi lai mang CUNG event_id nhung kich thuoc khac.
    setup();
    uint8_t *old = mk(81, 1150), *neu = mk(81 + 100000, 2300);
    for (uint16_t i = 0; i < 3; i++) send_frag(81, old, 1150, i, true);        // anh cu: 3/5 manh
    uint16_t total = 10;
    for (uint16_t i = 0; i < total; i++) {                                      // anh moi: event_id=81, 2300 byte
        uint8_t buf[300]; int n = build_frag(buf, 81, 320, 240, 2300, i, total, neu + (size_t)i * 230, 230);
        deliver(buf, n); run_task();
    }
    if (nimg == 0) { mt_bug_seen = 1; INFO("anh moi (event_id trung) KHONG bao gio duoc ghep: manh dau bi nuot khi huy anh cu"); }
    free(old); free(neu);
}
static void t_limit_event_id_collision_same_length_makes_hybrid_image(void)
{
    setup();
    uint8_t *old = mk(82, 1150), *neu = mk(82 + 100000, 1150);                 // cung do dai, noi dung khac
    for (uint16_t i = 0; i < 3; i++) send_frag(82, old, 1150, i, true);
    for (uint16_t i = 0; i < 5; i++) send_frag(82, neu, 1150, i, true);        // manh 0..2 bi coi la trung -> bo
    bool is_old = img_is(0, 82, old, 1150), is_new = img_is(0, 82, neu, 1150);
    if (nimg == 1 && !is_old && !is_new) { mt_bug_seen = 1; INFO("anh ra la ANH LAI: 3 manh dau tu anh cu + 2 manh cuoi tu anh moi, khong co canh bao nao"); }
    free(old); free(neu);
}
static void t_limit_total_inconsistent_with_jpeg_len_completes_early(void)
{
    // fragment_total=1 nhung jpeg_len=1000: chi can 1 manh 230 byte -> bao "anh 1000 byte" voi 770 byte chua ghi.
    setup();
    uint8_t pl[230]; memset(pl, 0x7E, sizeof(pl));
    uint8_t buf[300]; int n = build_frag(buf, 83, 320, 240, 1000, 0, 1, pl, 230);
    deliver(buf, n); run_task();
    if (nimg == 1 && imgs[0].len == 1000) { mt_bug_seen = 1; INFO("khong kiem tra fragment_total == ceil(jpeg_len/230): anh 1000 byte duoc bao hoan chinh voi 770 byte CHUA GHI"); }
}
static void t_limit_short_middle_fragment_leaves_hole(void)
{
    setup();
    uint8_t pl[230]; memset(pl, 0x33, sizeof(pl));
    uint8_t b0[300], b1[300];
    int n0 = build_frag(b0, 84, 320, 240, 460, 0, 2, pl, 100);                 // manh 0 chi 100 byte (phai 230)
    int n1 = build_frag(b1, 84, 320, 240, 460, 1, 2, pl, 230);
    deliver(b0, n0); deliver(b1, n1); run_task();
    if (nimg == 1) { mt_bug_seen = 1; INFO("manh giua ngan hon 230 van duoc chap nhan -> anh co lo hong 130 byte chua ghi, van duoc bao hoan chinh"); }
}
static void t_limit_truncated_packet_counts_as_full(void)
{
    // Goi ESP-NOW ngan hon header+payload_len khai bao: phan thieu bi dien so 0 nhung van tinh la du manh.
    setup();
    uint8_t pl[230]; memset(pl, 0xEE, sizeof(pl));
    uint8_t buf[300]; int n = build_frag(buf, 85, 320, 240, 230, 0, 1, pl, 230);
    deliver(buf, n - 100); run_task();                                          // cat mat 100 byte cuoi
    if (nimg == 1) { mt_bug_seen = 1; INFO("goi bi cat 100 byte van duoc coi la manh day du (phan thieu = 0x00)"); }
}
static void t_limit_no_upper_bound_on_jpeg_len(void)
{
    // Mot manh hong/gia mao khai bao anh 8 MB: code cap phat luon, khong co tran kich thuoc.
    setup();
    uint8_t pl[230] = {0}, buf[300];
    uint32_t big = 8u * 1024 * 1024;
    int n = build_frag(buf, 86, 320, 240, big, 0, (uint16_t)((big + 229) / 230), pl, 230);
    int w0 = stub_log_warn;
    deliver(buf, n); run_task();
    bool rejected_now = stub_log_warn > w0;                                     // bi tu choi ngay -> co canh bao ngay
    advance_ms(5000); int w1 = stub_log_warn; run_task();
    bool slot_existed = stub_log_warn > w1;                                     // chi co canh bao timeout neu da cap slot
    if (!rejected_now && slot_existed) { mt_bug_seen = 1; INFO("chap nhan khai bao anh 8 MB va cap phat bo nho (PSRAM chip chi 8 MB) - nen dat tran, vd 1 MB nhu sd_storage"); }
}

int main(void)
{
    SUITE("node_b_espnow_recv");
    RUN(t_single_fragment_image,                         "anh 1 manh: ghep dung");
    RUN(t_multi_fragment_in_order,                       "anh 14 manh dung thu tu: ghep dung 100%");
    RUN(t_reverse_order,                                 "10 manh den NGUOC thu tu: ghep dung");
    RUN(t_duplicates_ignored,                            "manh gui trung 2 lan: anh chi bao 1 lan, dung noi dung");
    RUN(t_missing_fragment_times_out_then_frees_slot,    "thieu 1 manh: huy dung sau 4 giay, giai phong slot, gui lai cung ID van OK");
    RUN(t_two_interleaved_images,                        "2 anh xen ke manh: ca 2 ghep dung, khong tron");
    RUN(t_third_inflight_image_evicts_oldest,            "anh thu 3 dang ghep: huy anh cu nhat, 2 anh con lai ghep dung");
    RUN(t_invalid_fragments_rejected_and_receiver_survives, "goi sai (total=0, index>=total, payload>230, vuot jpeg_len, qua ngan/dai): bi bo, khong crash");
    RUN(t_mismatched_info_between_fragments_discards_image, "thong tin manh khong khop nhau -> huy anh, khong bao hoan chinh");
    RUN(t_queue_overflow_drops_and_recovers,             "burst 20 manh (hang doi 16): rot kem canh bao, phuc hoi sau timeout");
    RUN(t_listeners_limit_and_multicast,                 "toi da 2 listener anh, ca hai deu nhan, tham so sai bi tu choi");

    SUITE("node_a_to_b_espnow");
    RUN(t_frames_from_real_node_a_reassemble_exactly,    "8 anh (1 B -> 150 KB) do Node A THAT chia manh -> Node B ghep dung tung byte");
    RUN(t_real_frames_shuffled_and_duplicated,           "anh 30 KB tu Node A: xao tron thu tu + trung lap van ghep dung");

    SUITE("node_b_espnow_limits");
    RUN_REPRO(t_limit_event_id_collision_different_length_loses_new_image, "event_id TRUNG (anh cu do dang) + khac do dai -> anh MOI bi mat");
    RUN_REPRO(t_limit_event_id_collision_same_length_makes_hybrid_image,   "event_id TRUNG (anh cu do dang) + cung do dai -> anh LAI 2 anh");
    RUN_REPRO(t_limit_total_inconsistent_with_jpeg_len_completes_early,    "fragment_total khong khop jpeg_len -> anh 'hoan chinh' nhung thieu du lieu");
    RUN_REPRO(t_limit_short_middle_fragment_leaves_hole,                   "manh giua ngan hon 230 -> anh co lo hong nhung van bao hoan chinh");
    RUN_REPRO(t_limit_truncated_packet_counts_as_full,                     "goi bi cat ngan hon payload_len -> van tinh la manh day du");
    RUN_REPRO(t_limit_no_upper_bound_on_jpeg_len,                          "khai bao jpeg_len 8 MB van duoc cap phat (khong co tran)");
    forget_images();
    return mt_exit_code();
}
