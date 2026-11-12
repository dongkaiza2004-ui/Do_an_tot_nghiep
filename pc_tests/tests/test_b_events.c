// test_b_events.c - Test NODE B: bus su kien (access_events.c) va nhat ky events.log (sd_storage.c).
// Dung code THAT; the SD = thu muc "sdtest"; task ghi log duoc chay bang tay (stub_task_fn).
#define _GNU_SOURCE
#include "mini_test.h"
#include "access_events.h"
#include "sd_storage.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LOGF SD_STORAGE_MOUNT_POINT "/events.log"
#define OLDF SD_STORAGE_MOUNT_POINT "/events.old"

static access_event_t ev(uint32_t id, access_event_type_t t, uint16_t face, uint16_t matter, float sim, int64_t unix_s)
{
    access_event_t e; memset(&e, 0, sizeof(e));
    e.event_id = id; e.type = t; e.face_user_id = face; e.matter_user_index = matter;
    e.similarity = sim; e.unix_time_s = unix_s; return e;
}
static void sh(const char *c) { if (system(c) != 0) { } }
static void drain_log(void) { stub_idle_polls = 0; if (setjmp(stub_rx_exit) == 0) stub_task_fn(NULL); }   // chay task ghi log den het hang doi
static void reset_logs(void) { sh("rm -f " LOGF " " OLDF); }
static int count_lines(const char *p) { FILE *f = fopen(p, "r"); int n = 0, c; if (!f) return -1; while ((c = fgetc(f)) != EOF) if (c == '\n') n++; fclose(f); return n; }
static long fsize(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

static int cb_count; static access_event_t cb_last; static void *cb_ctx_seen;
static void on_ev(const access_event_t *e, void *ctx) { cb_count++; cb_last = *e; cb_ctx_seen = ctx; }
static int cb2_count;
static void on_ev2(const access_event_t *e, void *ctx) { (void)e; (void)ctx; cb2_count++; }

// ============================= access_events ================================================
static void t_publish_before_init_rejected(void)
{
    access_event_t e = ev(1, ACCESS_EVENT_GRANTED, 1, 1, 0.9f, 0);
    CHECK(access_events_publish(&e) == ESP_ERR_INVALID_STATE);
    CHECK(access_events_subscribe(on_ev, NULL) == ESP_ERR_INVALID_STATE);
    access_event_t out[4]; CHECK(access_events_get_recent(out, 4) == 0);
    CHECK(access_events_restore(&e, 1) != ESP_OK);
}
static void t_init_idempotent_and_args(void)
{
    CHECK(access_events_init() == ESP_OK);
    CHECK(access_events_init() == ESP_OK);
    CHECK(access_events_publish(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(access_events_subscribe(NULL, NULL) == ESP_ERR_INVALID_ARG);
    access_event_t out[2];
    CHECK(access_events_get_recent(NULL, 2) == 0 && access_events_get_recent(out, 0) == 0);
}
static void t_publish_fields_and_timestamps(void)
{
    stub_now_us = 123456;
    access_event_t e = ev(500, ACCESS_EVENT_UNKNOWN_FACE, 0, 0, 0.0f, 0);
    CHECK(access_events_publish(&e) == ESP_OK);
    access_event_t out[1]; CHECK(access_events_get_recent(out, 1) == 1);
    CHECK(out[0].event_id == 500 && out[0].type == ACCESS_EVENT_UNKNOWN_FACE);
    CHECK_MSG(out[0].timestamp_us == 123456, "timestamp_us tu dien tu esp_timer khi = 0, thuc te %lld", (long long)out[0].timestamp_us);
    access_event_t e2 = ev(501, ACCESS_EVENT_GRANTED, 3, 2, 0.8f, 1700000999); e2.timestamp_us = 777;
    access_events_publish(&e2); access_events_get_recent(out, 1);
    CHECK(out[0].timestamp_us == 777 && out[0].unix_time_s == 1700000999);               // gia tri dat san duoc GIU
}
static void t_ring_buffer_keeps_32_newest(void)
{
    for (uint32_t i = 0; i < 40; i++) { access_event_t e = ev(1000 + i, ACCESS_EVENT_GRANTED, 1, 1, 0.5f, 0); access_events_publish(&e); }
    access_event_t out[100]; size_t n = access_events_get_recent(out, 100);
    CHECK_MSG(n == 32, "lich su toi da 32, thuc te %zu", n);
    CHECK(out[0].event_id == 1039 && out[31].event_id == 1008);                          // moi nhat truoc
    n = access_events_get_recent(out, 5);
    CHECK(n == 5 && out[0].event_id == 1039 && out[4].event_id == 1035);
}
static void t_subscribers(void)
{
    cb_count = 0; cb2_count = 0; int tag = 42;
    CHECK(access_events_subscribe(on_ev, &tag) == ESP_OK);
    CHECK(access_events_subscribe(on_ev2, NULL) == ESP_OK);
    CHECK(access_events_subscribe(on_ev2, NULL) == ESP_OK);          // thu 3 (toi da 3)
    CHECK(access_events_subscribe(on_ev2, NULL) != ESP_OK);          // thu 4 bi tu choi
    access_event_t e = ev(2000, ACCESS_EVENT_DENIED, 7, 4, 0.66f, 0);
    CHECK(access_events_publish(&e) == ESP_OK);
    CHECK(cb_count == 1 && cb2_count == 2 && cb_ctx_seen == &tag);
    CHECK(cb_last.event_id == 2000 && cb_last.type == ACCESS_EVENT_DENIED && cb_last.face_user_id == 7);
}
static void t_restore_order_and_no_callbacks(void)
{
    int before = cb_count;
    access_event_t old[5];
    for (int i = 0; i < 5; i++) old[i] = ev(3000 + (uint32_t)i, ACCESS_EVENT_GRANTED, 1, 1, 0.5f, 100 + i);   // CU -> MOI
    CHECK(access_events_restore(old, 5) == ESP_OK);
    CHECK_MSG(cb_count == before, "restore khong duoc goi subscriber (se day su kien cu len dashboard nhu moi)");
    access_event_t out[5]; CHECK(access_events_get_recent(out, 5) == 5);
    CHECK(out[0].event_id == 3004 && out[4].event_id == 3000);
    access_event_t many[40];
    for (int i = 0; i < 40; i++) many[i] = ev(4000 + (uint32_t)i, ACCESS_EVENT_GRANTED, 1, 1, 0.5f, 0);
    CHECK(access_events_restore(many, 40) == ESP_OK);
    access_event_t o2[40]; size_t n = access_events_get_recent(o2, 40);
    CHECK(n == 32 && o2[0].event_id == 4039 && o2[31].event_id == 4008);
}

// ============================= nhat ky events.log ===========================================
static void t_sd_init(void) { sh("rm -rf " SD_STORAGE_MOUNT_POINT); CHECK(sd_storage_init() == ESP_OK); CHECK(stub_task_fn != NULL); }

static void t_log_line_format_exact(void)
{
    reset_logs();
    access_event_t e = ev(5, ACCESS_EVENT_GRANTED, 42, 3, 0.873f, 1700000123);
    CHECK(sd_storage_log_event(&e) == ESP_OK);
    drain_log();
    FILE *f = fopen(LOGF, "r"); char line[128] = ""; CHECK(f != NULL);
    if (f) { if (!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f); }
    CHECK_MSG(strcmp(line, "1700000123,5,0,42,3,873\n") == 0, "dong log = '%s'", line);
}
static void t_log_load_roundtrip_oldest_first(void)
{
    reset_logs();
    for (uint32_t i = 0; i < 5; i++) { access_event_t e = ev(10 + i, (access_event_type_t)(i % 4), (uint16_t)(20 + i), (uint16_t)i, 0.5f + 0.01f * (float)i, 1700000000 + i); sd_storage_log_event(&e); }
    drain_log();
    access_event_t out[32]; size_t n = sd_storage_load_recent_events(out, 32);
    CHECK_MSG(n == 5, "nap lai %zu/5 su kien", n);
    for (size_t i = 0; i < n && i < 5; i++) {
        CHECK(out[i].event_id == 10 + i && (int)out[i].type == (int)(i % 4) && out[i].face_user_id == 20 + i && out[i].matter_user_index == i);
        CHECK(out[i].unix_time_s == (int64_t)(1700000000 + i));
        CHECK(fabsf(out[i].similarity - (0.5f + 0.01f * (float)i)) < 0.0011f);
    }
    n = sd_storage_load_recent_events(out, 2);                                          // chi lay 2 gan nhat, van CU -> MOI
    CHECK(n == 2 && out[0].event_id == 13 && out[1].event_id == 14);
}
static void t_similarity_precision(void)
{
    reset_logs(); double worst = 0;
    for (int k = 0; k <= 100; k++) { access_event_t e = ev((uint32_t)k + 1, ACCESS_EVENT_GRANTED, 1, 1, (float)k / 100.0f, 1); sd_storage_log_event(&e); if (k % 16 == 15) drain_log(); }
    drain_log();
    access_event_t out[128]; size_t n = sd_storage_load_recent_events(out, 128);
    for (size_t i = 0; i < n; i++) { double err = fabs((double)out[i].similarity - (double)i / 100.0); if (err > worst) worst = err; }
    INFO("do tin cay luu x1000: sai so lon nhat %.4f (n=%zu dong nap duoc)", worst, n);
    CHECK(n >= 100 && worst < 0.0011);
}
static void t_log_queue_full_drops_with_error(void)
{
    reset_logs(); int ok = 0, dropped = 0;
    for (uint32_t i = 0; i < 40; i++) { access_event_t e = ev(i + 1, ACCESS_EVENT_GRANTED, 1, 1, 0.5f, 1); if (sd_storage_log_event(&e) == ESP_OK) ok++; else dropped++; }
    drain_log();
    CHECK_MSG(ok == 16 && dropped == 24, "hang doi ghi log 16: nhan %d, rot %d", ok, dropped);
    CHECK(count_lines(LOGF) == 16);
    INFO("burst > 16 su kien ma task ghi the chua kip: cac su kien du bi MAT khoi nhat ky (bao loi cho nguoi goi, khong thu lai)");
}
static void t_corrupted_lines_skipped(void)
{
    reset_logs();
    FILE *f = fopen(LOGF, "w");
    fputs("garbage line without commas\n", f);
    fputs("1700000001,7,0,1,1,500\n", f);            // hop le
    fputs("1700000002,8,99,1,1,500\n", f);           // type = 99 -> loai
    fputs("1700000003,9,1,2\n", f);                  // thieu truong -> loai
    fputs("\n\n", f);
    fputs("1700000004,10,2,0,0,0\n", f);             // hop le
    fputs("1700000005,11,3,0,0", f);                 // dong cuoi bi cat (khong co \\n, thieu 1 truong) -> loai
    fclose(f);
    access_event_t out[16]; size_t n = sd_storage_load_recent_events(out, 16);
    CHECK_MSG(n == 2 && out[0].event_id == 7 && out[1].event_id == 10, "chi 2 dong hop le duoc nap, thuc te %zu", n);
}
static void t_big_file_only_tail_read_first_partial_line_dropped(void)
{
    reset_logs();
    FILE *f = fopen(LOGF, "w");
    for (uint32_t i = 1; i <= 2000; i++) fprintf(f, "1700000000,%u,0,1,1,500\n", i);     // ~50 KB, doc khoi dong chi doc 8 KB cuoi
    fclose(f);
    access_event_t out[32]; size_t n = sd_storage_load_recent_events(out, 32);
    CHECK(n == 32 && out[31].event_id == 2000 && out[0].event_id == 1969);
    for (size_t i = 1; i < n; i++) CHECK(out[i].event_id == out[i - 1].event_id + 1);
}
static void t_rotation_keeps_history_continuous(void)
{
    reset_logs();
    uint32_t id = 1; int batches = 0;
    while (fsize(OLDF) < 0 && batches < 2000) {                       // ghi cho den khi events.log bi xoay vong
        for (int k = 0; k < 16; k++) { access_event_t e = ev(id++, ACCESS_EVENT_GRANTED, 1, 1, 0.5f, 1700000000); sd_storage_log_event(&e); }
        drain_log(); batches++;
    }
    CHECK_MSG(fsize(OLDF) > 0, "events.log khong xoay vong sau %d dot", batches);
    long sz_new = fsize(LOGF);
    CHECK_MSG(sz_new >= 0 && sz_new < SD_STORAGE_LOG_ROTATE_BYTES, "events.log moi con %ld byte", sz_new);
    access_event_t out[32]; size_t n = sd_storage_load_recent_events(out, 32);
    CHECK_MSG(n == 32, "nap lai %zu/32 su kien sau xoay vong", n);
    for (size_t i = 1; i < n; i++) CHECK_MSG(out[i].event_id == out[i - 1].event_id + 1, "mat su kien qua ranh gioi xoay vong: %u -> %u", out[i - 1].event_id, out[i].event_id);
    CHECK(out[n - 1].event_id == id - 1);
    INFO("xoay vong tai %.0f KB sau %u su kien; events.log moi chi co %ld byte nhung van nap du 32 su kien lien tuc nho events.old",
         SD_STORAGE_LOG_ROTATE_BYTES / 1024.0, id - 1, sz_new);
}
static void t_limit_duplicate_event_ids_in_log_after_node_a_reboot(void)
{
    reset_logs();
    for (int session = 0; session < 2; session++)                    // Node A cap ID tu 1 sau moi lan khoi dong
        for (uint32_t i = 1; i <= 3; i++) { access_event_t e = ev(i, ACCESS_EVENT_GRANTED, 1, 1, 0.5f, 1700000000 + session * 3600); sd_storage_log_event(&e); }
    drain_log();
    access_event_t out[16]; size_t n = sd_storage_load_recent_events(out, 16);
    int distinct = 0;
    for (size_t i = 0; i < n; i++) { bool dup = false; for (size_t j = 0; j < i; j++) if (out[j].event_id == out[i].event_id) dup = true; if (!dup) distinct++; }
    INFO("6 su kien (2 lan khoi dong x 3) nhung chi co %d event_id khac nhau -> anh /sdcard/img/<id>.jpg khong the phan biet", distinct);
    if (n == 6 && distinct == 3) mt_bug_seen = 1;
}

int main(void)
{
    SUITE("node_b_access_events");
    RUN(t_publish_before_init_rejected,        "goi truoc init -> bi tu choi, khong crash");
    RUN(t_init_idempotent_and_args,            "init goi lai an toan; tham so NULL/0 bi tu choi");
    RUN(t_publish_fields_and_timestamps,       "publish giu nguyen truong; timestamp_us tu dien khi = 0, giu gia tri dat san");
    RUN(t_ring_buffer_keeps_32_newest,         "lich su vong tron 32: giu 32 su kien MOI nhat, moi nhat truoc");
    RUN(t_subscribers,                         "toi da 3 subscriber, nhan dung ban sao + ctx, thu 4 bi tu choi");
    RUN(t_restore_order_and_no_callbacks,      "restore (CU->MOI) khong goi subscriber; > 32 chi giu 32 moi nhat");
    SUITE("node_b_event_log");
    RUN(t_sd_init,                             "khoi tao the SD gia");
    RUN(t_log_line_format_exact,               "dong log dung dinh dang: unix,id,type,face,matter,sim*1000");
    RUN(t_log_load_roundtrip_oldest_first,     "ghi 5 su kien -> nap lai dung tung truong, thu tu CU -> MOI");
    RUN(t_similarity_precision,                "do tin cay 0.00..1.00 luu x1000: sai so < 0.0011");
    RUN(t_log_queue_full_drops_with_error,     "hang doi ghi log 16: rot kem ma loi ESP_ERR_NO_MEM, khong crash");
    RUN(t_corrupted_lines_skipped,             "dong hong / type=99 / thieu truong / dong cuoi bi cat: bi bo qua");
    RUN(t_big_file_only_tail_read_first_partial_line_dropped, "file log lon: chi doc 8 KB cuoi, dong dau bi cat duoc bo");
    RUN(t_rotation_keeps_history_continuous,   "xoay vong >256 KB: events.old tao ra, lich su nap lai LIEN TUC qua ranh gioi");
    RUN_REPRO(t_limit_duplicate_event_ids_in_log_after_node_a_reboot, "HAN CHE: nhat ky co event_id TRUNG sau khi Node A khoi dong lai");
    sh("rm -rf " SD_STORAGE_MOUNT_POINT);
    return mt_exit_code();
}
