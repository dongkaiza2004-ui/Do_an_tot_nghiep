// test_a_appstate.c - Test app_state.c THAT cua NODE A: luu/nap cau hinh qua NVS (NVS gia trong RAM).
// "Khoi dong lai" = goi lai app_state_init(): code doc lai NVS va ghi de bien trong RAM (giong luc boot).
#include "mini_test.h"
#include "app_state.h"
#include <math.h>
#include <string.h>

static void t_first_boot_defaults(void)
{
    stub_nvs_reset();
    CHECK(app_state_init() == ESP_OK);
    CHECK(app_state_get_model() == RECOG_MODEL_MFN_S8_V1);
    CHECK_MSG(fabsf(app_state_get_threshold() - 0.55f) < 1e-6f, "nguong mac dinh = %f", app_state_get_threshold());
    CHECK(app_state_get_brightness() == 0 && app_state_get_contrast() == 0);
    CHECK(app_state_get_flip() && app_state_get_mirror());
    CHECK(app_state_get_streaming());
    INFO("mac dinh lan dau: model=%d nguong=%.3f flip=%d mirror=%d", app_state_get_model(), app_state_get_threshold(), app_state_get_flip(), app_state_get_mirror());
}

static void t_save_and_reload_all_persisted_fields(void)
{
    stub_nvs_reset(); app_state_init();
    app_state_set_model(RECOG_MODEL_MBF_S8_V1);
    app_state_set_threshold(0.62f);
    app_state_set_cam_param(-2, 2, false, false);
    app_state_set_light_threshold(37);
    app_state_save_to_nvs();
    // lam hong gia tri trong RAM, roi "boot lai"
    app_state_set_model(RECOG_MODEL_MFN_S8_V1); app_state_set_threshold(0.1f); app_state_set_cam_param(1, 1, true, true); app_state_set_light_threshold(99);
    CHECK(app_state_init() == ESP_OK);
    CHECK(app_state_get_model() == RECOG_MODEL_MBF_S8_V1);
    CHECK(fabsf(app_state_get_threshold() - 0.62f) < 0.0015f);
    CHECK(app_state_get_brightness() == -2 && app_state_get_contrast() == 2);
    CHECK(!app_state_get_flip() && !app_state_get_mirror());
    CHECK(app_state_get_light_threshold() == 37);
}

static void t_name_truncated_and_enrol_state(void)
{
    stub_nvs_reset(); app_state_init();
    app_state_begin_enroll(7, "TenRatDaiVuotQuaHaiMuoiBaKyTu_XXXXXXXX");
    enroll_state_t st = app_state_get_enroll_state();
    CHECK(app_state_is_enrolling() && st.in_progress && st.user_id == 7);
    CHECK_MSG(strlen(st.user_name) == 23 && st.user_name[23] == 0, "ten bi cat con %zu ky tu", strlen(st.user_name));
    app_state_cancel_enroll();
    CHECK(!app_state_is_enrolling());
}

static void t_threshold_roundtrip_error_small(void)
{
    // Moi gia tri 0.00..1.00 (buoc 0.01): luu roi nap lai KHONG lech qua 0.002.
    double worst = 0; float worst_t = 0;
    for (int i = 0; i <= 100; i++) {
        float t = (float)i / 100.0f;
        stub_nvs_reset(); app_state_init();
        app_state_set_threshold(t); app_state_save_to_nvs(); app_state_init();
        double err = fabs((double)app_state_get_threshold() - (double)t);
        if (err > worst) { worst = err; worst_t = t; }
    }
    INFO("sai so 1 lan luu/nap lon nhat: %.4f (tai nguong %.2f)", worst, worst_t);
    CHECK(worst < 0.002);
}

static void t_threshold_does_not_drift_over_200_reboots(void)
{
    // Giai thuyet da kiem chung: (int32)(nguong*1000) co the CAT va lam nguong tut dan? -> KHONG (float lam tron dung).
    double worst = 0; float worst_t = 0;
    for (int i = 0; i <= 100; i++) {
        float t = (float)i / 100.0f;
        stub_nvs_reset(); app_state_init();
        app_state_set_threshold(t);
        for (int boot = 0; boot < 200; boot++) { app_state_save_to_nvs(); app_state_init(); }
        double drift = fabs((double)t - (double)app_state_get_threshold());
        if (drift > worst) { worst = drift; worst_t = t; }
    }
    INFO("101 gia tri nguong x 200 lan reboot: lech lon nhat %.4f (tai %.2f)", worst, worst_t);
    CHECK(worst < 0.002);
}

static void t_limit_garbage_model_in_nvs_not_validated(void)
{
    stub_nvs_reset(); app_state_init();
    nvs_handle_t h; nvs_open("app_state", NVS_READWRITE, &h); nvs_set_u8(h, "model", 7); nvs_commit(h); nvs_close(h);
    app_state_init();
    int m = (int)app_state_get_model();
    INFO("NVS chua model=7 (hong flash/phien ban cu) -> app_state_get_model() = %d (chi hop le 0..1)", m);
    if (m != 0 && m != 1) mt_bug_seen = 1;
}

static void t_limit_liveness_not_persisted(void)
{
    stub_nvs_reset(); app_state_init();
    app_state_set_liveness_enabled(true);
    app_state_save_to_nvs();
    nvs_handle_t h; uint8_t v;
    bool found = false;
    if (nvs_open("app_state", NVS_READONLY, &h) == ESP_OK) {
        const char *keys[] = {"liveness", "liveness_en", "live", "liveness_enabled", "liveness_on"};
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) if (nvs_get_u8(h, keys[i], &v) == ESP_OK) found = true;
        nvs_close(h);
    }
    INFO("bat liveness roi luu NVS: key liveness %s trong NVS -> sau mat dien Node A %s",
         found ? "CO" : "KHONG co", found ? "giu nguyen" : "quay ve TAT. (Khi viet test: Node B chi gui lenh nay khi nguoi dung bam dashboard /api/liveness, khong gui lai luc khoi dong)");
    if (!found) mt_bug_seen = 1;
}

static void t_missing_namespace_is_not_fatal(void)
{
    stub_nvs_reset();
    int w0 = stub_log_warn;
    CHECK(app_state_init() == ESP_OK);
    INFO("NVS trong (lan dau): %d canh bao, init van tra ESP_OK", stub_log_warn - w0);
    // (Gia tri mac dinh da duoc kiem o t_first_boot_defaults; trong 1 tien trinh bien RAM giu gia tri cu nen khong kiem lai o day)
}

int main(void)
{
    SUITE("node_a_app_state");
    RUN(t_first_boot_defaults,                       "NVS trong (lan dau): gia tri mac dinh dung");
    RUN(t_save_and_reload_all_persisted_fields,      "luu NVS -> boot lai: model, nguong, camera, flip/mirror, nguong sang giu dung");
    RUN(t_name_truncated_and_enrol_state,            "ten nguoi dung dai bi cat 23 ky tu + NUL; trang thai dang them user");
    RUN(t_threshold_roundtrip_error_small,           "nguong 0.00..1.00: 1 lan luu/nap lech < 0.002");
    RUN(t_missing_namespace_is_not_fatal,     "khong co namespace NVS: init van tra ESP_OK, khong crash");
    RUN(t_threshold_does_not_drift_over_200_reboots, "nguong KHONG tut/troi sau 200 lan luu-nap (101 gia tri)");
    RUN_REPRO(t_limit_garbage_model_in_nvs_not_validated, "HAN CHE: NVS chua model=7 van duoc nap, khong kiem tra mien gia tri");
    RUN_REPRO(t_limit_liveness_not_persisted,        "HAN CHE: liveness_enabled khong duoc luu NVS -> mat sau khi mat dien");
    return mt_exit_code();
}
