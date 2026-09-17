# face_engine

## Chức năng
Thành phần **lõi AI** của Node A — bọc (wrap) `HumanFaceDetect` + `HumanFaceRecognizer`
(esp-dl), xử lý mỗi frame nhận từ `cam_driver`, và đảm nhiệm:

1. **Detect + Recognize** mỗi frame theo model/threshold đang cấu hình trong `app_state`.
2. **Quản lý CSDL khuôn mặt** (lưu SPIFFS `/spiffs/face.db`): enroll, xóa, liệt kê.
3. **Luồng enroll 2 bước**: `ADD_USER_START` (vào chế độ chờ, báo `overlay_render` vẽ hint
   "đưa mặt vào khung") → `ADD_USER_CAPTURE` (chụp frame hiện tại để enroll khi có lệnh xác
   nhận từ Node B) → trả kết quả qua `comm_uart`.
4. **Phân loại sự kiện + chống spam**: khi phát hiện người lạ (không khớp CSDL) hoặc nhận
   diện thành công (khớp), tạo 1 "event" kèm ảnh + metadata, có cơ chế cooldown (mặc định
   10 giây/khuôn mặt tại cùng vị trí) trước khi cho phép tạo event tiếp theo — tránh gửi
   ảnh liên tục khi 1 người đứng yên trước camera.

## Đầu vào
- `frame_queue` từ `cam_driver`
- `app_state`: model, threshold, enroll state

## Đầu ra
- Kết quả detect/recognize (box tọa độ, tên/id, similarity) → đẩy cho `overlay_render` để vẽ
- Khi có sự kiện (stranger/login_success) → đẩy `(jpeg-ready frame, event_type, user_id, name)`
  vào `event_queue` cho `comm_espnow` gửi đi
- Kết quả enroll/xóa/liệt kê user → trả về cho `comm_uart` gửi phản hồi Node B

## API công khai (gợi ý)
```c
esp_err_t face_engine_init(void);
void      face_engine_process_frame(camera_fb_t *fb, face_result_t *out_result);

// Model & threshold
void face_engine_switch_model(recog_model_t model);
void face_engine_set_threshold(float threshold);

// Quản lý user
int  face_engine_enroll_capture(uint16_t user_id, const char *name);  // gọi khi nhận ADD_USER_CAPTURE
bool face_engine_delete_user(uint16_t user_id);
int  face_engine_list_users(user_info_t *out_list, int max_count);
```

## Cấu trúc dữ liệu chính (gợi ý, đặt trong `include/face_engine.h`)
```c
typedef enum { EVENT_NONE, EVENT_STRANGER, EVENT_LOGIN_SUCCESS } face_event_type_t;

typedef struct {
    face_event_type_t type;
    uint16_t user_id;      // 0xFFFF nếu là người lạ
    char     name[24];
    uint8_t  *jpeg_data;
    size_t   jpeg_len;
} face_event_t;
```

## Phụ thuộc
- `esp-dl`, `human_face_detect`, `human_face_recognition` (managed components)
- `app_state`
- `esp_jpeg`/`esp_new_jpeg` (nén ảnh trước khi đưa vào `event_queue`)
- SPIFFS (`face.db`)

## Được dùng bởi
`overlay_render` (nhận kết quả detect để vẽ), `comm_espnow` (tiêu thụ `event_queue`),
`comm_uart` (gọi các hàm quản lý user/model/threshold khi có lệnh từ Node B)

## Ghi chú
- Logic cooldown chống spam nên khóa theo **vị trí khuôn mặt gần trùng** (so sánh box tọa độ
  giữa các frame liên tiếp), không chỉ theo thời gian tuyệt đối — nếu không, 2 người lạ khác
  nhau xuất hiện gần nhau về thời gian có thể bị bỏ sót sự kiện của người thứ 2.
- Nên nén JPEG **ngay tại đây** (không phải ở `comm_espnow`) vì `face_engine` là nơi có sẵn
  frame gốc + biết chính xác thời điểm cần chụp; `comm_espnow` chỉ nên lo việc fragment và gửi.
