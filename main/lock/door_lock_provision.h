#pragma once

#include "access_dashboard.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bo dang ky nguoi dung tren khoa Matter (DoorLockStorage) de dashboard goi sau khi Node A bao
// dang ky khuon mat thanh cong. Dung: access_dashboard_set_user_provisioner(door_lock_dashboard_provisioner());
const access_user_provisioner_t *door_lock_dashboard_provisioner(void);

#ifdef __cplusplus
}
#endif
