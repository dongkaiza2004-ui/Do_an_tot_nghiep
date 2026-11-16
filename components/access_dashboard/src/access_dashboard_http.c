// access_dashboard_http.c
//
// HTTP API don gian cho Web Dashboard / App Qt. Cac endpoint chi lam nhung
// gi Matter KHONG lam duoc: lich su, anh, cau hinh camera/nguoi dung nhan
// dien. Dieu khien khoa (lock/unlock) KHONG co o day - da co Matter lo.
//
// Vi comm_uart la bat dong bo (dang ky listener, nhan qua callback), nhung
// mot so thao tac (them/xoa nguoi dung, lay danh sach) can tra ket qua ngay
// trong 1 request HTTP -> dung mot "cau" dong bo hoa tam thoi: dang ky 1
// listener rieng cho request do, cho ket qua qua queue voi timeout, roi GO
// DANG KY NGAY sau khi xong (bat buoc - xem ghi chu trong comm_uart.h).

#include "access_dashboard.h"
#include "access_dashboard_events.h"

#include "comm_uart.h"
#include "uart_protocol.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "access_dashboard_http";
static httpd_handle_t s_server = NULL;

extern const uint8_t dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const uint8_t dashboard_html_end[]   asm("_binary_dashboard_html_end");

//==========================================================================
// Cau dong bo hoa tam thoi cho comm_uart (xem giai thich o dau file)
//==========================================================================

typedef struct {
    uint8_t rsp_cmd;
    uint8_t payload[UART_MAX_PAYLOAD];
    uint8_t len;
} pending_response_t;

typedef struct {
    QueueHandle_t queue;
} wait_ctx_t;

#define ENROLL_PENDING_TTL_MS 180000

typedef struct {
    bool active;
    bool add_only;   
    uint16_t user_id;
    char name[24];
    char pin[ACCESS_USER_PIN_BUF];
    TickType_t started;
} pending_enroll_t;

static pending_enroll_t s_pending;
static access_user_provisioner_t s_prov;
static bool s_prov_set = false;

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t len = dashboard_html_end - dashboard_html_start;
    return httpd_resp_send(req, (const char *)dashboard_html_start, len);
}

static void wait_listener(uint8_t rsp_cmd, const uint8_t *payload, uint8_t len, void *ctx)
{
    wait_ctx_t *w = (wait_ctx_t *)ctx;
    pending_response_t item = {0};
    item.rsp_cmd = rsp_cmd;
    item.len = len > UART_MAX_PAYLOAD ? UART_MAX_PAYLOAD : len;
    if (payload && item.len > 0) {
        memcpy(item.payload, payload, item.len);
    }
    // Khong bao gio block trong callback cua task doc UART - neu hang doi day
    // (nguoi goi khong kip xu ly) thi danh rot goi nay, nguoi goi se tu timeout.
    xQueueSend(w->queue, &item, 0);
}

// Gui 1 lenh CMD_* va cho DUY NHAT MOT phan hoi bat ky quay ve (dung cho cac
// cap CMD/RSP 1-doi-1 don gian nhu ADD_USER_CAPTURE/SUB_USER).
static bool send_and_wait_once(uint8_t cmd, const void *cmd_payload, uint8_t cmd_len,
                                pending_response_t *out_response, TickType_t timeout)
{
    QueueHandle_t queue = xQueueCreate(4, sizeof(pending_response_t));
    if (!queue) {
        return false;
    }

    wait_ctx_t w = { .queue = queue };
    comm_uart_listener_handle_t handle = comm_uart_register_listener(wait_listener, &w);
    if (handle < 0) {
        vQueueDelete(queue);
        return false;
    }

    comm_uart_send_cmd(cmd, cmd_payload, cmd_len);
    bool ok = (xQueueReceive(queue, out_response, timeout) == pdTRUE);

    comm_uart_unregister_listener(handle);
    vQueueDelete(queue);

    return ok;
}

// Gui CMD_LIST_USER_REQ va gom toan bo RSP_USER_LIST_ITEM cho toi khi gap
// RSP_USER_LIST_END hoac het thoi gian cho.
static cJSON *request_user_list(TickType_t timeout)
{
    QueueHandle_t queue = xQueueCreate(16, sizeof(pending_response_t));
    if (!queue) {
        return NULL;
    }

    wait_ctx_t w = { .queue = queue };
    comm_uart_listener_handle_t handle = comm_uart_register_listener(wait_listener, &w);
    if (handle < 0) {
        vQueueDelete(queue);
        return NULL;
    }

    comm_uart_cmd_list_users_request();

    cJSON *array = cJSON_CreateArray();
    pending_response_t item;
    TickType_t deadline = xTaskGetTickCount() + timeout;
    bool done = false;

    while (!done) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            ESP_LOGW(TAG, "Timeout khi cho danh sach nguoi dung");
            break;
        }

        if (xQueueReceive(queue, &item, deadline - now) != pdTRUE) {
            ESP_LOGW(TAG, "Timeout khi cho danh sach nguoi dung");
            break;
        }

        if (item.rsp_cmd == RSP_USER_LIST_END) {
            done = true;
            break;
        }

        if (item.rsp_cmd == RSP_USER_LIST_ITEM && item.len >= sizeof(payload_user_list_item_t)) {
            payload_user_list_item_t p;
            memcpy(&p, item.payload, sizeof(p));

            char name_buf[sizeof(p.name) + 1] = {0};
            memcpy(name_buf, p.name, sizeof(p.name));

            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "user_id", p.user_id);
            cJSON_AddStringToObject(obj, "name", name_buf);
            cJSON_AddItemToArray(array, obj);

            if (p.is_last) {
                done = true;
            }
        }
        // Cac RSP_* khac (vd ACK ngau nhien tu lenh khac chay song song) bi bo qua.
    }

    comm_uart_unregister_listener(handle);
    vQueueDelete(queue);

    return array;
}

//==========================================================================
// Ham dung chung
//==========================================================================

// Doc toan bo body cua request vao 1 buffer cap phat dong (nguoi goi tu free()).
// Tra ve NULL neu body rong/qua lon/loi doc.
static char *read_request_body(httpd_req_t *req, size_t max_len)
{
    if (req->content_len == 0 || (size_t)req->content_len > max_len) {
        return NULL;
    }

    char *buf = (char *)malloc((size_t)req->content_len + 1);
    if (!buf) {
        return NULL;
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
    }
    buf[req->content_len] = '\0';
    return buf;
}

// Lay so nguyen o cuoi URI, vd "/api/image/1234" -> 1234.
static uint32_t parse_trailing_id(const char *uri)
{
    const char *last_slash = strrchr(uri, '/');
    if (!last_slash) {
        return 0;
    }
    return (uint32_t)strtoul(last_slash + 1, NULL, 10);
}

static esp_err_t send_json_and_free(httpd_req_t *req, cJSON *json, const char *fallback)
{
    char *json_str = json ? cJSON_PrintUnformatted(json) : NULL;
    if (json) {
        cJSON_Delete(json);
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json_str ? json_str : fallback);
    free(json_str);
    return err;
}

//==========================================================================
// GET /api/status
//==========================================================================

static esp_err_t handle_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//==========================================================================
// GET /api/history?limit=N
//==========================================================================

static esp_err_t handle_history(httpd_req_t *req)
{
    size_t limit = 20;

    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char val[8];
            if (httpd_query_key_value(query, "limit", val, sizeof(val)) == ESP_OK) {
                int v = atoi(val);
                if (v > 0) {
                    limit = (size_t)v;
                }
            }
        }
    }

    cJSON *array = access_dashboard_build_history_json(limit);
    return send_json_and_free(req, array, "[]");
}

//==========================================================================
// GET /api/image/{event_id}
//==========================================================================

static esp_err_t handle_image(httpd_req_t *req)
{
    uint32_t event_id = parse_trailing_id(req->uri);

    uint8_t *data = NULL;
    size_t len = 0;
    if (access_dashboard_copy_image(event_id, &data, &len) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Khong tim thay anh cho event_id nay");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t err = httpd_resp_send(req, (const char *)data, (ssize_t)len);
    free(data);
    return err;
}

//==========================================================================
// GET /api/users
//==========================================================================

static esp_err_t handle_users_list(httpd_req_t *req)
{
    cJSON *array = request_user_list(pdMS_TO_TICKS(3000));
    if (!array) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Khong lay duoc danh sach nguoi dung");
        return ESP_FAIL;
    }
    return send_json_and_free(req, array, "[]");
}

static void secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) {
        *v++ = 0;
    }
}

static void pending_clear(void)
{
    secure_zero(&s_pending, sizeof(s_pending));
}

esp_err_t access_dashboard_set_user_provisioner(const access_user_provisioner_t *p)
{
    pending_clear();
    if (!p) {
        s_prov_set = false;
        memset(&s_prov, 0, sizeof(s_prov));
        return ESP_OK;
    }
    if (!p->register_user || p->max_user_index == 0 || p->pin_min_len == 0 ||
        p->pin_max_len < p->pin_min_len || p->pin_max_len >= ACCESS_USER_PIN_BUF) {
        return ESP_ERR_INVALID_ARG;
    }
    s_prov = *p;
    s_prov_set = true;
    return ESP_OK;
}

static bool pin_is_valid(const char *pin)
{
    size_t n = strlen(pin);
    if (n < s_prov.pin_min_len || n > s_prov.pin_max_len) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (pin[i] < '0' || pin[i] > '9') {
            return false;
        }
    }
    return true;
}

static void zero_json_string(cJSON *item)
{
    if (item && cJSON_IsString(item) && item->valuestring) {
        secure_zero(item->valuestring, strlen(item->valuestring));
    }
}

// Gui CMD_SUB_USER va tra ve true neu Node A xac nhan da xoa khuon mat.
static bool rollback_face(uint16_t user_id)
{
    payload_sub_user_t sub = { .user_id = user_id };
    pending_response_t resp;
    if (!send_and_wait_once(CMD_SUB_USER, &sub, sizeof(sub), &resp, pdMS_TO_TICKS(3000))) {
        return false;
    }
    if (resp.rsp_cmd != RSP_SUB_USER_RESULT || resp.len < sizeof(payload_sub_user_result_t)) {
        return false;
    }
    payload_sub_user_result_t r;
    memcpy(&r, resp.payload, sizeof(r));
    return r.success != 0;
}

//==========================================================================
// POST /api/users/start  {"user_id":N,"name":"..."}
//==========================================================================

static esp_err_t handle_user_add_start(httpd_req_t *req)
{
    char *body = read_request_body(req, 512);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body khong hop le hoac qua lon");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(body);
    secure_zero(body, strlen(body));
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON khong hop le");
        return ESP_FAIL;
    }

    cJSON *user_id_item = cJSON_GetObjectItem(json, "user_id");
    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    cJSON *pin_item = cJSON_GetObjectItem(json, "pin");
    if (!cJSON_IsNumber(user_id_item) || !cJSON_IsString(name_item)) {
        zero_json_string(pin_item);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Thieu truong user_id (so) hoac name (chuoi)");
        return ESP_FAIL;
    }

    // user_id phai la so nguyen trong 1..max (max do bo dang ky khoa cho biet, mac dinh 65535)
    const int max_id = s_prov_set ? (int)s_prov.max_user_index : 65535;
    char msg[112];
    if (user_id_item->valuedouble != (double)user_id_item->valueint ||
        user_id_item->valueint < 1 || user_id_item->valueint > max_id) {
        snprintf(msg, sizeof(msg), "user_id phai la so nguyen trong khoang 1..%d", max_id);
        zero_json_string(pin_item);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_FAIL;
    }

    uint16_t user_id = (uint16_t)user_id_item->valueint;
    char name[24] = {0};
    strncpy(name, name_item->valuestring, sizeof(name) - 1);

    // Nguoi dung DA co tren khoa -> chi them mau khuon mat cho Node A (nhieu mau cho 1 nguoi). PIN bi bo qua:
    // khong doc, khong kiem tra, khong luu. KHONG bao gio dung toi nguoi dung/PIN dang co tren khoa.
    const bool add_only = s_prov_set && s_prov.user_exists && s_prov.user_exists(user_id);
    char pin[ACCESS_USER_PIN_BUF] = {0};
    if (s_prov_set && !add_only) {
        if (!cJSON_IsString(pin_item) || !pin_is_valid(pin_item->valuestring)) {
            // KHONG dua PIN vao thong bao loi
            snprintf(msg, sizeof(msg), "PIN phai gom %u..%u chu so (0-9)",
                     (unsigned)s_prov.pin_min_len, (unsigned)s_prov.pin_max_len);
            zero_json_string(pin_item);
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
            return ESP_FAIL;
        }
        strncpy(pin, pin_item->valuestring, sizeof(pin) - 1);
    }
    zero_json_string(pin_item);
    cJSON_Delete(json);

    if (s_prov_set && !add_only) {
        // Kiem tra som, TRUOC khi bat nguoi dung chup anh, de khong chup xong moi bao loi
        if (s_prov.credential_slot_busy && s_prov.credential_slot_busy(user_id)) {
            secure_zero(pin, sizeof(pin));
            snprintf(msg, sizeof(msg), "O PIN cua User ID %u da bi chiem tren khoa. Xoa PIN do truoc khi dang ky",
                     (unsigned)user_id);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
            return ESP_FAIL;
        }
        if (s_prov.pin_in_use && s_prov.pin_in_use(pin)) {
            secure_zero(pin, sizeof(pin));
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "PIN nay da duoc nguoi dung khac su dung, hay chon PIN khac");
            return ESP_FAIL;
        }
    }
    comm_uart_cmd_add_user_start(user_id, name);

    pending_clear();
    if (s_prov_set) {
        s_pending.active = true;
        s_pending.add_only = add_only;
        s_pending.user_id = user_id;
        memcpy(s_pending.name, name, sizeof(s_pending.name));
        memcpy(s_pending.pin, pin, sizeof(s_pending.pin)); 
        s_pending.started = xTaskGetTickCount();
    }
    secure_zero(pin, sizeof(pin));
    httpd_resp_set_type(req, "application/json");
    if (add_only) {
        return httpd_resp_sendstr(req,
            "{\"ok\":true,\"mode\":\"add_face_only\",\"note\":\"nguoi dung da co tren khoa: chi them mau khuon mat, PIN (neu co) bi bo qua; goi /api/users/capture khi san sang chup\"}");
    }
    if (s_prov_set) {
        return httpd_resp_sendstr(req,
            "{\"ok\":true,\"mode\":\"new_user\",\"note\":\"da gui lenh bat dau dang ky, goi tiep /api/users/capture khi san sang chup\"}");
    }

    return httpd_resp_sendstr(req,
        "{\"ok\":true,\"note\":\"da gui lenh bat dau dang ky, goi tiep /api/users/capture khi san sang chup\"}");
}

//==========================================================================
// POST /api/users/capture
//==========================================================================

static esp_err_t handle_user_add_capture(httpd_req_t *req)
{
    pending_response_t resp;
    bool ok = send_and_wait_once(CMD_ADD_USER_CAPTURE, NULL, 0, &resp, pdMS_TO_TICKS(5000));

    httpd_resp_set_type(req, "application/json");
    if (!ok || resp.rsp_cmd != RSP_ADD_USER_RESULT || resp.len < sizeof(payload_add_user_result_t)) {
        // Giu nguyen phien dang ky (chua xoa PIN) de nguoi dung co the bam "Chup" lai
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"khong nhan duoc ket qua tu camera (timeout)\"}");
    }

    payload_add_user_result_t result;
    memcpy(&result, resp.payload, sizeof(result));

    char name_buf[sizeof(result.name) + 1] = {0};
    memcpy(name_buf, result.name, sizeof(result.name));

    const bool face_ok = result.success != 0;
    bool matter_ok = false;
    bool add_only_done = false;     // chi them mau khuon mat cho nguoi dung DA co tren khoa (khong dung toi khoa)
    bool rolled_back = false;
    char error[200] = {0};
    char note[160] = {0};

    if (!face_ok) {
        // Node A bao THAT BAI: khong co gi duoc them, nen KHONG co gi de huy (khong goi CMD_SUB_USER)
        pending_clear();
        snprintf(error, sizeof(error), "Camera bao dang ky khuon mat that bai");
    } else if (s_prov_set) {
        // === Node A DA BAO THANH CONG 
        const bool valid_pending = s_pending.active && s_pending.user_id == result.user_id &&
                                   (TickType_t)(xTaskGetTickCount() - s_pending.started) <= pdMS_TO_TICKS(ENROLL_PENDING_TTL_MS);
        if (valid_pending && s_pending.add_only) {
            add_only_done = true;
        } else if (!valid_pending) {
            if (s_prov.user_exists && s_prov.user_exists(result.user_id)) {
                // Nguoi dung da co tren khoa -> day la them mau. TUYET DOI khong huy (CMD_SUB_USER xoa TOAN BO mau cua nguoi nay)
                add_only_done = true;
                snprintf(note, sizeof(note), "Phien khong con hieu luc nhung nguoi dung da co tren khoa: chi them mau khuon mat");
            } else {
                snprintf(error, sizeof(error), "Phien dang ky khong con hieu luc (het han hoac chua bam 'Bat dau')");
            }
        } else {
            // Nguoi dung moi: bay gio moi dang ky tren khoa
            access_user_prov_result_t pr = s_prov.register_user(result.user_id, s_pending.name, s_pending.pin);
            if (pr == ACCESS_USER_PROV_OK) {
                matter_ok = true;
            } else if (pr == ACCESS_USER_PROV_ERR_EXISTS) {
                // Nguoi dung vua duoc tao tren khoa giua luc "Bat dau" va "Chup": coi nhu them mau, giu nguyen nguoi dung/PIN cua khoa
                add_only_done = true;
                snprintf(note, sizeof(note), "User ID vua duoc tao tren khoa: chi them mau khuon mat, PIN bi bo qua");
            } else if (pr == ACCESS_USER_PROV_ERR_CREDENTIAL_SLOT_BUSY) {
                snprintf(error, sizeof(error), "O PIN cua User ID %u da bi chiem tren khoa", (unsigned)result.user_id);
            } else if (pr == ACCESS_USER_PROV_ERR_PIN_IN_USE) {
                snprintf(error, sizeof(error), "PIN nay da duoc nguoi dung khac su dung");
            } else {
                snprintf(error, sizeof(error), "Khong ghi duoc nguoi dung vao khoa (loi luu tru)");
            }
        }
        pending_clear();
        if (!matter_ok && !add_only_done) {
            rolled_back = rollback_face(result.user_id);   // chi cho nguoi dung MOI: tat ca hoac khong gi
            size_t n = strlen(error);
            snprintf(error + n, sizeof(error) - n, rolled_back
                     ? ". Da huy khuon mat vua dang ky tren camera"
                     : ". CANH BAO: khong xoa duoc khuon mat tren camera, hay xoa thu cong user nay");
        }
    }


    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "ok", face_ok && (!s_prov_set || matter_ok || add_only_done));
    cJSON_AddNumberToObject(obj, "user_id", result.user_id);
    cJSON_AddStringToObject(obj, "name", name_buf);

    if (s_prov_set) {
        cJSON_AddBoolToObject(obj, "matter_registered", matter_ok);
        if (add_only_done) {
            cJSON_AddStringToObject(obj, "mode", "add_face_only");
        }
        if (face_ok && !matter_ok && !add_only_done) {
            cJSON_AddBoolToObject(obj, "face_rolled_back", rolled_back);
        }
    }
    if (note[0]) {
        cJSON_AddStringToObject(obj, "note", note);
    }
    if (error[0]) {
        cJSON_AddStringToObject(obj, "error", error);
    }

    return send_json_and_free(req, obj, "{\"ok\":false}");
}

//==========================================================================
// POST /api/users/cancel
//==========================================================================

static esp_err_t handle_user_add_cancel(httpd_req_t *req)
{
    pending_clear(); 
    comm_uart_cmd_add_user_cancel();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//==========================================================================
// DELETE /api/users/{id}
//==========================================================================

static esp_err_t handle_user_delete(httpd_req_t *req)
{

    int parsed_id = parse_trailing_id(req->uri);
    if (s_prov_set && (parsed_id < 1 || parsed_id > (int)s_prov.max_user_index)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "user_id khong hop le");
        return ESP_FAIL;
    }
    uint16_t user_id = (uint16_t)parsed_id;

    payload_sub_user_t sub_payload = { .user_id = user_id };
    pending_response_t resp;
    bool ok = send_and_wait_once(CMD_SUB_USER, &sub_payload, sizeof(sub_payload), &resp, pdMS_TO_TICKS(3000));

    httpd_resp_set_type(req, "application/json");
    if (!ok || resp.rsp_cmd != RSP_SUB_USER_RESULT || resp.len < sizeof(payload_sub_user_result_t)) {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"khong nhan duoc ket qua tu camera (timeout)\"}");
    }

    payload_sub_user_result_t result;
    memcpy(&result, resp.payload, sizeof(result));

    // Chi go quyen tren khoa SAU KHI Node A xac nhan da xoa khuon mat (cung thu tu voi luc dang ky)
    const bool manage_lock = s_prov_set && s_prov.unregister_user;
    bool matter_removed = false;
    if (result.success && manage_lock) {
        matter_removed = s_prov.unregister_user(result.user_id);
    }
    char out[128];
    if (manage_lock) {
        snprintf(out, sizeof(out), "{\"ok\":%s,\"user_id\":%u,\"matter_removed\":%s}",
                 result.success ? "true" : "false", (unsigned)result.user_id, matter_removed ? "true" : "false");
    } else {
        snprintf(out, sizeof(out), "{\"ok\":%s,\"user_id\":%u}",
                 result.success ? "true" : "false", (unsigned)result.user_id);
    }
    return httpd_resp_sendstr(req, out);
}
//==========================================================================
// Kiem tra mien gia tri (cung gioi han voi Node A, dinh nghia trong uart_protocol.h)
// Dung valuedouble de bat so khong nguyen / qua lon, tranh ep kieu cat bot (vd 300 -> 44).
//==========================================================================
static bool json_is_int_in_cam_range(const cJSON *item)
{
    return item->valuedouble == (double)item->valueint && uart_cam_param_valid(item->valueint);
}

static bool json_is_valid_model_id(const cJSON *item)
{
    return item->valuedouble == (double)item->valueint && uart_model_id_valid(item->valueint);
}



//==========================================================================
// POST /api/camera  {"brightness":N,"contrast":N,"flip":bool,"mirror":bool}
// Tat ca truong deu tuy chon - chi gui lenh cho truong nao co mat trong JSON.
//==========================================================================

static esp_err_t handle_camera_config(httpd_req_t *req)
{
    char *body = read_request_body(req, 256);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body khong hop le hoac qua lon");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON khong hop le");
        return ESP_FAIL;
    }

    cJSON *item;

    if ((item = cJSON_GetObjectItem(json, "brightness")) && cJSON_IsNumber(item) && !json_is_int_in_cam_range(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "brightness phai la so nguyen trong khoang -3..3");
        return ESP_FAIL;
    }
    if ((item = cJSON_GetObjectItem(json, "contrast")) && cJSON_IsNumber(item) && !json_is_int_in_cam_range(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "contrast phai la so nguyen trong khoang -3..3");
        return ESP_FAIL;
    }
    if ((item = cJSON_GetObjectItem(json, "brightness")) && cJSON_IsNumber(item)) {
        comm_uart_cmd_set_brightness((int8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "contrast")) && cJSON_IsNumber(item)) {
        comm_uart_cmd_set_contrast((int8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "flip")) && cJSON_IsBool(item)) {
        comm_uart_cmd_set_flip(cJSON_IsTrue(item));
    }
    if ((item = cJSON_GetObjectItem(json, "mirror")) && cJSON_IsBool(item)) {
        comm_uart_cmd_set_mirror(cJSON_IsTrue(item));
    }
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//==========================================================================
// POST /api/liveness  {"enable":bool}
//==========================================================================

static esp_err_t handle_liveness_config(httpd_req_t *req)
{
    char *body = read_request_body(req, 128);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body khong hop le hoac qua lon");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON khong hop le");
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItem(json, "enable");
    bool enable = cJSON_IsTrue(item);
    cJSON_Delete(json);

    comm_uart_cmd_enable_liveness_check(enable);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//==========================================================================
// POST /api/recognition  {"model_id":N,"threshold":F}
//==========================================================================

static esp_err_t handle_recognition_config(httpd_req_t *req)
{
    char *body = read_request_body(req, 128);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body khong hop le hoac qua lon");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON khong hop le");
        return ESP_FAIL;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(json, "model_id")) && cJSON_IsNumber(item) && !json_is_valid_model_id(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "model_id khong hop le (chi 0 hoac 1)");
        return ESP_FAIL;
    }
    if ((item = cJSON_GetObjectItem(json, "threshold")) && cJSON_IsNumber(item) &&
        !uart_threshold_valid((float)item->valuedouble)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "threshold phai trong khoang 0.30..1.00");
        return ESP_FAIL;
    }
    if ((item = cJSON_GetObjectItem(json, "model_id")) && cJSON_IsNumber(item)) {
        comm_uart_cmd_set_model((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "threshold")) && cJSON_IsNumber(item)) {
        comm_uart_cmd_set_threshold((float)item->valuedouble);
    }
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//==========================================================================
// Khoi dong / dung server
//==========================================================================

esp_err_t access_dashboard_start(void)
{
    esp_err_t err = access_dashboard_events_init();
    if (err != ESP_OK) {
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.stack_size = 8192; // cJSON + xu ly JSON body can nhieu stack hon mac dinh

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start that bai: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",                  .method = HTTP_GET,    .handler = handle_root }, 
        { .uri = "/api/status",        .method = HTTP_GET,    .handler = handle_status },
        { .uri = "/api/history",       .method = HTTP_GET,    .handler = handle_history },
        { .uri = "/api/image/*",       .method = HTTP_GET,    .handler = handle_image },
        { .uri = "/api/users",         .method = HTTP_GET,    .handler = handle_users_list },
        { .uri = "/api/users/start",   .method = HTTP_POST,   .handler = handle_user_add_start },
        { .uri = "/api/users/capture", .method = HTTP_POST,   .handler = handle_user_add_capture },
        { .uri = "/api/users/cancel",  .method = HTTP_POST,   .handler = handle_user_add_cancel },
        { .uri = "/api/users/*",       .method = HTTP_DELETE, .handler = handle_user_delete },
        { .uri = "/api/camera",        .method = HTTP_POST,   .handler = handle_camera_config },
        { .uri = "/api/liveness",      .method = HTTP_POST,   .handler = handle_liveness_config },
        { .uri = "/api/recognition",   .method = HTTP_POST,   .handler = handle_recognition_config },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Dang ky URI %s (method %d) that bai: %s",
                     routes[i].uri, routes[i].method, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "access_dashboard HTTP server dang chay");
    return ESP_OK;
}

void access_dashboard_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
