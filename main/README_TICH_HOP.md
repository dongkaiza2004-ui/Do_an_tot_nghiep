# Hướng dẫn tích hợp lớp giao tiếp camera vào `door_lock` (Node B)

## 1. Copy file vào đâu

```
door_lock/                          <- project esp-matter/examples/door_lock hiện có của bạn
├── components/                     <- COPY TOÀN BỘ thư mục components/ của gói này vào đây
│   ├── protocol/
│   ├── comm_uart/
│   ├── comm_espnow/
│   ├── access_events/
│   └── access_dashboard/
│
└── main/                           <- Copy 4 file trong main_patch/ vào đây
    ├── access_gateway.h            <- file MỚI
    ├── access_gateway.cpp          <- file MỚI
    ├── app_main.cpp                <- GHI ĐÈ file cũ (đã giữ nguyên 100% code gốc,
    │                                   chỉ thêm phần tích hợp, đánh dấu rõ bằng comment)
    └── CMakeLists.txt              <- GHI ĐÈ file cũ (chỉ thêm PRIV_REQUIRES)
```

ESP-IDF tự động nhận diện thư mục `components/` nằm cạnh `main/` mà không cần sửa
`CMakeLists.txt` gốc ở thư mục gốc project.

## 2. BẮT BUỘC kiểm tra/chỉnh trước khi build

Đây là những chỗ mình phải đặt giá trị placeholder hoặc giả định vì không có
đủ thông tin — **bỏ qua bước này thì code sẽ compile được nhưng KHÔNG chạy
đúng thực tế**:

| # | Việc cần làm | Ở đâu |
|---|---|---|
| 1 | Chọn 2 chân GPIO còn trống trên board Node B (chưa bị `esp_bsp_devkit` dùng cho nút/LED) để nối UART, thay cho giá trị ví dụ 17/18 | `main/app_main.cpp`, hằng số `kFaceLinkUartTxPin`/`kFaceLinkUartRxPin` |
| 2 | Đấu dây **chéo**: TX Node B → RX camera (GPIO47), RX Node B → TX camera (GPIO21), chung GND | Phần cứng |
| 3 | Sau khi flash Node B, đọc log để lấy MAC Wi-Fi STA thật (dòng `comm_espnow init OK. MAC STA cua Node B...`), rồi cập nhật vào `s_peer_mac`/tham số `comm_espnow_init(peer_mac)` bên **project camera** | `project_face_cam/main/app_main.cpp` |
| 4 | **Cố định kênh Wi-Fi của router** về một giá trị (ví dụ kênh 1), rồi thêm `esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE)` ngay sau `esp_wifi_start()` bên project camera. Nếu bỏ qua bước này, ESP-NOW sẽ **im lặng không nhận được gì** dù Matter/Wi-Fi của Node B vẫn hoạt động bình thường — rất dễ nhầm là lỗi khác khi debug | `project_face_cam/main/app_main.cpp` (phần `comm_espnow_init`) + cấu hình router |
| 5 | Xác nhận lại cách ánh xạ `user_id` (camera) ↔ `userIndex` (Matter). Code hiện dùng ánh xạ tạm 1-1 (`user_id == userIndex`, giới hạn 1..6) — đúng như tài liệu thiết kế gốc đã ghi là phần còn bỏ ngỏ | `main/access_gateway.cpp`, hàm `MapFaceUserIdToMatterIndex` |
| 6 | Nếu build lỗi thiếu `OperationSourceEnum::kBiometric` (tuỳ phiên bản SDK connectedhomeip đang dùng), đổi tạm sang `OperationSourceEnum::kRemote` (chắc chắn có sẵn, đã dùng trong `door_lock_callbacks.cpp`) | `main/access_gateway.cpp`, hàm `DoScheduledUnlock` |

## 3. Luồng hoạt động sau khi tích hợp

```
Camera --UART (RSP_FACE_MATCHED/UNKNOWN/LIVENESS_FAIL)--> comm_uart --> access_gateway
                                                                            │
                                                                            ├─► DoorLockStorage::GetUser() kiểm tra hợp lệ
                                                                            ├─► PlatformMgr().ScheduleWork() → BoltLockManager::Unlock()
                                                                            │        → đồng bộ Matter Door Lock cluster (Google/Apple Home)
                                                                            └─► access_events_publish() → access_dashboard (lịch sử)

Camera --ESP-NOW (ảnh JPEG phân mảnh)--> comm_espnow --> access_dashboard (cache RAM, đối chiếu theo event_id)

Web Dashboard/App Qt --HTTP--> access_dashboard:
  GET  /api/status
  GET  /api/history?limit=N
  GET  /api/image/{event_id}
  GET  /api/users
  POST /api/users/start   {"user_id":N,"name":"..."}
  POST /api/users/capture
  POST /api/users/cancel
  DELETE /api/users/{id}
  POST /api/camera        {"brightness":N,"contrast":N,"flip":bool,"mirror":bool}
  POST /api/liveness      {"enable":bool}
  POST /api/recognition   {"model_id":N,"threshold":F}
```

Lưu ý: dashboard **không có** endpoint khoá/mở cửa trực tiếp — việc đó thuộc
về Matter (Google Home/Apple Home), tránh 2 đường điều khiển khoá giẫm lên nhau.

## 4. Vì sao `access_gateway` nằm trong `main/` chứ không phải `components/`

Đây không phải lựa chọn tổ chức tuỳ ý — là giới hạn thật của ESP-IDF: file
`bolt_lock_manager.h` và `door_lock_storage.h` chỉ được khai báo trong
`PRIV_INCLUDE_DIRS` (riêng tư) của component `main` (xem `main/CMakeLists.txt`
gốc), nên một component khác đặt trong `components/` **không thể** include
được 2 header này. `access_gateway` là mã duy nhất cần truy cập trực tiếp
`BoltLockManager`/`DoorLockStorage` để quyết định mở khoá, nên buộc phải nằm
cùng component `main`. Ngược lại, `access_dashboard` (HTTP server) hoàn toàn
không đụng tới 2 header đó — nó chỉ nói chuyện với `access_events` và
`comm_espnow` — nên tách thành component riêng bình thường được.

## 5. Các giới hạn đã biết (cố ý, không phải bug bỏ sót)

- **ESP-NOW không có ACK/gửi lại mảnh rớt** — đúng như thiết kế gốc mô tả kênh
  ảnh là "best-effort". `comm_espnow` tự huỷ ảnh sau 4 giây nếu không nhận đủ
  mảnh, ghi log cảnh báo kèm số mảnh đã nhận được.
- **`comm_uart_unregister_listener` có một khoảng race rất hẹp** với task đọc
  UART đang dispatch — chấp nhận được ở tốc độ khung hiện tại của hệ thống,
  nhưng không phải tuyệt đối an toàn 100% nếu sau này tăng tần suất lệnh lên
  rất cao. Đã ghi rõ trong comment ở `comm_uart.h`.
- **Bộ nhớ đệm ảnh của dashboard chỉ ở RAM** (8 ảnh gần nhất), mất khi mất điện
  — đúng phạm vi "xem nhanh" đã bàn, không phải lưu trữ lâu dài.
- **Chưa có kiểm tra khung giờ truy cập theo từng người dùng** — `DoorLockStorage`
  hiện tại không có trường này; đã đánh dấu `TODO` trong `access_gateway.cpp`
  nếu sau này cần bổ sung.
- Lỗi khởi tạo `comm_uart`/`comm_espnow`/`access_dashboard` trong `app_main.cpp`
  chỉ được **log**, không `ABORT_APP_ON_FAILURE` như phần lõi Matter — chủ ý để
  thiết bị vẫn hoạt động như một khoá Matter bình thường ngay cả khi dây UART
  cắm sai hoặc board camera chưa sẵn sàng, thay vì khiến cả thiết bị không boot
  được.
