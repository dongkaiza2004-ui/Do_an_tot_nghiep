// access_gateway.h
//
// "Bo nao" quyet dinh mo khoa dua tren ket qua nhan dien tu camera.
//
// Dat trong main/ (KHONG phai component rieng trong components/) vi ly do
// bat buoc ve mat ky thuat: file .cpp cua no can include truc tiep
// "bolt_lock_manager.h" va "door_lock_storage.h" - hai header nay chi duoc
// khai bao trong PRIV_INCLUDE_DIRS (rieng tu) cua component "main"
// (xem main/CMakeLists.txt), nen KHONG the include tu mot component khac dat
// trong components/. Day la quy dinh cua ESP-IDF build system, khong phai
// lua chon to chuc tuy y.
//
// access_gateway la noi DUY NHAT (ngoai app_driver.cpp va door_lock_callbacks.cpp
// da co san) duoc phep goi BoltLockManager::Instance().Lock()/Unlock(), va no
// luon lam viec do thong qua PlatformMgr().ScheduleWork(...) - dung mau da
// dung trong app_driver_button_toggle_cb - de tranh dam vao Matter event loop
// tu mot task khac (task doc UART).

#pragma once

#include <esp_err.h>

// Khoi tao access_gateway: dang ky lam listener cua comm_uart cho cac phan
// hoi RSP_FACE_MATCHED / RSP_FACE_UNKNOWN / RSP_LIVENESS_FAIL.
//
// Goi ham nay SAU comm_uart_init() nhung TRUOC comm_uart_start(), de dam bao
// khong bo lot khung dau tien nhan duoc.
esp_err_t access_gateway_init();
