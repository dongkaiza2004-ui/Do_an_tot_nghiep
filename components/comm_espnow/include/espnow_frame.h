// espnow_frame.h
// Giao thuc chia goi (fragment) de gui 1 frame anh (da nen JPEG) qua ESP-NOW.
// Node A: render bitmap (co overlay box/ten/FPS/huong dan enroll) -> nen JPEG -> chia fragment -> gui.
// Node B: nhan fragment -> rap lai theo frame_id -> giai nen JPEG -> ve len TFT bang LVGL image widget.
//
// Ly do nen JPEG truoc khi gui: bitmap RGB565 128x160 tho la 40960 byte/frame,
// can toi ~171 goi ESP-NOW (gioi han ~240 byte payload/goi) -> qua nang, de mat goi/tre hinh.
// JPEG chat luong vua thuong con ~3-8KB/frame -> chi con ~15-35 goi/frame, kha thi hon nhieu.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Kich thuoc anh co dinh (khop TFT 128x160 ca 2 node)
#define STREAM_IMG_WIDTH   128
#define STREAM_IMG_HEIGHT  160

// ESP-NOW gioi han khoang 250 byte/goi -> tru header con lai cho du lieu anh
#define ESPNOW_FRAGMENT_MAX_PAYLOAD  240

// Moi fragment la 1 phan cua chuoi byte JPEG da nen (kich thuoc JPEG thay doi
// tung frame nen fragment_total duoc TINH DONG luc gui, khong phai hang so).
typedef struct __attribute__((packed)) {
    uint16_t frame_id;                          // tang dan moi frame camera moi (wrap-around uint16 la on)
    uint16_t fragment_index;                    // 0-based, thu tu manh trong frame nay
    uint16_t fragment_total;                    // tong so manh CUA FRAME NAY (tinh = ceil(jpeg_size / MAX_PAYLOAD))
    uint16_t payload_len;                       // so byte thuc trong payload (manh cuoi thuong < MAX_PAYLOAD)
    uint8_t  payload[ESPNOW_FRAGMENT_MAX_PAYLOAD];
} espnow_frame_fragment_t;

// ---------------------------------------------------------------------------
// Goi ý logic ghep frame o Node B (khong bat buoc dung struct nay, chi la vi du):
// ---------------------------------------------------------------------------
// - Giu 1 buffer tam ~16-20KB (du cho JPEG lon nhat du kien) + bien current_frame_id.
// - Khi nhan fragment co frame_id KHAC current_frame_id -> coi la frame moi,
//   reset buffer, cap nhat current_frame_id, bat dau ghi tu fragment_index = 0.
// - Ghi payload vao buffer tai vi tri (fragment_index * ESPNOW_FRAGMENT_MAX_PAYLOAD).
// - Dem so fragment da nhan; khi du fragment_total (hoac nhan duoc fragment_index ==
//   fragment_total - 1) -> coi frame da day du -> goi ham giai nen JPEG -> ve len TFT.
// - Neu qua thoi gian timeout (vd 200ms) ma chua du fragment -> HUY frame do, cho frame_id tiep theo
//   (tranh hien anh bi vo/nham do mat goi ESP-NOW - von khong dam bao thu tu/khong mat goi).
//
// Luu y quan trong: ESP-NOW co the gui khong theo dung thu tu hoac mat goi giua chung,
// nen Node B PHAI tu xu ly mat goi bang co che timeout/bo frame loi nhu tren,
// khong duoc gia dinh cac fragment luon den du va dung thu tu.

#ifdef __cplusplus
}
#endif
