// servo_door.h
//
// Dieu khien servo (PWM 50Hz qua LEDC) de co cau chap hanh mo/dong cua.
// Servo quay den goc "mo" khi BoltLockManager bat dau Unlock va quay ve goc
// "dong" khi bat dau Lock. Sau khi quay xong, PWM duoc TAT de servo khong
// rung/nong va khong ton dien khi dung yen.

#pragma once

#include <esp_err.h>
#include <driver/gpio.h>

// ---- CAU HINH (sua cho khop co cau thuc te) --------------------------------
constexpr gpio_num_t kServoPin          = GPIO_NUM_5;
constexpr int        kServoClosedAngle  = 0;    // goc khi KHOA / cua dong (do)
constexpr int        kServoOpenAngle    = 90;   // goc khi MO KHOA (do)
constexpr int        kServoMinPulseUs   = 500;  // SG90/MG90: ~500us = 0 do
constexpr int        kServoMaxPulseUs   = 2400; // SG90/MG90: ~2400us = 180 do
constexpr int        kServoHoldMs       = 1000; // giu PWM bao lau roi tat
// ---------------------------------------------------------------------------

// Khoi tao LEDC va dua servo ve goc "dong". Goi 1 lan luc khoi dong.
esp_err_t servo_door_init(void);

// open == true  -> quay den kServoOpenAngle
// open == false -> quay ve kServoClosedAngle
// An toan de goi tu Matter thread.
void servo_door_set_open(bool open);
