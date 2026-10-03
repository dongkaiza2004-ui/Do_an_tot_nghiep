// door_sensor.h
//
// Doc trang thai cua vat ly qua cam bien tu (reed switch) va ap dat chinh
// sach: BOLT CHI DUOC PHEP KHOA (Lock) KHI CUA DANG DONG. Neu co yeu cau khoa
// trong luc cua dang mo (tu app, tu auto-relock, tu nut bam...), yeu cau do
// se duoc HOAN LAI va tu dong thuc hien ngay khi cam bien bao cua vua dong
// lai - khong bao gio khoa "vao khoang khong" khi cua con dang mo.
//
// Mo khoa (Unlock) KHONG bi anh huong boi module nay - khuon mat va app van
// mo khoa duoc binh thuong bat ky luc nao.
//
// ------------------------------------------------------------------------
// VE AUTO-RELOCK: khong dung thuoc tinh AutoRelockTime tich hop san cua
// Matter SDK nua (da bo dong cluster::door_lock::attribute::create_auto_relock_time
// trong app_main.cpp). Ly do: co che do co 2 van de khi ket hop voi cam bien cua:
//   1. Callback emberAfPluginDoorLockOnAutoRelock() co kieu tra ve "void" -
//      SDK tu dat thuoc tinh LockState=Locked TRUOC KHI goi callback, bat
//      ke thuc te co khoa duoc hay khong (khong co cach "tu choi" tu callback
//      void). Neu cua dang mo, thuoc tinh se bao "Da khoa" sai su that.
//   2. Neu code tu sua lai thuoc tinh ve Unlocked de sua loi (1), chinh hanh
//      dong "dat lai thanh Unlocked" do lai bi SDK hieu nham la "vua moi mo
//      khoa" va TU DONG bat lai dong ho auto-relock 5 giay mot lan nua -> lap
//      vo han, spam thong bao lien tuc tren app.
//
// Giai phap: tu quan ly toan bo auto-relock trong module nay
// (door_sensor_arm_auto_relock/door_sensor_cancel_auto_relock), khong dung
// thuoc tinh AutoRelockTime cua SDK nua. HandleBoltStateChange() trong
// door_lock_callbacks.cpp se goi arm() ngay sau khi mo khoa xong, va cancel()
// ngay khi khoa xong (bat ke khoa do nguyen nhan gi).
// ------------------------------------------------------------------------
//
// Dat trong main/ (khong phai component rieng) vi can goi truc tiep
// BoltLockManager::Instance().Lock() va dung chip::DeviceLayer::SystemLayer()
// - giong ly do access_gateway cung nam trong main/.

#pragma once

#include <cstdint>
#include <esp_err.h>
#include <driver/gpio.h>
#include "bolt_lock_manager.h"

// Thoi gian tu khoa lai sau khi mo khoa thanh cong, neu khong co ai chu dong
// khoa truoc do. Doi so nay truc tiep trong code (khong con qua thuoc tinh
// Matter AutoRelockTime nua - xem giai thich o tren).
constexpr uint32_t kAutoRelockDelayMs = 5000;

// Khoi tao GPIO doc cam bien cua. PHAI goi truoc door_sensor_start().
//
// GIA DINH VE CACH DAU DAY (kiem tra lai voi cam bien thuc te ban dung):
//   - Cua DONG (nam cham ap sat) -> GPIO muc THAP (0).
//   - Cua MO (nam cham roi xa) -> GPIO muc CAO (1), nho pull-up noi bo.
//   Neu day nguoc lai, sua bieu thuc "raw == 0" trong door_sensor.cpp thanh
//   "raw != 0".
esp_err_t door_sensor_init(gpio_num_t pin);

// Bat task doc + debounce GPIO chay nen.
void door_sensor_start();

// Trang thai cua hien tai, DA QUA debounce.
bool door_sensor_is_closed();

// Diem DUY NHAT nen dung de "khoa cua" thay vi goi thang
// BoltLockManager::Instance().Lock(source) tu moi noi trong code.
//
// Tra ve true neu da khoa NGAY (cua dang dong), false neu hoan lai (cua dang
// mo - se tu dong khoa sau khi cam bien bao dong).
bool door_sensor_request_lock_when_closed(BoltLockManager::OperationSource source);

// Dat lich tu-khoa sau delayMs mili giay, dung "source" khi thuc su khoa
// (hoac khi phai hoan lai do cua con mo - xem door_sensor_request_lock_when_closed).
// Goi ham nay ngay khi mo khoa xong (state == kUnlockingCompleted).
// Goi lai nhieu lan se HUY lich cu va dat lich moi (khong cong don).
void door_sensor_arm_auto_relock(uint32_t delayMs, BoltLockManager::OperationSource source);

// Huy lich tu-khoa dang cho (neu co). Goi khi cua da duoc khoa boi bat ky
// nguyen nhan nao khac, de tranh mot lan khoa "thua" khong can thiet sau do
// (vo hai vi BoltLockManager::Lock() tu bo qua neu da khoa roi, nhung huy cho
// sach va de doc log).
void door_sensor_cancel_auto_relock();