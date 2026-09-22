// access_gateway.cpp

#include "access_gateway.h"

#include "comm_uart.h"
#include "uart_protocol.h"
#include "access_events.h"

#include "bolt_lock_manager.h"
#include "door_lock_storage.h"

#include <esp_log.h>
#include <platform/PlatformManager.h>

#include <cstring>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters::DoorLock;

namespace {

constexpr const char *TAG = "access_gateway";

// So lan lien tiep "unknown" hoac "liveness fail" truoc khi ghi log canh bao
// muc cao hon (co nghi van co nguoi la co y do truoc cua).
constexpr uint8_t kRepeatedFailureAlertThreshold = 3;

uint8_t s_consecutive_unknown = 0;
uint8_t s_consecutive_liveness_fail = 0;

// --------------------------------------------------------------------
// Anh xa user_id cua bo nhan dien khuon mat (camera) sang userIndex cua
// Matter Door Lock cluster.
//
// !!! GIA DINH TAM THOI, CAN XAC NHAN LAI TRUOC KHI DUNG THAT !!!
// Tai lieu thiet ke goc (muc "Dinh huong phat trien") con de ngo phan nay:
// "Xay dung phuong an anh xa cu the giua chi so nguoi dung/thong tin xac
// thuc tren Matter Cluster voi ma nguoi dung noi bo". O day dang dung anh
// xa 1-1 don gian nhat (face user_id == Matter userIndex, gioi han
// [1, kMaxUsers]). Neu quy uoc cap phat user_id ben camera KHONG di theo
// khoang nay (vi du bat dau tu 0, hoac danh so tu do khong lien quan gi den
// thu tu dang ky tren Matter), phai thay ham nay bang mot bang anh xa that
// (vi du luu trong NVS: face_user_id -> matter_user_index).
// --------------------------------------------------------------------
bool MapFaceUserIdToMatterIndex(uint16_t faceUserId, uint16_t &matterUserIndex)
{
    if (faceUserId < 1 || faceUserId > DoorLockStorage::kMaxUsers) {
        return false;
    }
    matterUserIndex = faceUserId;
    return true;
}

struct UnlockRequestContext {
    uint16_t matterUserIndex;
};

void DoScheduledUnlock(intptr_t arg)
{
    auto *ctx = reinterpret_cast<UnlockRequestContext *>(arg);

    // OperationSourceEnum::kBiometric la gia tri chuan cua Matter Door Lock
    // cluster cho "mo khoa bang sinh trac hoc". NEU BAN BUILD BI LOI thieu
    // identifier nay (phu thuoc phien ban SDK/connectedhomeip dang dung), hay
    // thay tam bang OperationSourceEnum::kRemote (da duoc dung san trong
    // door_lock_callbacks.cpp nen chac chan ton tai trong SDK cua ban).
    BoltLockManager::Instance().Unlock(OperationSourceEnum::kBiometric);

    delete ctx;
}

void PublishEvent(uint32_t eventId, access_event_type_t type, uint16_t faceUserId,
                   uint16_t matterUserIndex, float similarity)
{
    access_event_t evt = {};
    evt.event_id = eventId;
    evt.type = type;
    evt.face_user_id = faceUserId;
    evt.matter_user_index = matterUserIndex;
    evt.similarity = similarity;
    access_events_publish(&evt);
}

void HandleFaceMatched(const payload_face_matched_t &payload)
{
    s_consecutive_unknown = 0;
    s_consecutive_liveness_fail = 0;

    uint16_t matterUserIndex = 0;
    bool mapped = MapFaceUserIdToMatterIndex(payload.user_id, matterUserIndex);

    if (!mapped) {
        ESP_LOGW(TAG, "event_id=%u: user_id=%u khong anh xa duoc sang Matter userIndex hop le",
                 (unsigned)payload.event_id, payload.user_id);
        PublishEvent(payload.event_id, ACCESS_EVENT_DENIED, payload.user_id, 0, payload.similarity);
        return;
    }

    EmberAfPluginDoorLockUserInfo userInfo;
    bool found = DoorLockStorage::Instance().GetUser(matterUserIndex, userInfo);

    if (!found || userInfo.userStatus != UserStatusEnum::kOccupiedEnabled) {
        ESP_LOGW(TAG,
                 "event_id=%u: user_id=%u (Matter index %u) khong hop le hoac da bi vo hieu hoa, tu choi mo khoa",
                 (unsigned)payload.event_id, payload.user_id, matterUserIndex);
        PublishEvent(payload.event_id, ACCESS_EVENT_DENIED, payload.user_id, matterUserIndex, payload.similarity);
        return;
    }

    // TODO: neu can gioi han theo khung gio truy cap rieng cho tung nguoi
    // dung, kiem tra them o day truoc khi cho mo khoa. DoorLockStorage hien
    // tai chua co truong luu khung gio - can bo sung neu yeu cau nay duoc dat ra.

    ESP_LOGI(TAG, "event_id=%u: user_id=%u (Matter index %u) hop le, do tin cay=%.3f -> mo khoa",
             (unsigned)payload.event_id, payload.user_id, matterUserIndex, (double)payload.similarity);

    PublishEvent(payload.event_id, ACCESS_EVENT_GRANTED, payload.user_id, matterUserIndex, payload.similarity);

    auto *ctx = new UnlockRequestContext{ matterUserIndex };
    CHIP_ERROR err = PlatformMgr().ScheduleWork(DoScheduledUnlock, reinterpret_cast<intptr_t>(ctx));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Khong the len lich mo khoa: %" CHIP_ERROR_FORMAT, err.Format());
        delete ctx;
    }
}

void HandleFaceUnknown(const payload_face_event_id_t &payload)
{
    s_consecutive_liveness_fail = 0;
    s_consecutive_unknown++;

    ESP_LOGW(TAG, "event_id=%u: khong nhan dien duoc khuon mat (lien tiep lan %u)",
             (unsigned)payload.event_id, s_consecutive_unknown);

    PublishEvent(payload.event_id, ACCESS_EVENT_UNKNOWN_FACE, 0, 0, 0.0f);

    if (s_consecutive_unknown >= kRepeatedFailureAlertThreshold) {
        ESP_LOGE(TAG, "CANH BAO: %u lan lien tiep khong nhan dien duoc - co the co nguoi la truoc cua",
                 s_consecutive_unknown);
        // TODO: phat sinh canh bao qua Matter (vd mot Door Lock alarm event)
        // va/hoac day thong tin nay len access_dashboard neu can hien thi
        // canh bao rieng ngoai lich su thong thuong.
    }
}

void HandleLivenessFail(const payload_face_event_id_t &payload)
{
    s_consecutive_unknown = 0;
    s_consecutive_liveness_fail++;

    ESP_LOGW(TAG, "event_id=%u: nghi ngo gia mao (lien tiep lan %u)",
             (unsigned)payload.event_id, s_consecutive_liveness_fail);

    PublishEvent(payload.event_id, ACCESS_EVENT_LIVENESS_FAIL, 0, 0, 0.0f);

    if (s_consecutive_liveness_fail >= kRepeatedFailureAlertThreshold) {
        ESP_LOGE(TAG, "CANH BAO: %u lan lien tiep nghi ngo gia mao", s_consecutive_liveness_fail);
        // TODO: giong nhu tren.
    }
}

void OnUartResponse(uint8_t rsp_cmd, const uint8_t *payload, uint8_t len, void * /*ctx*/)
{
    switch (rsp_cmd) {
    case RSP_FACE_MATCHED: {
        if (len < sizeof(payload_face_matched_t)) {
            ESP_LOGW(TAG, "RSP_FACE_MATCHED thieu du lieu (%u byte)", len);
            return;
        }
        payload_face_matched_t p;
        memcpy(&p, payload, sizeof(p));
        HandleFaceMatched(p);
        break;
    }

    case RSP_FACE_UNKNOWN: {
        if (len < sizeof(payload_face_event_id_t)) {
            ESP_LOGW(TAG, "RSP_FACE_UNKNOWN thieu du lieu (%u byte)", len);
            return;
        }
        payload_face_event_id_t p;
        memcpy(&p, payload, sizeof(p));
        HandleFaceUnknown(p);
        break;
    }

    case RSP_LIVENESS_FAIL: {
        if (len < sizeof(payload_face_event_id_t)) {
            ESP_LOGW(TAG, "RSP_LIVENESS_FAIL thieu du lieu (%u byte)", len);
            return;
        }
        payload_face_event_id_t p;
        memcpy(&p, payload, sizeof(p));
        HandleLivenessFail(p);
        break;
    }

    default:
        // Cac RSP_* khac (ACK/NACK/USER_LIST/FPS_REPORT/...) khong lien quan
        // toi quyet dinh mo khoa. access_dashboard tu dang ky listener rieng
        // cho cac lenh do khi can (xem access_dashboard_http.c).
        break;
    }
}

} // namespace

esp_err_t access_gateway_init()
{
    esp_err_t err = access_events_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "access_events_init that bai: %d", err);
        return err;
    }

    comm_uart_listener_handle_t handle = comm_uart_register_listener(OnUartResponse, nullptr);
    if (handle < 0) {
        ESP_LOGE(TAG, "Khong dang ky duoc listener UART cho access_gateway");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "access_gateway init OK");
    return ESP_OK;
}
