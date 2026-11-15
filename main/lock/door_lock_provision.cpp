// Dang ky / go nguoi dung (PIN) tren khoa Matter theo yeu cau cua dashboard.
// Dung CHINH cac loi goi DoorLockStorage nhu lenh console `add-pin` (door_lock_console.cpp).
//
// Quy uoc (giong console): o luu PIN (credential index) = chi so nguoi dung (user index).
// Gop y: PIN KHONG bao gio duoc ghi ra log.

#include "door_lock_provision.h"

#include "door_lock_capabilities.h"
#include "door_lock_name_util.h"
#include "door_lock_storage.h"

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <esp_log.h>
#include <esp_matter.h>

#include <cstring>

using namespace chip;
using namespace chip::app::Clusters::DoorLock;

namespace {

const char *TAG = "door_lock_provision";

// Do dai PIN (chu so 0-9) chap nhan khi dang ky tu dashboard.
// Chon 6..8 (mac dinh pho bien cua cluster Door Lock). Neu ban doi MinPINCodeLength/MaxPINCodeLength thi doi o day.
constexpr uint8_t kPinMinLen = 6;
constexpr uint8_t kPinMaxLen = 8;

// Cac ham *Locked ben duoi YEU CAU dang giu khoa ngan xep CHIP (ScopedChipStackLock).
bool UserOccupiedLocked(uint16_t userIndex)
{
    EmberAfPluginDoorLockUserInfo user;
    return DoorLockStorage::Instance().GetUser(userIndex, user) && user.userStatus != UserStatusEnum::kAvailable;
}

bool CredentialOccupiedLocked(uint16_t credentialIndex)
{
    EmberAfPluginDoorLockCredentialInfo credential;
    return DoorLockStorage::Instance().GetCredential(credentialIndex, CredentialTypeEnum::kPin, credential) &&
           credential.status != DlCredentialStatus::kAvailable;
}

bool PinMatchesLocked(const char *pin)
{
    const ByteSpan pinSpan(reinterpret_cast<const uint8_t *>(pin), std::strlen(pin));
    DoorLockStorage::PinMatch match;
    return DoorLockStorage::Instance().ValidatePIN(pinSpan, match);
}

bool SlotInUse(uint16_t userIndex)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    return UserOccupiedLocked(userIndex) || CredentialOccupiedLocked(userIndex);
}

bool PinInUse(const char *pin)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    return PinMatchesLocked(pin);
}

access_user_prov_result_t RegisterUser(uint16_t userIndex, const char *name, const char *pin)
{
    const size_t pinLen = std::strlen(pin);
    if (userIndex < 1 || userIndex > DoorLockCapabilities::kUsers || pinLen < kPinMinLen || pinLen > kPinMaxLen) {
        return ACCESS_USER_PROV_ERR_STORAGE;
    }

    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    DoorLockStorage &storage = DoorLockStorage::Instance();

    // KHONG ghi de nguoi dung / PIN dang co (vd tao tu ung dung Matter)
    if (UserOccupiedLocked(userIndex)) {
        return ACCESS_USER_PROV_ERR_EXISTS;
    }
    if (CredentialOccupiedLocked(userIndex)) {
        return ACCESS_USER_PROV_ERR_CREDENTIAL_SLOT_BUSY;
    }
    if (PinMatchesLocked(pin)) {
        return ACCESS_USER_PROV_ERR_PIN_IN_USE;
    }

    const uint16_t credentialIndex = userIndex;
    const ByteSpan pinSpan(reinterpret_cast<const uint8_t *>(pin), pinLen);
    if (!storage.SetCredential(credentialIndex, chip::kUndefinedFabricIndex, chip::kUndefinedFabricIndex,
                               DlCredentialStatus::kOccupied, CredentialTypeEnum::kPin, pinSpan)) {
        ESP_LOGE(TAG, "SetCredential that bai (user %u)", (unsigned)userIndex);
        return ACCESS_USER_PROV_ERR_STORAGE;
    }

    CredentialStruct credentials[1] = {
        { .credentialType = CredentialTypeEnum::kPin, .credentialIndex = credentialIndex }
    };
    // Ten Matter toi da DOOR_LOCK_MAX_USER_NAME_SIZE byte; cat dung ranh gioi UTF-8
    char nameBuf[DOOR_LOCK_MAX_USER_NAME_SIZE + 1] = {};
    const size_t nameLen = utf8_truncate_len(name, DOOR_LOCK_MAX_USER_NAME_SIZE);
    std::memcpy(nameBuf, name, nameLen);

    if (!storage.SetUser(userIndex, chip::kUndefinedFabricIndex, chip::kUndefinedFabricIndex,
                         CharSpan(nameBuf, nameLen), 0, UserStatusEnum::kOccupiedEnabled,
                         UserTypeEnum::kUnrestrictedUser, CredentialRuleEnum::kSingle, credentials, 1)) {
        ESP_LOGE(TAG, "SetUser that bai (user %u), hoan tac PIN", (unsigned)userIndex);
        // Hoan tac: xoa PIN vua ghi de khong de lai PIN mo coi
        storage.SetCredential(credentialIndex, chip::kUndefinedFabricIndex, chip::kUndefinedFabricIndex,
                              DlCredentialStatus::kAvailable, CredentialTypeEnum::kPin, ByteSpan());
        return ACCESS_USER_PROV_ERR_STORAGE;
    }

    ESP_LOGI(TAG, "Da dang ky nguoi dung %u tren khoa (PIN %u chu so)", (unsigned)userIndex, (unsigned)pinLen);
    return ACCESS_USER_PROV_OK;
}

bool UnregisterUser(uint16_t userIndex)
{
    if (userIndex < 1 || userIndex > DoorLockCapabilities::kUsers) {
        return false;
    }
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    DoorLockStorage &storage = DoorLockStorage::Instance();
    bool ok = true;

    // Xoa PIN TRUOC (cat quyen mo bang ban phim ngay), roi moi xoa nguoi dung
    if (CredentialOccupiedLocked(userIndex)) {
        ok = storage.SetCredential(userIndex, chip::kUndefinedFabricIndex, chip::kUndefinedFabricIndex,
                                   DlCredentialStatus::kAvailable, CredentialTypeEnum::kPin, ByteSpan());
    }
    if (UserOccupiedLocked(userIndex)) {
        ok = storage.SetUser(userIndex, chip::kUndefinedFabricIndex, chip::kUndefinedFabricIndex, CharSpan(), 0,
                             UserStatusEnum::kAvailable, UserTypeEnum::kUnrestrictedUser, CredentialRuleEnum::kSingle,
                             nullptr, 0) && ok;
    }
    if (ok) {
        ESP_LOGI(TAG, "Da go nguoi dung %u khoi khoa", (unsigned)userIndex);
    } else {
        ESP_LOGE(TAG, "Go nguoi dung %u khong hoan toan thanh cong", (unsigned)userIndex);
    }
    return ok;
}

} // namespace

extern "C" const access_user_provisioner_t *door_lock_dashboard_provisioner(void)
{
    static access_user_provisioner_t provisioner;
    provisioner.max_user_index = DoorLockCapabilities::kUsers;
    provisioner.pin_min_len = kPinMinLen;
    provisioner.pin_max_len = kPinMaxLen;
    provisioner.user_exists = SlotInUse;
    provisioner.pin_in_use = PinInUse;
    provisioner.register_user = RegisterUser;
    provisioner.unregister_user = UnregisterUser;
    return &provisioner;
}
