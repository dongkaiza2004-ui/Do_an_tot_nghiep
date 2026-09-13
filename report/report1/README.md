# project_face_cam — Ghi chú tiến độ (phần giao diện TFT + hệ thống)

Tài liệu này ghi lại những gì đã hoàn thành, cách làm, và hướng tiếp theo — tính từ giai đoạn thiết kế giao diện trên màn hình TFT ST7735 (128x160).

---

## 1. Đã làm được gì

### 1.1. Giao diện (UI)
- Thiết kế toàn bộ giao diện bằng **SquareLine Studio** (LVGL 8.3.11), độ phân giải 128x160, màu 16-bit.
- Đã dựng đủ **16 màn hình**, tổ chức theo dạng cây menu phân cấp (hub → sub-screen):

```
Home (ui_GUI)
├── Setting
│   ├── Camera settings
│   ├── Change password (2 tab: mật khẩu mở cửa / PIN admin)
│   └── More (otherscreen)
│       ├── Network (nhập SSID, hiện QR)
│       ├── CPU (thông số hệ thống)
│       └── Help (hướng dẫn tĩnh)
├── Face recognition (live view camera)
├── Keypad (mở cửa bằng mã số)
├── Fingerprint (mở cửa bằng vân tay)
├── User
│   ├── Add user (nhập tên/ID/code, đăng ký vân tay hoặc khuôn mặt)
│   ├── Sub user (xóa user theo danh sách vân tay/khuôn mặt)
│   └── Unlock history (lịch sử mở khóa)
└── QR (hiển thị QR để quét)
```

### 1.2. Cấu trúc project (ESP-IDF component)
```
project_face_cam/
├── main/                      # chỉ init he thong + tao task, khong chua logic
├── managed_components/        # tu dong (esp32-camera, human_face_detect/recognition,
│                               #   esp_lcd_st7735, lvgl, esp_lvgl_port...)
└── components/                # component tu viet
    ├── comm_uart/              # doc UART tu node ban phim, dich thanh su kien dieu huong
    ├── ui_menu/                # UI SquareLine export + logic dieu huong (ui_nav.c)
    └── app_config/             # sys_monitor.c - doc thong so he thong hien len man CPU
```

### 1.3. Điều hướng bằng UART (không cảm ứng, không GPIO vật lý trên board này)

- Board TFT nhận tín hiệu điều hướng qua **UART từ 1 node bàn phím vật lý riêng** (node kia có phím 0-9, Enter, Back, và tự xử lý multi-tap kiểu Nokia — gửi thẳng ký tự đã hoàn chỉnh sang, không phải số thô).
- Sơ đồ phím khi đang ở chế độ menu:
  - `2` = lên/trước · `8` = xuống/sau · `4` = giảm giá trị · `6` = tăng giá trị
  - `Enter` = chọn/xác nhận · `Back` = quay lại/xóa ký tự
- Khi focus vào 1 ô cần nhập (tên, mã, mật khẩu...), board chuyển sang **chế độ nhập ký tự**: mọi byte nhận được ghi thẳng vào ô, không còn là lệnh điều hướng, cho tới khi bấm Enter (xác nhận) hoặc Back (xóa).

### 1.4. Hệ thống điều hướng (`ui_nav.c`)

Thiết kế theo hướng **table-driven**, không hard-code từng màn:

- Mỗi màn hình đăng ký 1 danh sách các "item" (nút/label/slider/switch/dropdown) theo đúng thứ tự duyệt.
- `2/8` duyệt qua từng item; `Enter` xử lý theo đúng loại widget đang focus (click nút, mở/đóng dropdown, bật/tắt switch, tăng/giảm slider, hoặc bật chế độ nhập ký tự nếu là ô nhập liệu).
- Chuyển màn hình dựa trên sự kiện `LV_EVENT_SCREEN_LOADED` của LVGL (không tự đoán thời điểm chuyển màn bằng cách so sánh thủ công — tránh sai lệch do animation chuyển màn mất ~200ms).
- Cơ chế **subpanel** dùng chung cho các màn có khung con ẩn/hiện bên trong (ví dụ 2 tab của "Change password", hoặc khung xem trước khuôn mặt trong "Add user") — không cần viết logic riêng cho từng trường hợp.

### 1.5. Giám sát hệ thống (màn CPU)

Cập nhật định kỳ mỗi 1 giây bằng `lv_timer_create()` (không cần tạo FreeRTOS task riêng — tận dụng task sẵn có của LVGL), chỉ tính toán khi màn CPU đang được xem (tránh lãng phí tài nguyên):

- **CPU Load (%)** — tính qua `uxTaskGetSystemState()`, dựa trên tỉ lệ thời gian rảnh của 2 task IDLE (ESP32-S3 2 nhân).
- **RAM nội bộ (SRAM) còn trống** — `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`.
- **PSRAM còn trống** — `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`.
- **Min heap SRAM thấp nhất từng ghi nhận** — `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` (tách riêng khỏi PSRAM để phát hiện rò rỉ bộ nhớ đúng vùng dễ cạn nhất).
- **Flash (SPIFFS)** — dung lượng đã dùng/tổng của riêng partition `storage` (nơi chứa `face.db`), qua `esp_spiffs_info("storage", ...)` — phản ánh đúng mức dùng thực tế (tăng theo số khuôn mặt đã enroll), thay vì hiện tổng dung lượng chip cố định không đổi.
- **Uptime** — từ `esp_timer_get_time()`.
- **Firmware version** — từ `esp_app_get_description()`.
- **Nhiệt độ chip** — cảm biến nội bộ ESP32-S3 (`driver/temperature_sensor.h`).

---

## 2. Vấn đề đã gặp và đã xử lý (để tránh lặp lại)

- **Component `main` tự thấy mọi header, component tự viết thì không** — mỗi component mới phải khai đủ `PRIV_REQUIRES` cho từng thư viện được include (đã gặp thiếu `esp_lvgl_port`, `lvgl`, `spiffs` nhiều lần).
- **Linkage C/C++**: header của file `.c` được include từ `main.cpp` (C++) phải bọc `extern "C"`, nếu không linker báo `undefined reference` dù hàm tồn tại thật.
- **`resync` sau mỗi lần Enter**: ban đầu code reset focus về item đầu tiên sau MỌI lần bấm Enter (kể cả khi không đổi màn) — sửa bằng cách chỉ đồng bộ lại khi có sự kiện `LV_EVENT_SCREEN_LOADED` thật sự.
- **Sai lệch wiring trong `Change password`**: 2 tab bấm nhầm mở panel của nhau (do thao tác trong SquareLine) — đã đối chiếu code thật và sửa lại đúng.
- **`partitions.csv` không nhận comment kiểu `//`** — chỉ chấp nhận `#` ở đầu dòng riêng.

---

## 3. Hướng tiếp theo (phục vụ những gì đã làm ở trên)

Khung UI + điều hướng + giám sát hệ thống đã chạy ổn — các bước tiếp theo là **nối phần cứng/logic thật vào đúng những màn đã có sẵn**, không cần thiết kế lại giao diện:

1. **Face recognition**: nối `esp_camera` + `human_face_detect`/`human_face_recognition` vào màn `Facerecognition` (hiện đang là khung trống) — cần thêm `lv_canvas`/`lv_img` để hiển thị khung hình camera trực tiếp.
2. **Add/Sub user thật**: nối logic enroll/xóa khuôn mặt (đã có sẵn code gốc `HumanFaceRecognizer::enroll`) vào 2 nút "finger/face" trên màn `Adduser`, và danh sách dropdown trên `Subuser`.
3. **Keypad mở cửa**: xử lý mã nhập ở `ui_inputopencode` thành lệnh mở khóa thật (so khớp với mã đã lưu).
4. **Vân tay**: tích hợp module cảm biến vân tay thật (UART) vào màn `fingerprint` và luồng enroll ở `Adduser`.
5. **Điều khiển khóa cửa**: thêm 1 lớp điều phối chung (`access_control`) nhận kết quả từ mặt/vân tay/keypad, quyết định mở relay/servo, ghi log — tránh 3 nguồn unlock method tự ý gọi thẳng phần cứng khóa.
6. **Lịch sử mở khóa**: lưu log (ai, lúc nào, bằng cách nào) vào SPIFFS/NVS, hiển thị lên màn `UnlockHistory` (hiện đang trống).
7. **WiFi + QR**: hoàn thiện `ConnectingSSID` (nhập SSID/mật khẩu WiFi) và sinh QR thật cho `QRconnectnetwork`/`QRcode` (hiện là khung trống, cần thư viện tạo QR bitmap).
8. **ESP-NOW**: truyền ảnh/dữ liệu nặng (khung hình có khung nhận diện) sang node khác — phần này chưa bắt đầu.
9. **Mount SPIFFS trong bản chạy chính thức**: hiện tại `app_main.cpp` bản test UI chưa gọi `init_spiffs()` (chỉ có trong `main.cpp` gốc) — cần ghép lại khi tích hợp `face_engine`.

Thứ tự ưu tiên gợi ý: (1) → (2) vì đây là lõi chức năng chính của thiết bị (nhận diện + quản lý user), các mục còn lại (keypad, vân tay, WiFi, ESP-NOW) có thể làm song song hoặc sau khi lõi chạy ổn định.
