# cam_driver

## Chức năng
Khởi tạo phần cứng camera (`esp32-camera`), cấu hình chân GPIO/clock/pixel format/frame
size, và chạy 1 task nền **chụp frame liên tục**, đẩy vào hàng đợi (`frame_queue`) cho
`face_engine` xử lý. Đồng thời áp dụng các thay đổi cấu hình cảm biến (brightness,
contrast, flip, mirror) khi `app_state` báo có thay đổi.

## Đầu vào
- Giá trị cấu hình từ `app_state` (brightness/contrast/flip/mirror) — áp dụng lại mỗi khi
  `comm_uart` cập nhật giá trị mới (qua callback hoặc so sánh giá trị mỗi vòng lặp).

## Đầu ra
- `frame_queue` (FreeRTOS queue) chứa con trỏ `camera_fb_t*` — mỗi phần tử là 1 frame vừa
  chụp, xuôi dòng cho `face_engine`. Sau khi `face_engine` xử lý xong phải gọi
  `esp_camera_fb_return()` để trả buffer, không được giữ lại lâu (camera driver dùng số
  lượng fb hạn chế — hiện `fb_count = 2`).

## API công khai (gợi ý)
```c
esp_err_t cam_driver_init(void);              // gọi 1 lần lúc app_main khởi động
void      cam_driver_start(QueueHandle_t out_queue);  // bắt đầu task chụp, đẩy fb vào out_queue
void      cam_driver_apply_sensor_config(int8_t brightness, int8_t contrast, bool flip, bool mirror);
```

## Phụ thuộc
- `esp32-camera` (managed component)
- `app_state` (đọc cấu hình cảm biến hiện tại lúc init và khi có thay đổi)

## Được dùng bởi
`face_engine` (tiêu thụ `frame_queue`)

## Ghi chú
- Giữ nguyên toàn bộ phần định nghĩa chân GPIO camera và cấu hình `camera_config_t` hiện có
  trong `main.cpp` cũ — chỉ di chuyển logic `init_camera()` + phần set sensor vào đây,
  không đổi giá trị.
- Cân nhắc: nếu tốc độ `face_engine` xử lý chậm hơn tốc độ chụp, `frame_queue` cần có chính
  sách drop-frame-cũ (ví dụ dùng `xQueueOverwrite` với queue length = 1) để tránh trễ hình
  tích lũy — không nên xếp hàng chờ nhiều frame cũ.
