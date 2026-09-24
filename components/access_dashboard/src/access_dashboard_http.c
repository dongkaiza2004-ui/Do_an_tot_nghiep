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
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON khong hop le");
        return ESP_FAIL;
    }

    cJSON *user_id_item = cJSON_GetObjectItem(json, "user_id");
    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    if (!cJSON_IsNumber(user_id_item) || !cJSON_IsString(name_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Thieu truong user_id (so) hoac name (chuoi)");
        return ESP_FAIL;
    }

    uint16_t user_id = (uint16_t)user_id_item->valueint;
    char name[24] = {0};
    strncpy(name, name_item->valuestring, sizeof(name) - 1);
    cJSON_Delete(json);

    comm_uart_cmd_add_user_start(user_id, name);

    httpd_resp_set_type(req, "application/json");
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
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"khong nhan duoc ket qua tu camera (timeout)\"}");
    }

    payload_add_user_result_t result;
    memcpy(&result, resp.payload, sizeof(result));

    char name_buf[sizeof(result.name) + 1] = {0};
    memcpy(name_buf, result.name, sizeof(result.name));

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "ok", result.success != 0);
    cJSON_AddNumberToObject(obj, "user_id", result.user_id);
    cJSON_AddStringToObject(obj, "name", name_buf);

    return send_json_and_free(req, obj, "{\"ok\":false}");
}

//==========================================================================
// POST /api/users/cancel
//==========================================================================

static esp_err_t handle_user_add_cancel(httpd_req_t *req)
{
    comm_uart_cmd_add_user_cancel();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//==========================================================================
// DELETE /api/users/{id}
//==========================================================================

static esp_err_t handle_user_delete(httpd_req_t *req)
{
    uint16_t user_id = (uint16_t)parse_trailing_id(req->uri);

    payload_sub_user_t sub_payload = { .user_id = user_id };
    pending_response_t resp;
    bool ok = send_and_wait_once(CMD_SUB_USER, &sub_payload, sizeof(sub_payload), &resp, pdMS_TO_TICKS(3000));

    httpd_resp_set_type(req, "application/json");
    if (!ok || resp.rsp_cmd != RSP_SUB_USER_RESULT || resp.len < sizeof(payload_sub_user_result_t)) {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"khong nhan duoc ket qua tu camera (timeout)\"}");
    }

    payload_sub_user_result_t result;
    memcpy(&result, resp.payload, sizeof(result));

    char out[96];
    snprintf(out, sizeof(out), "{\"ok\":%s,\"user_id\":%u}",
             result.success ? "true" : "false", (unsigned)result.user_id);
    return httpd_resp_sendstr(req, out);
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
