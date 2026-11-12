// event_id.h - Bo cap event_id TANG DAN, KHONG TRUNG sau khi khoi dong lai.
//
// Dat file nay vao:  components/face_engine/include/event_id.h   (nhanh nodeA)
//
// Y tuong: khong luu moi event (mon flash), ma luu truoc 1 "moc tran" (limit).
// Moi ID cap ra luon <= limit da luu. Khoi dong lai thi counter bat dau TU limit
// -> moi ID moi deu lon hon moi ID cu, ke ca khi mat dien dot ngot (co the bo
// qua vai ID chua dung, khong sao). Chi ghi flash moi `reserve` su kien.
//
// File nay KHONG phu thuoc ESP-IDF: luu/doc di qua con tro ham, nen bien dich
// va test duoc tren PC. Tren chip, noi vao NVS (xem huong dan trong README).
// Khong an toan da luong: chi goi tu 1 task, hoac tu bao ve bang mutex.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_ID_DEFAULT_RESERVE 32u

// Doc moc da luu. Tra false neu chua co (lan khoi dong dau tien) -> bat dau tu 0.
typedef bool (*event_id_load_fn)(uint32_t *out, void *ctx);
// Luu moc moi. Tra false neu ghi that bai.
typedef bool (*event_id_store_fn)(uint32_t value, void *ctx);

typedef struct {
    uint32_t counter;        // ID da cap gan nhat
    uint32_t limit;          // moc tran da luu ben vung
    uint32_t reserve;        // so ID dat truoc moi lan ghi
    event_id_store_fn store;
    void *ctx;
    bool persist_ok;         // false neu lan ghi gan nhat that bai -> nen ESP_LOGW
} event_id_gen_t;

// Goi 1 lan luc khoi dong (truoc khi cap ID dau tien). Tra ve persist_ok.
static inline bool event_id_init(event_id_gen_t *g, event_id_load_fn load,
                                 event_id_store_fn store, void *ctx, uint32_t reserve)
{
    uint32_t hwm = 0;
    if (reserve == 0) {
        reserve = EVENT_ID_DEFAULT_RESERVE;
    }
    if (load && !load(&hwm, ctx)) {
        hwm = 0;
    }
    g->counter = hwm;                       // tiep tuc SAU moc cu
    g->reserve = reserve;
    g->limit = hwm + reserve;
    g->store = store;
    g->ctx = ctx;
    g->persist_ok = store ? store(g->limit, ctx) : false;
    return g->persist_ok;
}

// Cap ID ke tiep (>= 1, tang dan, khong trung giua cac lan khoi dong).
static inline uint32_t event_id_next(event_id_gen_t *g)
{
    if (g->counter >= g->limit) {           // sap vuot moc -> luu moc moi TRUOC khi cap
        g->limit += g->reserve;
        g->persist_ok = g->store ? g->store(g->limit, g->ctx) : false;
    }
    return ++g->counter;
}

#ifdef __cplusplus
}
#endif
