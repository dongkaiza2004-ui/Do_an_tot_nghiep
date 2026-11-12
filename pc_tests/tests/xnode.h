// xnode.h - Dung chung cho test_a_espnow (ben gui) va test_b_espnow (ben nhan).
// Ca hai deu SINH CUNG MOT anh JPEG gia tu (event_id, do dai), nen khong can truyen file anh.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t id; uint32_t len; uint16_t w, h; } xn_case_t;
static const xn_case_t XN_CASES[] = {
    {101,      1, 320, 240},   // 1 byte  -> 1 manh
    {102,    229, 320, 240},   // sat bien 230
    {103,    230, 320, 240},   // dung 1 manh day
    {104,    231, 320, 240},   // 2 manh, manh cuoi 1 byte
    {105,    460, 640, 480},   // dung 2 manh
    {106,   4097, 640, 480},   // le
    {107,  30011, 640, 480},   // anh JPEG kich thuoc thuc te
    {108, 150000, 800, 600},   // anh lon (652 manh)
};
#define XN_NCASES (sizeof(XN_CASES) / sizeof(XN_CASES[0]))
#define XN_FILE "a_frags.bin"   // A ghi, B doc: [uint16 do dai][byte...] lap lai

static inline void xn_jpeg(uint32_t id, uint32_t len, uint8_t *out)
{
    uint32_t x = id * 2654435761u + 12345u;
    for (uint32_t i = 0; i < len; i++) { x = x * 1664525u + 1013904223u; out[i] = (uint8_t)(x >> 24); }
    if (len >= 2) { out[0] = 0xFF; out[1] = 0xD8; out[len - 2] = 0xFF; out[len - 1] = 0xD9; }
}
