# Dự án: Khóa cửa nhận diện khuôn mặt bằng ESP32-CAM + ESP32-S/WROOM

## 1. Mục tiêu dự án

Xây dựng hệ thống mở khóa cửa nhà tự động bằng nhận diện khuôn mặt, phục vụ cho gia đình khoảng **5-10 thành viên**. Ngoài nhận diện khuôn mặt, hệ thống có: mở khóa dự phòng bằng mã số qua bàn phím cảm ứng TTP223 (điều hướng menu và nhập mã luôn qua bàn phím này, kiểu giống bàn phím số điện thoại, không dùng encoder), cấu hình/giám sát qua web và MQTT, hiển thị trạng thái trên màn hình TFT, và tối ưu năng lượng bằng deep sleep.

## 2. Kiến trúc tổng thể: 2 node, tách vai trò rõ ràng

Hệ thống chạy **on-device**, không cần server/Raspberry Pi riêng, chia làm 2 node giao tiếp qua **ESP-NOW**:

- **Node A** — board ESP32-CAM tự thiết kế lại: đọc camera, nhận diện khuôn mặt, TFT gắn liền chỉ hiển thị **hình ảnh camera (preview) + trạng thái nhận diện** (đang quét, nhận ra ai, từ chối...).
- **Node B** — mạch bàn phím + relay: đọc bàn phím số TTP223×12, chạy web server + MQTT, sinh mã QR, ra quyết định điều khiển relay/khóa điện. TFT trên Node B hiển thị **menu lựa chọn** (điều hướng và nhập mã hoàn toàn qua bàn phím TTP223 kiểu số điện thoại, mã QR, các màn hình cấu hình).

Cả 2 node đều dùng chung **module ESP32 thường (ESP32-S/WROOM)**, không dùng ESP32-S3 — lựa chọn có chủ đích để tiết kiệm chi phí, dễ mua, quen thuộc khi làm mạch, kết hợp thêm PSRAM rời (chip `ESP-PSRAM64H`, QSPI) trên cả 2 board thay vì cần chip đắt hơn có PSRAM tích hợp lớn. Vì Node B không phải gánh camera/thẻ nhớ như Node A, nhu cầu GPIO nhẹ hơn nhiều (chỉ I2C 2 chân cho bàn phím + 1 chân relay + tùy chọn TFT), nên dùng chung loại module giới hạn chân vẫn thoải mái.

**Lý do tách 2 node:** không phải vì hiệu năng, mà vì **giới hạn số chân GPIO khả dụng** trên một module ESP32 duy nhất khi phải gánh cả camera lẫn bàn phím/mạng cùng lúc.

**Nguyên tắc phân vai:**
- Node A chỉ lo phần cần camera trực tiếp — chụp, nhận diện, hiển thị hình ảnh camera + trạng thái nhận diện lên TFT của chính nó. Nhờ vậy không cần truyền ảnh/video qua dây/radio giữa 2 node, tránh nút thắt băng thông.
- Node B lo toàn bộ input (bàn phím TTP223×12), toàn bộ mạng (web, MQTT, QR), hiển thị menu lựa chọn trên TFT của chính nó, và ra quyết định điều khiển relay dựa trên kết quả Node A gửi về.
- Mỗi node có TFT và tự vẽ nội dung của mình — không còn kiểu một node gửi lệnh vẽ cho node kia điều khiển màn hình hộ; 2 node chỉ trao đổi dữ liệu (kết quả nhận diện, lệnh yêu cầu xác thực...) qua ESP-NOW theo mô hình **request/response**.

**Nguyên tắc offline-first (quan trọng):** Logic mở/khóa cửa dựa trên kết quả nhận diện phải chạy độc lập, không phụ thuộc web server hay MQTT có kết nối mạng hay không. Web/MQTT chỉ là lớp phụ để giám sát và cấu hình từ xa — mất mạng không được ảnh hưởng tới chức năng mở/khóa cốt lõi.

## 3. Kênh giao tiếp giữa 2 node: ESP-NOW, mô hình request/response

So với UART, ESP-NOW không tốn chân GPIO (dùng radio WiFi tích hợp sẵn), không cần dây nối vật lý, phù hợp với gói tin nhỏ (độ trễ vài chục ms). Vì mỗi node tự vẽ TFT của mình (Node A: camera + trạng thái; Node B: menu), giao tiếp giữa 2 node chỉ cần chở gói tin nhỏ dạng **request/response** theo cả 2 chiều, ví dụ:

- **Node B → Node A (request)**: yêu cầu bắt đầu quét/xác thực khuôn mặt (khi người dùng chọn mục "Mở cửa bằng khuôn mặt" trên menu Node B).
- **Node A → Node B (response)**: trả kết quả nhận diện (nhận ra ai / từ chối) sau khi xử lý xong request.
- **Node B → Node A (request)**: yêu cầu trạng thái hiện tại (đang rảnh, đang bận, lỗi camera...) để Node B tự cập nhật menu nếu cần.
- Node B nhận response, tự quyết định điều khiển relay/khóa điện và tự cập nhật TFT của mình; Node A không cần biết gì về menu hay trạng thái relay.

## 4. Node A — ESP32-CAM redesign (đã có schematic EasyEDA)

### 4.1 Thay đổi so với board AI-Thinker gốc

| Hạng mục | Bản gốc | Bản redesign |
|---|---|---|
| Nạp code | Cần FTDI rời | CH340C + mạch auto-download onboard (DTR/RTS qua Q2, Q3, nút SW1/SW2) |
| PSRAM | Tích hợp sẵn trong module | Chip rời `ESP-PSRAM64H` (QSPI) |
| Camera | OV2640 hàn thẳng | Nối qua connector rời (`CSI_*`), 2 rail nguồn riêng `CSI_1.2V` / `CSI_2.8V` |
| Thẻ nhớ | Micro SD (SDIO 4-bit) | **Bỏ hẳn**, giải phóng GPIO2, 4, 12, 13, 14, 15 |
| Màn hình | Không có | Connector TFT 8 chân |
| Đèn flash | 1 LED | Dàn 8 LED (`LED_FLASH & PCB PRIVATE`) |

### 4.2 Bảng chân module (theo schematic — số chân vật lý → GPIO)

| Chân vật lý | Tên | Chân vật lý | Tên |
|---|---|---|---|
| 1 | GND | 21 | SDO/SD0 |
| 2 | 3V3 | 22 | SDI/SD1 |
| 3 | EN | 23 | IO15 |
| 4 | SENSOR_VP | 24 | IO2 |
| 5 | SENSOR_VN | 25 | IO0 |
| 6 | IO34 | 26 | IO4 |
| 7 | IO35 | 27 | IO16 |
| 8 | IO32 | 28 | IO17 |
| 9 | IO33 | 29 | IO5 |
| 10 | IO25 | 30 | IO18 |
| 11 | IO26 | 31 | IO19 |
| 12 | IO27 | **32** | **NC** *(không bonded, không dùng được)* |
| 13 | IO14 | 33 | IO21 |
| 14 | IO12 | 34 | RXD0 |
| 15 | GND | 35 | TXD0 |
| 16 | IO13 | 36 | IO22 |
| 17 | SHD/SD2 | 37 | IO23 |
| 18 | SWP/SD3 | 38 | GND |
| 19 | SCS/CMD | 39 | EP (GND) |
| 20 | SCK/CLK | | |

### 4.3 Phân bổ GPIO theo chức năng

| Nhóm chức năng | GPIO |
|---|---|
| Camera OV2640/connector (DVP) | 0, 5, 18, 19, 21, 22, 23, 25, 26, 27, 32, 34, 35, 36, 39 |
| SD (SDIO 4-bit) — **đã bỏ, giải phóng cho TFT** | 2, 4, 12, 13, 14, 15 |
| PSRAM (dùng chip rời QSPI riêng nên không chiếm chân module) | 16, 17 |
| UART nạp code/debug | 1 (TXD0), 3 (RXD0) |
| Reset | EN (chân 3) |
| LED Flash (dàn 8 LED, qua transistor điều khiển) | 33 |

### 4.4 Bảng gán chân TFT (chỉ hiển thị hình ảnh camera + trạng thái nhận diện)

| Tín hiệu TFT | GPIO | Ghi chú |
|---|---|---|
| SCLK | GPIO14 | Rảnh sau khi bỏ SD |
| MOSI | GPIO13 | Rảnh sau khi bỏ SD |
| DC | GPIO15 | Strapping nhẹ — đã có trở/transistor bảo vệ (xem 4.5) |
| RESET | GPIO2 | Strapping — đã có trở/transistor bảo vệ, tách riêng khỏi EN (không nối chung reset toàn board) |
| Backlight (TFT_LED) | GPIO4 | Qua transistor Q4/Q7, cho phép bật/tắt hoặc PWM độ sáng |
| CS | Chưa dùng — TFT luôn active vì chỉ có 1 thiết bị SPI trên bus | Dự phòng **GPIO12** nếu sau này cần thêm CS |
| MISO | Không dùng | TFT chỉ hiển thị, không cảm ứng SPI resistive |

### 4.5 Chân strapping — đã có mạch bảo vệ

Ba chân strapping dùng cho TFT (GPIO2, GPIO12 dự phòng, GPIO15) đều đã được thêm **trở/transistor đệm** trên schematic (khớp với các khối Q4/Q7 và điện trở R15, R16, R18, R19, R21, R22 quanh net TFT_RESET/TFT_DC/TFT_LED) để cách ly mức tín hiệu lúc boot. Vẫn nên đo thực tế mức áp trên các chân này lúc cấp nguồn/reset để xác nhận mạch hoạt động đúng trước khi hàn hàng loạt.

Riêng **GPIO0** là chân chọn chế độ boot (bootloader vs chạy chương trình), bản chất khác GPIO2/12/15 nên không xử lý bằng trở/transistor đệm theo cùng cách được. May là GPIO0 đã cố định vai trò XCLK cho camera, không dùng cho TFT nên không phát sinh rủi ro mới.

## 5. Node B — bàn phím, relay (schematic EasyEDA `ESP32_Control`, đã có)

### 5.1 Nền tảng mạch

Dùng lại cùng khối mạch nền như Node A: module ESP32-S/WROOM (cùng kiểu chân GND1...EP39 như bảng 4.2), PSRAM rời `ESP-PSRAM64H` (QSPI), CH340C + mạch auto-download onboard.

### 5.2 Bàn phím TTP223 × 12 qua 2× PCF8574T

- Dùng **2× PCF8574T** (không phải PCF8575).
- Bus I2C dùng chung: `PCF_SDA`, `PCF_SCL` — 2 chip đặt địa chỉ khác nhau qua chân A0/A1/A2.
- Có đường `PCF_INT` (nối chân `INT#` của cả 2 PCF8574) — cho phép đọc bàn phím theo kiểu **ngắt (interrupt-driven)** thay vì polling liên tục, tiết kiệm CPU/năng lượng.
- 12 module TTP223 (`U17, U16, U10, U15, U12, U13, U14, U1, U11, U2, U9, U18`) nối vào các chân P0-P7 của 2 PCF8574, khớp với 0-9 + enter + back.

### 5.3 Relay mở khóa

- Relay module `SRD-05V` (K1), điều khiển qua transistor Q5.
- Diode chống dòng ngược (D1, D2) bảo vệ transistor khi relay ngắt.
- 2 LED báo trạng thái relay (LED1, LED2).
- Ngõ ra nối cực NO/COM của relay ra 2 domino vít (`KF128-3.81-2P`) để đấu dây khóa điện thật — tách biệt mạch điều khiển (điện áp thấp) khỏi mạch tải khóa (điện áp/dòng có thể cao hơn).
- Tín hiệu điều khiển từ MCU vào relay đặt tên net `IN_Relay`.

### 5.4 Ghi chú

- **Connector TFT trên Node B**: là màn hình chính thức của Node B, dùng để hiển thị **menu lựa chọn** — điều hướng và nhập mã hoàn toàn qua bàn phím TTP223×12 (kiểu bàn phím số điện thoại, không dùng encoder), hiện mã QR, các màn hình cấu hình. Firmware/logic vẽ menu lên TFT Node B chưa bắt đầu code.
- Gán GPIO cụ thể cho `PCF_SDA`, `PCF_SCL`, `PCF_INT`, `IN_Relay`, và connector TFT trên Node B chưa trích xuất được chính xác từ bản PDF phẳng (mất routing) — cần đối chiếu trực tiếp trong EasyEDA trước khi fab.
- Đã bỏ hẳn encoder khỏi thiết kế — không xuất hiện trong schematic Node B và không còn nằm trong kế hoạch phần cứng.

## 6. Phần cứng

| Thiết bị | Vai trò | Trạng thái |
|---|---|---|
| ESP32-CAM/module ESP32-S-WROOM (Node A) | Nhận diện + hiển thị preview | Đã có schematic, chờ fab |
| Module ESP32-S-WROOM (Node B) | Bàn phím, web/MQTT, relay | Đã có schematic, chờ fab |
| CH340C (×2, onboard) | Nạp code không cần FTDI rời | Đã tích hợp vào schematic |
| ESP-PSRAM64H (×2, QSPI) | PSRAM rời cho cả 2 node | Đã tích hợp vào schematic |
| Màn hình TFT (SPI) ×2 | Node A: hiển thị camera + trạng thái nhận diện; Node B: hiển thị menu lựa chọn | Cần mua |
| TTP223 × 12 | Bàn phím cảm ứng số 0-9, enter, back | Cần mua |
| PCF8574T × 2 | Mở rộng chân I2C cho 12 phím TTP223 | Cần mua |
| Relay module SRD-05V | Đóng/ngắt khóa điện | Cần mua |
| Khóa điện (solenoid/khóa từ) | Chốt cửa vật lý | Cần mua |
| MOSFET/load switch | Cắt nguồn vật lý camera khi deep sleep | Cần mua, mới ở mức ý tưởng |
| Nút bấm vật lý | Đánh thức hệ thống khi đang deep sleep | Cần mua, mới ở mức ý tưởng |

## 7. Phần mềm

### 7.1 Node A (nhận diện khuôn mặt)

Base code lấy từ repo mã nguồn mở: [robotzero1/esp32cam-access-control](https://github.com/robotzero1/esp32cam-access-control), dùng thư viện nhận diện khuôn mặt có sẵn của Espressif (`fd_forward.h` cho phát hiện, `fr_forward.h`/`fr_flash.h` cho nhận diện và lưu trữ), dựa trên mô hình **MTMN** (detection) và **MobileFaceNet** (recognition) đã huấn luyện sẵn — không cần tự train lại. Đã tùy chỉnh:

- Đổi thông tin WiFi thành placeholder rõ ràng để dễ điền.
- Tăng `FACE_ID_SAVE_NUMBER` từ 7 lên **10** người.

Cấu trúc project (định dạng PlatformIO):
```
esp32cam-pio/
├── platformio.ini          # ghim platform = espressif32@3.5.0 để tương thích fd_forward.h/fr_forward.h bản cũ
├── partitions.csv          # bảng phân vùng flash, chừa đủ chỗ lưu face ID
├── src/
│   └── main.cpp             # code chính (đổi tên từ .ino gốc)
└── include/
    ├── camera_pins.h         # định nghĩa chân camera
    └── camera_index.h        # giao diện web (HTML nhúng) để enroll/quản lý khuôn mặt
```

Sắp làm thêm (chưa bắt đầu code): driver TFT (TFT_eSPI hoặc LovyanGFX — nhẹ, phù hợp ESP32 gốc còn ít RAM) để vẽ hình ảnh camera + trạng thái nhận diện, ưu tiên **partial redraw** (chỉ vẽ lại vùng thay đổi) thay vì vẽ lại toàn màn hình mỗi frame; và giao tiếp ESP-NOW theo mô hình request/response với Node B (nhận request bắt đầu quét, trả response kết quả nhận diện).

### 7.2 Node B (input, mạng, điều khiển) — chưa bắt đầu code

- Đọc bàn phím TTP223×12 qua PCF8574 (I2C) — dùng để cả nhập mã số **và** điều hướng menu (kiểu bàn phím số điện thoại: ví dụ phím số để di chuyển/chọn, phím "enter"/"back" để xác nhận/quay lại).
- Vẽ và điều hướng **menu lựa chọn** trên TFT riêng của Node B, điều khiển hoàn toàn bằng bàn phím TTP223, không dùng encoder.
- Web server nội bộ để cấu hình (không phụ thuộc internet ngoài).
- MQTT client (tùy chọn, dùng cho giám sát/log từ xa, không nằm trên đường xử lý mở khóa).
- Sinh mã QR hiển thị ngay trên TFT của Node B để truy cập web nội bộ bằng cách quét mã, không cần gõ IP.
- Gửi **request** yêu cầu Node A bắt đầu quét/xác thực khi người dùng chọn mục tương ứng trên menu; nhận **response** kết quả nhận diện từ Node A qua ESP-NOW, ra quyết định điều khiển relay/khóa điện.
- Phối hợp deep sleep với Node A: quyết định khi nào cả 2 node cùng ngủ, ai đánh thức ai khi có người bấm nút hoặc bấm phím TTP223.

## 8. Trạng thái hiện tại

✅ Đã xong:
- Chốt kiến trúc 2 node (ESP32-S/WROOM + ESP-NOW), nguyên tắc offline-first.
- Môi trường build PlatformIO cho Node A chạy được, firmware build thành công.
- Tùy chỉnh code Node A: tăng giới hạn lưu khuôn mặt lên 10 người.
- Schematic EasyEDA Node A (redesign ESP32-CAM, có TFT) và Node B (bàn phím + relay, có connector TFT dự phòng).
- Bảng gán chân TFT cho Node A.

⏳ Đang chờ / chưa làm:
- Đối chiếu GPIO cụ thể cho các net PCF_SDA/PCF_SCL/PCF_INT/IN_Relay/TFT dự phòng trên Node B trong EasyEDA.
- Đặt fab cả 2 board.
- Mua linh kiện: TFT, PCF8574T×2, relay, khóa điện, MOSFET/load switch, nút đánh thức.
- Nạp firmware Node A lên phần cứng thật, test enroll khuôn mặt qua giao diện web.
- Viết code Node B (bàn phím TTP223 dùng cho cả nhập mã lẫn điều hướng menu, web/MQTT, QR, relay, ESP-NOW).
- Viết driver TFT + overlay UI cho Node A, giao tiếp ESP-NOW 2 chiều giữa 2 node.
- Thiết kế mạch quản lý năng lượng (deep sleep, cắt nguồn camera qua MOSFET) — mới ở mức ý tưởng.
- Tối ưu tốc độ/độ chính xác nhận diện (điều chỉnh `min_face`, `pyramid_times`, ánh sáng, số mẫu enroll).

## 9. Bước tiếp theo đề xuất

1. Đối chiếu và chốt GPIO cụ thể cho Node B trực tiếp trong EasyEDA (PCF_SDA, PCF_SCL, PCF_INT, IN_Relay, connector TFT).
2. Đặt fab 2 board (Node A + Node B).
3. Mua đủ linh kiện còn thiếu ở mục 6.
4. Trong lúc chờ fab: nạp firmware Node A hiện có lên ESP32-CAM gốc (AI-Thinker) qua FTDI để test luồng enroll/nhận diện trước, độc lập với phần cứng redesign.
5. Sau khi có board thật: viết driver TFT (partial redraw) và giao tiếp ESP-NOW cho Node A; viết toàn bộ firmware Node B.
6. Tích hợp 2 node, test end-to-end luồng nhận diện → hiển thị trạng thái → điều khiển relay.
7. Đấu nối relay + khóa điện thật, kiểm thử an toàn điện trước khi lắp lên cửa.
8. Thiết kế và tích hợp mạch/logic deep sleep sau khi hệ thống chạy ổn định ở chế độ luôn bật.
