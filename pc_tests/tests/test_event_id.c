// test_event_id.c - Test bo cap event_id (event_id.h) va TAI HIEN loi cua cach cap cu.
#include "mini_test.h"
#include "event_id.h"
#include <stdlib.h>

// ---- "NVS gia" trong RAM --------------------------------------------------------
typedef struct { uint32_t val; bool has; int writes; bool fail_store; } fake_nvs_t;

static bool fake_load(uint32_t *out, void *ctx)
{
    fake_nvs_t *n = ctx;
    if (!n->has) return false;
    *out = n->val;
    return true;
}
static bool fake_store(uint32_t v, void *ctx)
{
    fake_nvs_t *n = ctx;
    if (n->fail_store) return false;
    n->val = v; n->has = true; n->writes++;
    return true;
}
static bool boot(event_id_gen_t *g, fake_nvs_t *n)   // = 1 lan khoi dong
{
    return event_id_init(g, fake_load, fake_store, n, EVENT_ID_DEFAULT_RESERVE);
}

static uint32_t lcg_state = 12345;
static uint32_t lcg(void) { lcg_state = lcg_state * 1664525u + 1013904223u; return lcg_state >> 8; }

// ---- Test -------------------------------------------------------------------------
static void t_first_boot_starts_at_1(void)
{
    fake_nvs_t n = {0}; event_id_gen_t g;
    CHECK(boot(&g, &n));
    CHECK(event_id_next(&g) == 1);
    CHECK(event_id_next(&g) == 2);
}

static void t_strictly_increasing_in_one_boot(void)
{
    fake_nvs_t n = {0}; event_id_gen_t g; boot(&g, &n);
    uint32_t prev = 0;
    for (int i = 0; i < 5000; i++) {
        uint32_t id = event_id_next(&g);
        CHECK_MSG(id == prev + 1, "ID khong lien tuc: %u sau %u", id, prev);
        prev = id;
        if (mt_fail) return;
    }
}

static void t_never_reuses_across_200_random_reboots(void)
{
    fake_nvs_t n = {0}; event_id_gen_t g;
    uint32_t max_seen = 0;
    long total = 0;
    for (int b = 0; b < 200; b++) {
        boot(&g, &n);
        int k = (int)(lcg() % 101);
        for (int i = 0; i < k; i++) {
            uint32_t id = event_id_next(&g);
            CHECK_MSG(id > max_seen, "TRUNG/LUI: boot %d cap %u <= %u da dung", b, id, max_seen);
            if (mt_fail) return;
            max_seen = id; total++;
        }
    }
    INFO("200 lan reboot, %ld ID da cap, ID lon nhat %u", total, max_seen);
}

static void t_reboot_right_at_reserve_boundaries(void)
{
    // Khoi dong lai ngay truoc/sau ranh gioi 32, 64, 96 su kien (cho de sai nhat).
    int counts[] = {31, 32, 33, 63, 64, 65, 95, 96, 97};
    for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
        fake_nvs_t n = {0}; event_id_gen_t g;
        boot(&g, &n);
        uint32_t last = 0;
        for (int i = 0; i < counts[c]; i++) last = event_id_next(&g);
        boot(&g, &n);                                   // mat dien ngay luc nay
        uint32_t first_after = event_id_next(&g);
        CHECK_MSG(first_after > last, "sau %d event: ID truoc=%u, ID sau reboot=%u", counts[c], last, first_after);
    }
}

static void t_flash_writes_are_rare(void)
{
    fake_nvs_t n = {0}; event_id_gen_t g; boot(&g, &n);
    for (int i = 0; i < 10000; i++) event_id_next(&g);
    INFO("10000 su kien -> %d lan ghi flash (cach ngay tho: 10000 lan)", n.writes);
    CHECK(n.writes <= (int)(10000 / EVENT_ID_DEFAULT_RESERVE) + 2);
}

static void t_missing_key_means_fresh_start(void)
{
    fake_nvs_t n = {0};  n.has = false; n.val = 0xDEADBEEF;   // gia tri rac nhung chua "co key"
    event_id_gen_t g; boot(&g, &n);
    CHECK(event_id_next(&g) == 1);
}

static void t_store_failure_is_reported_not_fatal(void)
{
    fake_nvs_t n = {0}; n.fail_store = true; event_id_gen_t g;
    CHECK(boot(&g, &n) == false);            // bao loi luu
    CHECK(g.persist_ok == false);
    uint32_t a = event_id_next(&g), b = event_id_next(&g);
    CHECK(b == a + 1);                       // van cap ID binh thuong trong lan chay nay
}

static void t_reserve_zero_uses_default_and_reserve_one_works(void)
{
    fake_nvs_t n = {0}; event_id_gen_t g;
    event_id_init(&g, fake_load, fake_store, &n, 0);
    CHECK(g.reserve == EVENT_ID_DEFAULT_RESERVE);

    fake_nvs_t m = {0}; event_id_gen_t h;
    event_id_init(&h, fake_load, fake_store, &m, 1);     // ghi moi ID: cham nhung van dung
    uint32_t last = 0;
    for (int i = 0; i < 50; i++) last = event_id_next(&h);
    event_id_init(&h, fake_load, fake_store, &m, 1);
    CHECK(event_id_next(&h) > last);
}

// ---- Tai hien loi cua cach cap CU (bien dem trong RAM, ++counter tu 0) -------------
static void t_legacy_counter_repeats_ids_after_reboot(void)
{
    uint32_t legacy_counter = 0;                          // y het s_event_id_counter = 0
    uint32_t last_boot1 = 0;
    for (int i = 0; i < 10; i++) last_boot1 = ++legacy_counter;
    legacy_counter = 0;                                   // reboot Node A: RAM mat
    uint32_t first_boot2 = ++legacy_counter;
    INFO("cach cu: boot 1 cap toi ID %u, sau reboot ID dau tien lai la %u", last_boot1, first_boot2);
    if (first_boot2 <= last_boot1) mt_bug_seen = 1;
}

int main(void)
{
    SUITE("event_id");
    RUN(t_first_boot_starts_at_1,                    "lan khoi dong dau tien bat dau tu ID 1");
    RUN(t_strictly_increasing_in_one_boot,           "ID tang dan lien tuc trong 1 lan chay (5000 ID)");
    RUN(t_never_reuses_across_200_random_reboots,    "200 lan reboot ngau nhien: khong bao gio dung lai ID");
    RUN(t_reboot_right_at_reserve_boundaries,        "reboot ngay tai ranh gioi 32/64/96: van khong trung");
    RUN(t_flash_writes_are_rare,                     "so lan ghi flash it hon ~30 lan so voi ghi moi su kien");
    RUN(t_missing_key_means_fresh_start,             "chua co key trong NVS -> bat dau tu 1, khong doc gia tri rac");
    RUN(t_store_failure_is_reported_not_fatal,       "ghi NVS loi -> bao persist_ok=false, van cap ID duoc");
    RUN(t_reserve_zero_uses_default_and_reserve_one_works, "tham so reserve: 0 -> mac dinh, 1 -> van dung");
    RUN_REPRO(t_legacy_counter_repeats_ids_after_reboot, "CACH CU: sau reboot ID quay ve 1 (tai hien loi)");
    return mt_exit_code();
}
