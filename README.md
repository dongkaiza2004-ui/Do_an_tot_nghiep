# cam_node — Node A (ESP32-S3 + Camera + AI)

Firmware độc lập, chịu trách nhiệm: chụp ảnh, chạy AI phát hiện/nhận diện khuôn mặt,
hiển thị stream trực tiếp lên TFT tại chỗ, tự động điều khiển đèn theo ánh sáng môi
trường, nhận lệnh điều khiển từ Node B qua UART, và gửi ảnh sự kiện (người lạ / đăng
nhập thành công) sang Node B qua ESP-NOW.

Xem chi tiết `README.md` bên trong từng component để biết chức năng, input/output,
API công khai.

## Sơ đồ luồng dữ liệu

```
                         ┌─────────────┐
                         │  light_ctrl  │  (chạy độc lập, không phụ thuộc luồng camera)
                         └─────────────┘

cam_driver ──frame──▶ face_engine ──(kết quả detect/recognize + event)──┐
                                                                          │
                          ┌───────────────────────────────────────────────┤
                          ▼                                              ▼
                  overlay_render (vẽ box/tên/FPS/hint)            comm_espnow
                          │                                        (gửi ảnh sự kiện
                          ▼                                         kèm metadata,
                  display_driver (TFT local)                       chỉ khi có event)

comm_uart ◀──lệnh điều khiển từ Node B── (đổi model, threshold, config cam,
                                            add/sub user, show_fps, start/stop...)
comm_uart ──ACK / kết quả / user list──▶ Node B

app_state: nơi lưu trạng thái runtime dùng chung (model, threshold, brightness,
contrast, flip, mirror, show_fps, enroll_state...). Mọi component đọc/ghi qua đây,
không tự giữ biến riêng, tránh lệch trạng thái giữa các module.
```

## Danh sách component

| Component | Vai trò tóm tắt |
|---|---|
| `app_state` | Nguồn sự thật duy nhất cho cấu hình/trạng thái runtime, lưu NVS |
| `cam_driver` | Khởi tạo & điều khiển phần cứng camera, task chụp frame |
| `face_engine` | Detect + recognize khuôn mặt, quản lý user DB, phân loại sự kiện |
| `overlay_render` | Vẽ box/tên/FPS/hướng dẫn enroll lên bitmap |
| `display_driver` | Driver TFT ST7735 mức thấp (raw bitmap, không LVGL) |
| `light_ctrl` | Đọc cảm biến ánh sáng, tự bật/tắt đèn |
| `comm_uart` | Nhận lệnh điều khiển, gửi phản hồi cho Node B |
| `comm_espnow` | Nén JPEG + gửi ảnh sự kiện sang Node B |
