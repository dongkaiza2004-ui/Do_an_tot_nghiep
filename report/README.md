# Đề Cương Dự Án: Hệ Thống Khóa Cửa Thông Minh Đa Yếu Tố (Face ID + PIN + Vân Tay)

## 1. Mục tiêu dự án

Xây dựng hệ thống kiểm soát ra vào thông minh sử dụng **2 vi điều khiển ESP32-S3** phối hợp với nhau, cho phép mở khóa bằng **1 trong 3 phương thức**: nhận diện khuôn mặt, mã PIN, hoặc vân tay — có ghi log lịch sử kèm ảnh, cảnh báo người lạ, và định hướng mở rộng giám sát/điều khiển từ xa qua web (MQTT).

Hệ thống tách thành **2 node chức năng riêng biệt**, giao tiếp với nhau qua **UART** (lệnh điều khiển) và **ESP-NOW** (ảnh sự kiện), nhằm:
- Tách biệt tác vụ AI (nặng, cần real-time) khỏi tác vụ giao diện người dùng (UI phức tạp, nhiều input) để tránh xung đột tài nguyên (đã thử nghiệm chạy chung LVGL + xử lý bitmap camera trên cùng 1 chip, gây xung đột hiệu năng và được xác định cần tách ra).
- Mỗi node có thể phát triển, kiểm thử, và nâng cấp độc lập.

---

## 2. Kiến trúc tổng quan hệ thống

```k
┌─────────────────────────────┐         UART (lệnh điều khiển)        ┌─────────────────────────────┐
│         NODE A              │────────────────────────────────────── │         NODE B              │
│   (ESP32-S3 + Camera + AI)  │                                       │  (ESP32-S3 + UI + Ngoại vi) │
│                             │───────────────────────────────────────│                             │
│                             │      ESP-NOW (ảnh sự kiện: người lạ / │                             │
│                             │       đăng nhập thành công + metadata)│                             │
└─────────────────────────────┘                                       └─────────────────────────────┘
```

### Node A — Node Camera & AI
| Thành phần | Vai trò |
|---|---|
| Camera (OV series) | Chụp ảnh liên tục cho AI xử lý |
| Model AI (esp-dl: human_face_detect + human_face_recognition) | Phát hiện + nhận diện khuôn mặt, hỗ trợ chuyển đổi giữa nhiều model (MFN/MBF) và chỉnh ngưỡng khớp lúc runtime |
| Màn hình TFT ST7735 (128x160) | Hiển thị **stream trực tiếp liên tục** kèm overlay (khung mặt, tên, FPS) ngay tại chỗ, không qua mạng |
| Cảm biến ánh sáng (LDR/BH1750 — dự kiến) | Tự động bật/tắt đèn hỗ trợ khi trời tối, hoạt động độc lập không cần lệnh từ Node B |
| ESP-NOW (gửi) | Gửi **ảnh chụp sự kiện** (không phải video liên tục) trong 2 trường hợp: phát hiện người lạ, hoặc đăng nhập thành công — kèm metadata (loại sự kiện, ID/tên nếu có, thời gian tương đối) |
| UART (nhận) | Nhận lệnh điều khiển từ Node B: đổi model, đổi ngưỡng, cấu hình camera (brightness/contrast/flip/mirror), thêm/xóa người dùng (enroll theo 2 bước: bắt đầu chờ → xác nhận chụp), bật/tắt hiển thị FPS |

### Node B — Node Giao Diện & Điều Khiển Truy Cập
| Thành phần | Vai trò |
|---|---|
| Màn hình TFT ST7735 (128x160) + LVGL | Toàn bộ giao diện menu: đăng nhập, quản lý người dùng, cấu hình camera, cài đặt mạng, lịch sử mở khóa, mã QR, trợ giúp |
| Bàn phím cảm ứng TTP223 x12 | Vừa dùng **điều hướng menu**, vừa dùng **nhập mã PIN**, tùy màn hình đang mở |
| Cảm biến vân tay AS608 (qua UART riêng) | Module tự so khớp vân tay on-board, Node B chỉ nhận kết quả (ID người dùng khớp) |
| Relay | Điều khiển mở khóa cửa vật lý |
| Loa + I2S | Phát thông báo giọng nói (mở khóa thành công, sai mã, mời quét vân tay...) |
| Mã QR (hiển thị trên LVGL) | (1) Quét để kết nối thiết bị vào WiFi, (2) quét để truy cập giao diện giám sát qua web |
| MQTT (định hướng mở rộng) | Dự kiến publish log/trạng thái lên dashboard web; khả năng subscribe lệnh mở cửa từ xa sẽ được đánh giá ở giai đoạn sau |
| ESP-NOW (nhận) | Nhận ảnh sự kiện (JPEG + metadata) từ Node A để lưu vào lịch sử mở khóa |
| UART (gửi) | Gửi lệnh điều khiển xuống Node A, nhận ACK/kết quả |

---

## 3. Giao thức giao tiếp

### 3.1. UART — Lệnh điều khiển (Node B ⇄ Node A)
Khung truyền dạng: `START | CMD | LEN | PAYLOAD | CHECKSUM | END`

Nhóm lệnh chính (Node B → Node A):
- Cấu hình AI: `SET_MODEL`, `SET_THRESHOLD`
- Cấu hình camera: `SET_BRIGHTNESS`, `SET_CONTRAST`, `SET_FLIP`, `SET_MIRROR`
- Hiển thị: `SHOW_FPS`
- Quản lý người dùng: `ADD_USER_START` → `ADD_USER_CAPTURE` (2 bước: vào chế độ chờ + hiển thị hướng dẫn, sau đó xác nhận chụp khi người dùng đã sẵn sàng trên UI của Node B), `ADD_USER_CANCEL`, `SUB_USER`, `LIST_USER_REQ`

Phản hồi (Node A → Node B): `ACK/NACK`, kết quả thêm/xóa người dùng, danh sách người dùng, báo cáo FPS định kỳ.

### 3.2. ESP-NOW — Ảnh sự kiện (Node A → Node B)

- Gửi ảnh khi có **sự kiện**: phát hiện người lạ, hoặc đăng nhập thành công.
- Ảnh được **nén JPEG** trước khi gửi (tận dụng component `esp_jpeg`/`esp_new_jpeg` đã có sẵn) để giảm kích thước, sau đó **chia gói (fragment)** do giới hạn ~240 byte/gói của ESP-NOW.
- Có cơ chế **chống spam**: 1 khuôn mặt chỉ kích hoạt gửi ảnh tối đa 1 lần mỗi khoảng thời gian nhất định (dự kiến 10 giây), tránh gửi liên tục khi 1 người đứng yên trước camera.
- Kèm metadata: loại sự kiện, ID/tên (nếu có), thời gian — thời gian thực tế được Node B đóng dấu lúc nhận (vì Node B có khả năng đồng bộ giờ qua mạng, Node A thì không).

---

## 4. Logic điều khiển mở khóa (Access Arbiter — Node B)

- **Giai đoạn hiện tại:** mở khóa khi có **1 trong 3** yếu tố xác thực đúng — PIN, vân tay, hoặc khuôn mặt (kết quả nhận diện gửi từ Node A qua UART).
- **Định hướng đánh giá tiếp:** khả năng yêu cầu **kết hợp từ 2 yếu tố trở lên** để tăng bảo mật (ví dụ PIN + vân tay), sẽ quyết định sau khi hoàn thiện bản 1-yếu-tố.
- Mọi lần mở khóa (thành công hay thất bại) đều được ghi vào lịch sử (`history_store`) kèm ảnh (nếu có từ ESP-NOW), phương thức xác thực, và thời gian.

---

## 5. Phương pháp phát triển

1. **Kiến trúc module hóa theo component ESP-IDF**: mỗi chức năng (camera, AI, display, UART, ESP-NOW, keypad, relay, MQTT...) là 1 component riêng, giao tiếp qua interface rõ ràng (queue/hàm gọi), giúp phát triển và kiểm thử độc lập, hạn chế phụ thuộc chéo giữa các phần code.
2. **Tách 2 firmware độc lập** cho Node A và Node B, không dùng chung codebase — mỗi node có vòng đời phát triển, build, flash riêng.
3. **Định nghĩa giao thức trước khi code chi tiết**: giao thức UART và ESP-NOW được thiết kế thành file header dùng chung dạng struct/enum, đảm bảo 2 bên hiểu đúng định dạng gói tin trước khi cắm dữ liệu.
4. **Phát triển theo từng lớp (bottom-up)**: hoàn thiện driver phần cứng (camera, display, keypad, vân tay...) → tích hợp logic xử lý (face engine, access arbiter) → hoàn thiện giao tiếp liên-node → cuối cùng tích hợp UI/UX hoàn chỉnh.
5. **Quyết định kỹ thuật dựa trên thử nghiệm thực tế**: ví dụ việc tách UI khỏi camera xuất phát từ việc thử nghiệm chạy chung LVGL với xử lý bitmap trên cùng 1 chip gây xung đột hiệu năng; việc chuyển từ "stream video qua ESP-NOW" sang "chỉ gửi ảnh sự kiện" xuất phát từ đánh giá giới hạn băng thông thực tế của ESP-NOW.

---

## 6. Hiện trạng & Việc cần làm tiếp

**Đã có:**
- Code camera + display + face detect/recognition cơ bản chạy được trên Node A (enroll qua Serial, nhận diện, vẽ khung).
- Thiết kế giao diện UI hoàn chỉnh cho Node B (LVGL/SquareLine Studio) với các màn hình: đăng nhập, quản lý người dùng, cấu hình camera, mạng, lịch sử mở khóa, mã QR, cài đặt.
- Thiết kế giao thức UART và ESP-NOW (struct/enum chi tiết).

**Cần làm tiếp:**
- [ ] Triển khai cấu trúc component hóa cho Node A (theo kiến trúc đã thiết kế)
- [ ] Triển khai cấu trúc component hóa cho Node B, tích hợp UI có sẵn với keypad/vân tay/relay/loa
- [ ] Cài đặt logic nén JPEG + fragment ESP-NOW, kiểm thử độ tin cậy truyền ảnh sự kiện
- [ ] Cài đặt `access_arbiter` xử lý 3 phương thức xác thực
- [ ] Tích hợp cảm biến ánh sáng + điều khiển đèn tự động trên Node A
- [ ] Thiết kế và tích hợp mã QR (WiFi provisioning + truy cập web)
- [ ] Đánh giá và quyết định hướng MQTT (chỉ giám sát, hay có điều khiển từ xa)
- [ ] Đánh giá bổ sung xác thực kết hợp đa yếu tố (giai đoạn sau)
- [ ] Kiểm thử tích hợp toàn hệ thống (end-to-end)

---

## 7. Phần cứng sử dụng

| Thiết bị | Vai trò |
|---|---|
| ESP32-S3 (Node A) | Xử lý camera + AI + display local |
| ESP32-S3 (Node B) | Xử lý UI + input + relay + audio + kết nối mạng |
| Camera OV series | Thu hình cho Node A |
| Màn hình ST7735 128x160 x2 | 1 cho mỗi node |
| Bàn phím cảm ứng TTP223 x12 | Input cho Node B |
| Module vân tay AS608 | Xác thực vân tay (UART riêng, nối vào Node B) |
| Cảm biến ánh sáng (LDR/BH1750 — dự kiến) | Tự động điều khiển đèn trên Node A |
| Relay | Mở khóa cửa (Node B) |
| Loa + module I2S | Thông báo giọng nói (Node B) |
