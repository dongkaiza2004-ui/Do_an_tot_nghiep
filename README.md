# project_face_cam

Firmware ESP32-S3: camera + nhận diện khuôn mặt (detect/recognition),
hiển thị TFT ST7735, điều khiển qua UART, truyền dữ liệu nặng qua
ESP-NOW. Hỗ trợ đổi model và chỉnh threshold ngay lúc runtime.

## Cấu trúc thư mục

- **`CMakeLists.txt`** — file cấu hình project gốc (boilerplate bắt
  buộc của ESP-IDF), khai báo tên project.

- **`partitions.csv`** — bảng phân vùng flash: `nvs`, `phy_init`,
  `factory` (app + model rodata), `storage` (SPIFFS chứa face.db).

- **`sdkconfig`** — cấu hình build hiện tại (tự sinh bởi
  `idf.py menuconfig`, không sửa tay), gồm flash size, model location,
  model nào được flash...

- **`main/`** — entry point, chỉ init hệ thống và tạo task, không chứa
  logic nghiệp vụ. Chi tiết xem `main/README.md`.

- **`managed_components/`** — component bên thứ 3, do ESP Component
  Manager tự tải theo `main/idf_component.yml`. **Không chỉnh sửa tay.**
  Gồm camera driver, model detect/recognition, driver màn hình, các thư
  viện esp-dl/jpeg đi kèm.

- **`components/`** — component tự viết, chia theo tầng trách nhiệm để
  dễ bảo trì và mở rộng. Mỗi thư mục con có README riêng mô tả chi tiết:
  - `app_config/` — nguồn cấu hình runtime duy nhất (model đang chọn,
    threshold, lưu NVS).
  - `cam_driver/` — thu khung hình từ camera, đẩy vào queue.
  - `display_driver/` — lớp vẽ nguyên thủy lên TFT ST7735.
  - `face_engine/` — quản lý model detect/recognition, enroll/recognize,
    đổi model runtime.
  - `ui_menu/` — giao diện menu trên TFT (chọn model, chỉnh threshold,
    enroll...).
  - `comm_uart/` — nhận/gửi bản tin điều khiển ngắn qua UART.
  - `comm_espnow/` — truyền dữ liệu nặng (ảnh) qua ESP-NOW, có
    chia/ráp gói.
