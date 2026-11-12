// test_sd_storage.c - Test sd_storage.c THAT cua Node B tren PC (the SD = thu muc tren dia).
// Build bang run_tests.py: duong dan the doi thanh "sdtest", SD_STORAGE_MAX_IMAGES doi thanh 5.
//
// Phan 2 mo phong "Node A khoi dong lai": moi lan khoi dong = 1 TIEN TRINH MOI
// (RAM mat het), con thu muc the SD va file NVS gia thi giu nguyen.
#define _GNU_SOURCE
#include "mini_test.h"
#include "sd_storage.h"
#include "event_id.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MNT  SD_STORAGE_MOUNT_POINT
#define IMG  MNT "/img"
#define NVS_FILE "nvs_sim.bin"
#define CHRONO   "chrono.txt"
#define MAXN SD_STORAGE_MAX_IMAGES

static void sh(const char *cmd) { if (system(cmd) != 0) { /* bo qua */ } }
static void clean_all(void) { sh("rm -rf " MNT " " NVS_FILE " " CHRONO); }

static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
static int count_files(void)
{
    int n = 0; DIR *d = opendir(IMG); if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) if (strlen(e->d_name) == 12 && strcasecmp(e->d_name + 8, ".jpg") == 0) n++;
    closedir(d); return n;
}

// ================= PHAN 1: don vi (1 tien trinh, the moi) ==========================
static void t_save_before_init_is_rejected(void)
{
    uint8_t x = 1;
    CHECK(sd_storage_save_image(1, &x, 1) == ESP_ERR_INVALID_STATE);
    CHECK(!sd_storage_is_mounted());
}

static void t_init_mounts_and_creates_dir(void)
{
    CHECK(sd_storage_init() == ESP_OK);
    CHECK(sd_storage_is_mounted());
    CHECK(exists(IMG));
    CHECK(sd_storage_init() == ESP_OK);                       // goi lai an toan
}

static void t_save_read_roundtrip_and_hex_filename(void)
{
    uint8_t data[300]; for (int i = 0; i < 300; i++) data[i] = (uint8_t)(i * 3);
    CHECK(sd_storage_save_image(0xABCDEF01u, data, sizeof(data)) == ESP_OK);
    CHECK(sd_storage_image_exists(0xABCDEF01u));
    CHECK(exists(IMG "/ABCDEF01.jpg"));                        // 8 ky tu HEX in hoa
    uint8_t *out = NULL; size_t len = 0;
    CHECK(sd_storage_read_image(0xABCDEF01u, &out, &len) == ESP_OK);
    CHECK(len == sizeof(data) && out && memcmp(out, data, len) == 0);
    free(out);
    CHECK(!sd_storage_image_exists(0x12345678u));
}

static void t_invalid_args_rejected_no_garbage(void)
{
    uint8_t x = 1;
    CHECK(sd_storage_save_image(5, NULL, 10) == ESP_ERR_INVALID_ARG);
    CHECK(sd_storage_save_image(5, &x, 0) == ESP_ERR_INVALID_ARG);
    static uint8_t big[1024 * 1024 + 1];
    CHECK(sd_storage_save_image(5, big, sizeof(big)) == ESP_ERR_INVALID_ARG);
    CHECK(!exists(IMG "/00000005.jpg"));
    CHECK(!exists(IMG "/tmp.tmp"));
}

static void t_same_event_id_silently_overwrites(void)
{
    // DAC TINH HIEN TAI (khong phai loi cua sd_storage): cung event_id -> anh sau de anh truoc.
    // Vi vay event_id TRUNG se lam mat anh cu - day la ly do phai sua o Node A.
    uint8_t a[] = "first-image", b[] = "SECOND";
    CHECK(sd_storage_save_image(0x77, a, sizeof(a)) == ESP_OK);
    int before = count_files();
    CHECK(sd_storage_save_image(0x77, b, sizeof(b)) == ESP_OK);
    CHECK(count_files() == before);                            // khong tang so luong
    uint8_t *out = NULL; size_t len = 0;
    CHECK(sd_storage_read_image(0x77, &out, &len) == ESP_OK);
    CHECK(len == sizeof(b) && memcmp(out, b, len) == 0);       // anh cu DA MAT
    free(out);
    CHECK(!exists(IMG "/tmp.tmp"));                            // khong de file tam
}

static void t_rotation_evicts_smallest_event_id(void)
{
    // Hien co: ABCDEF01, 00000077. Luu 1..6 -> luon <= MAX anh, ID NHO NHAT bi xoa truoc.
    uint8_t x[] = "x";
    for (uint32_t id = 1; id <= 6; id++) CHECK(sd_storage_save_image(id, x, 1) == ESP_OK);
    CHECK_MSG(count_files() == MAXN, "so anh = %d, ky vong %d", count_files(), MAXN);
    CHECK(!exists(IMG "/00000001.jpg"));
    CHECK(!exists(IMG "/00000002.jpg"));
    CHECK(exists(IMG "/00000006.jpg"));
    CHECK(exists(IMG "/ABCDEF01.jpg"));                        // ID lon -> duoc giu
    INFO("quy tac xoay vong: xoa anh co event_id NHO NHAT (khong phai anh luu som nhat)");
}

// ================= PHAN 2: kich ban reboot Node A (nhieu tien trinh) ================
static bool nvs_load(uint32_t *out, void *ctx)
{ (void)ctx; FILE *f = fopen(NVS_FILE, "rb"); if (!f) return false; size_t n = fread(out, 4, 1, f); fclose(f); return n == 1; }
static bool nvs_store(uint32_t v, void *ctx)
{ (void)ctx; FILE *f = fopen(NVS_FILE, "wb"); if (!f) return false; fwrite(&v, 4, 1, f); fclose(f); return true; }

// Chay o tien trinh con: ./test_sd_storage --session <legacy|persist> <so su kien> <tag>
static int run_session(const char *mode, int n, const char *tag)
{
    if (sd_storage_init() != ESP_OK) return 2;
    bool persist = strcmp(mode, "persist") == 0;
    event_id_gen_t g; uint32_t legacy = 0;                     // legacy = s_event_id_counter = 0 cua Node A hien tai
    if (persist) event_id_init(&g, nvs_load, nvs_store, NULL, EVENT_ID_DEFAULT_RESERVE);
    for (int i = 0; i < n; i++) {
        uint32_t id = persist ? event_id_next(&g) : ++legacy;
        char buf[64]; int len = snprintf(buf, sizeof(buf), "%s:%u", tag, (unsigned)id);
        if (sd_storage_save_image(id, (const uint8_t *)buf, (size_t)len) != ESP_OK) return 3;
        FILE *f = fopen(CHRONO, "a"); fprintf(f, "%s\n", buf); fclose(f);
    }
    return 0;
}

static char self_path[256];
static void spawn(const char *mode, int n, const char *tag)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --session %s %d %s", self_path, mode, n, tag);
    int rc = system(cmd);
    CHECK_MSG(rc == 0, "tien trinh con that bai (rc=%d): %s", rc, cmd);
}

// So sanh tap anh con lai tren the voi "N anh LUU GAN NHAT THEO THOI GIAN" (dung ky vong cua nguoi dung).
static bool survivors_match_expected(void)
{
    char lines[256][64]; int nl = 0;
    FILE *f = fopen(CHRONO, "r");
    while (f && nl < 256 && fgets(lines[nl], sizeof(lines[nl]), f)) { lines[nl][strcspn(lines[nl], "\n")] = 0; nl++; }
    if (f) fclose(f);
    int first = nl > MAXN ? nl - MAXN : 0;

    char have[64][64]; int nh = 0;
    DIR *d = opendir(IMG); struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strlen(e->d_name) != 12 || strcasecmp(e->d_name + 8, ".jpg")) continue;
        char path[128]; snprintf(path, sizeof(path), IMG "/%s", e->d_name);
        FILE *g = fopen(path, "r"); char c[64] = ""; if (g) { if (!fgets(c, sizeof(c), g)) c[0] = 0; fclose(g); }
        if (nh < 64) strcpy(have[nh++], c);
    }
    if (d) closedir(d);

    bool ok = (nh == nl - first);
    for (int i = first; i < nl; i++) {
        bool found = false;
        for (int j = 0; j < nh; j++) if (!strcmp(lines[i], have[j])) found = true;
        if (!found) { ok = false; INFO("   THIEU anh luu gan day: %s", lines[i]); }
    }
    for (int j = 0; j < nh; j++) {
        bool wanted = false;
        for (int i = first; i < nl; i++) if (!strcmp(lines[i], have[j])) wanted = true;
        if (!wanted) INFO("   THUA anh cu van con: %s", have[j]);
    }
    INFO("   ky vong %d anh gan nhat, tren the co %d anh", nl - first, nh);
    return ok;
}

static void t_scn_reboot_collision_legacy(void)
{
    clean_all();
    spawn("legacy", 2, "run1"); spawn("legacy", 2, "run2");    // 2 anh, reboot, 2 anh nua
    bool ok = survivors_match_expected();
    if (!ok) mt_bug_seen = 1;
}
static void t_scn_reboot_collision_fixed(void)
{
    clean_all();
    spawn("persist", 2, "run1"); spawn("persist", 2, "run2");
    CHECK(survivors_match_expected());
}
static void t_scn_rotation_after_reboot_legacy(void)
{
    clean_all();
    spawn("legacy", 14, "run1"); spawn("legacy", 3, "run2");   // the day, reboot, anh moi bi xoa som?
    bool ok = survivors_match_expected();
    if (!ok) mt_bug_seen = 1;
}
static void t_scn_rotation_after_reboot_fixed(void)
{
    clean_all();
    spawn("persist", 14, "run1"); spawn("persist", 3, "run2");
    CHECK(survivors_match_expected());
}
static void t_scn_three_reboots_fixed(void)
{
    clean_all();
    spawn("persist", 4, "run1"); spawn("persist", 4, "run2"); spawn("persist", 4, "run3");
    CHECK(survivors_match_expected());
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "--session")) return run_session(argv[2], atoi(argv[3]), argv[4]);
    snprintf(self_path, sizeof(self_path), "%s", argv[0]);

    clean_all();
    SUITE("sd_storage");
    RUN(t_save_before_init_is_rejected,        "luu anh truoc khi init the -> ESP_ERR_INVALID_STATE");
    RUN(t_init_mounts_and_creates_dir,         "init: gan the, tao thu muc /img, goi lai an toan");
    RUN(t_save_read_roundtrip_and_hex_filename,"luu roi doc lai dung 100%, ten file = 8 ky tu HEX in hoa");
    RUN(t_invalid_args_rejected_no_garbage,    "tham so sai (NULL / 0 byte / >1MB) bi tu choi, khong de file rac");
    RUN(t_same_event_id_silently_overwrites,   "DAC TINH: cung event_id -> ghi de im lang, anh cu mat");
    RUN(t_rotation_evicts_smallest_event_id,   "dat gioi han MAX anh: xoa anh co event_id nho nhat");

    SUITE("reboot_scenarios");
    RUN_REPRO(t_scn_reboot_collision_legacy,        "CACH CU: 2 anh + reboot + 2 anh -> ID trung, mat anh cu");
    RUN(t_scn_reboot_collision_fixed,               "DA SUA: 2 anh + reboot + 2 anh -> giu du 4 anh");
    RUN_REPRO(t_scn_rotation_after_reboot_legacy,   "CACH CU: the day + reboot -> anh MOI bi xoa truoc anh cu");
    RUN(t_scn_rotation_after_reboot_fixed,          "DA SUA: the day + reboot -> xoa dung anh cu nhat");
    RUN(t_scn_three_reboots_fixed,                  "DA SUA: 3 lan reboot lien tiep, luon giu dung anh gan nhat");
    clean_all();
    return mt_exit_code();
}
