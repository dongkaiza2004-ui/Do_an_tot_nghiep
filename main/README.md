# main

Entry point của firmware. Chỉ chịu trách nhiệm khởi tạo hệ thống và tạo
task — không chứa logic nghiệp vụ (logic thật nằm trong các component ở
`components/`).

## Các file

### `app_main.cpp` (trước đây là `main.cpp`)
- Hàm `app_main()` — điểm khởi chạy đầu tiên của ESP-IDF.
- Gọi init theo thứ tự: `app_config` (đọc cấu hình từ NVS) → `cam_driver`
  → `display_driver` → `face_engine` → `ui_menu` → `comm_uart` →
  `comm_espnow`.
- Tạo các FreeRTOS task chính và queue kết nối giữa chúng (frame queue,
  result queue, control queue).
- Không xử lý chi tiết camera/model/UI/giao tiếp — chỉ "lắp ráp" các
  component lại với nhau.

### `CMakeLists.txt`
- Đăng ký component `main` với ESP-IDF build system.
- Liệt kê source file (`app_main.cpp`).
- Khai báo `PRIV_REQUIRES` tới các component tự viết trong
  `components/` (app_config, cam_driver, display_driver, face_engine,
  ui_menu, comm_uart, comm_espnow) để `app_main.cpp` include và gọi
  được API của chúng.

### `idf_component.yml`
- Khai báo các dependency tải từ ESP Component Registry (managed
  components): `espressif/esp32-camera`, `espressif/human_face_detect`,
  `espressif/human_face_recognition`, `teriyakigod/esp_lcd_st7735`.
- Không khai báo component tự viết ở đây — chúng được ESP-IDF tự tìm
  thấy qua thư mục `components/` cục bộ, không cần đăng ký qua file này.

## Ghi chú
- Nếu `app_main.cpp` bắt đầu chứa logic xử lý cụ thể (vòng lặp đọc
  frame, vẽ, tính threshold...) thay vì chỉ init/tạo task, đó là dấu
  hiệu logic đang bị đặt sai chỗ — nên chuyển phần đó vào component
  tương ứng trong `components/`.