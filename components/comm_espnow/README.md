# comm_espnow (Node A)

## Chức năng
Gửi **ảnh sự kiện** (không phải stream liên tục) sang Node B qua ESP-NOW, mỗi khi
`face_engine` phát hiện: (1) người lạ, hoặc (2) đăng nhập thành công. Chịu trách nhiệm
**chia gói (fragment)** dữ liệu JPEG theo `espnow_frame.h` vì giới hạn kích thước gói tin
của ESP-NOW (~240 byte payload/gói), và gửi tuần tự các fragment cho 1 frame.

## Đầu vào
- `event_queue` từ `face_engine`: mỗi phần tử gồm `jpeg_data`, `jpeg_len`, `event_type`,
  `user_id`, `name`

## Đầu ra
- Chuỗi gói tin ESP-NOW gửi tới địa chỉ MAC của Node B, mỗi gói là 1
  `espnow_frame_fragment_t` (frame_id, fragment_index, fragment_total, payload)

## API công khai (gợi ý)
```c
esp_err_t comm_espnow_init(const uint8_t peer_mac[6]);
void      comm_espnow_start(QueueHandle_t event_queue);  // task nền: lấy event, fragment, gửi
```

## Phụ thuộc
- `esp_now` (ESP-IDF WiFi)
- `espnow_frame.h` (định nghĩa giao thức fragment)
- `face_engine` (nguồn `event_queue`)

## Được dùng bởi
`app_main` (khởi tạo), nhận dữ liệu từ `face_engine`

## Ghi chú
- Vì đây là **ảnh bằng chứng cho log lịch sử** (khác video live có thể chấp nhận mất khung),
  nên cân nhắc thêm cơ chế xác nhận đã nhận đủ: dùng callback `esp_now_send_cb` để biết gói
  gửi thành công ở tầng MAC (ESP-NOW có ACK phần cứng), và có thể thêm **gửi lại toàn bộ
  frame** nếu Node B không xác nhận nhận đủ trong khoảng thời gian nhất định (cần thêm 1
  cơ chế phản hồi nhỏ từ Node B — có thể qua chính ESP-NOW hoặc qua UART nếu 2 node đã nối
  dây UART sẵn, tùy bạn quyết định độ phức tạp cần thiết).
- Không cần định nghĩa `ESPNOW_FRAGMENTS_PER_FRAME` cố định vì kích thước JPEG thay đổi mỗi
  frame — `fragment_total` phải **tính động** = `ceil(jpeg_len / ESPNOW_FRAGMENT_MAX_PAYLOAD)`
  ngay tại hàm gửi.
- Cần đăng ký `peer_mac` của Node B trước khi gửi (`esp_now_add_peer`) — địa chỉ MAC nên lưu
  cấu hình được (Kconfig hoặc NVS) thay vì hard-code, để dễ thay board khi cần.
