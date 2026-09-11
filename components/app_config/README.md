# app_config

Nguồn cấu hình duy nhất (single source of truth) cho toàn bộ app.

## Vai trò
- Lưu và đọc các thông số runtime: model detect/recognition đang chọn,
  similarity threshold, thông tin peer ESP-NOW, các cờ bật/tắt tính năng.
- Ghi xuống NVS để giữ cấu hình sau khi mất điện/reset.
- Cung cấp API get/set + cơ chế thông báo (callback hoặc event) khi giá trị
  thay đổi, để các module khác (ui_menu, face_engine, comm_uart) tự cập nhật.

## Không làm gì
- Không tự vẽ UI, không tự xử lý logic model hay giao tiếp.

## Phụ thuộc
- nvs_flash (ESP-IDF)