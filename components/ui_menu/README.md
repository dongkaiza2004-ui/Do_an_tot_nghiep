# ui_menu

Giao diện menu hiển thị trên TFT ST7735.

## Vai trò
- State machine cho các màn hình: xem trực tiếp / chọn model /
  chỉnh threshold / enroll / danh sách khuôn mặt đã lưu.
- Đọc input điều hướng (nút bấm vật lý hoặc lệnh UART) để chuyển màn hình.
- Gọi display_driver để vẽ, gọi app_config/face_engine để đọc-ghi
  trạng thái khi người dùng chọn.

## Không làm gì
- Không tự vẽ pixel trực tiếp qua SPI (luôn qua display_driver),
  không tự chứa logic model.

## Phụ thuộc
- display_driver, app_config, face_engine