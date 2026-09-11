# cam_driver

Bọc (wrap) esp32-camera, chỉ lo phần thu khung hình.

## Vai trò
- Khởi tạo camera (pin, format, resolution, grab mode).
- Chạy 1 task chụp frame liên tục, đẩy con trỏ frame vào queue
  cho face_engine xử lý.
- Cung cấp API chỉnh thông số cảm biến runtime (vflip, hmirror,
  brightness, exposure...) để ui_menu/comm_uart gọi tới.

## Không làm gì
- Không detect/recognize khuôn mặt, không vẽ lên khung hình.

## Phụ thuộc
- espressif__esp32-camera
- app_events (định nghĩa struct frame_msg_t)