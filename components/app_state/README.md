# app_state

## Chức năng
Giữ **toàn bộ trạng thái cấu hình runtime** của Node A ở một nơi duy nhất, để các
component khác đọc/ghi qua đây thay vì tự giữ biến riêng (tránh lệch trạng thái khi
nhiều nơi cùng cần biết "model đang dùng là gì", "threshold hiện tại bao nhiêu"...).

Một phần trạng thái được **lưu vào NVS** để nhớ lại sau khi mất điện/reset (model,
threshold, brightness/contrast/flip/mirror, ngưỡng ánh sáng); phần còn lại chỉ tồn
tại trong RAM vì có tính tạm thời (đang trong chế độ enroll hay không, đang stream
hay không).

## Trạng thái quản lý
- `recog_model_t current_model` — model nhận diện đang dùng (MFN/MBF)
- `float match_threshold` — ngưỡng coi là "khớp"
- `int8_t brightness`, `int8_t contrast`, `bool flip`, `bool mirror` — cấu hình cảm biến camera
- `bool show_fps` — có vẽ FPS overlay hay không
- `bool streaming_local` — có đang chạy vòng lặp hiển thị/AI hay không (thường luôn true, dự phòng cho lệnh tắt để tiết kiệm điện)
- `int light_threshold_lux` — ngưỡng để `light_ctrl` quyết định bật đèn
- Trạng thái enroll: `enroll_in_progress`, `pending_user_id`, `pending_user_name[24]`

## API công khai (gợi ý)
```c
void app_state_init(void);                      // load từ NVS lúc boot
void app_state_set_model(recog_model_t m);
recog_model_t app_state_get_model(void);
void app_state_set_threshold(float t);
float app_state_get_threshold(void);
void app_state_set_cam_param(int8_t brightness, int8_t contrast, bool flip, bool mirror);
void app_state_set_show_fps(bool enable);
void app_state_begin_enroll(uint16_t user_id, const char *name);
void app_state_cancel_enroll(void);
bool app_state_is_enrolling(void);
void app_state_save_to_nvs(void);               // gọi sau mỗi lần đổi giá trị cần lưu bền
```

## Phụ thuộc
- `nvs_flash` (ESP-IDF)

## Được dùng bởi
`cam_driver` (đọc brightness/contrast/flip/mirror), `face_engine` (đọc model/threshold/enroll
state), `overlay_render` (đọc show_fps/enroll state để vẽ hint), `comm_uart` (ghi giá trị mới
khi nhận lệnh từ Node B), `light_ctrl` (đọc/ghi ngưỡng ánh sáng).

## Ghi chú
- Không nên gọi `app_state_save_to_nvs()` sau **mỗi** lần thay đổi nếu tần suất cao (ví dụ
  threshold đổi liên tục lúc test) — nên debounce (chỉ ghi NVS sau khi giá trị ổn định một
  khoảng thời gian) để tránh hao mòn flash.
