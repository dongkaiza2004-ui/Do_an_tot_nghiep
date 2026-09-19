# Các mô hình AI nhận diện khuôn mặt của Espressif (ESP-DL) trên ESP32-S3

> Tài liệu tổng hợp về **1 mô hình phát hiện khuôn mặt** (`HumanFaceDetect`) và **2 mô hình trích xuất đặc trưng để nhận dạng khuôn mặt** (`MFN_S8_V1`, `MBF_S8_V1`) do Espressif cung cấp thông qua thư viện ESP-DL, chạy trên nền ESP-IDF.
> Ngày tổng hợp: 18/09/2026.

---

## 1. Phạm vi và cách hiểu đề bài

Espressif không phát hành các mô hình này trực tiếp trong lõi ESP-IDF. Chúng nằm trong thư viện **ESP-DL** (Espressif Deep Learning Library), được đóng gói thành các component trên ESP Component Registry, và được ESP-WHO sử dụng như một framework ứng dụng. Toàn bộ chạy trên ESP-IDF (ESP-DL yêu cầu ESP-IDF phiên bản 5.3 trở lên).

Trong mã nguồn của dự án, có hai lớp được sử dụng:

| Lớp C++ | Chức năng | Thành phần AI |
|---|---|---|
| `HumanFaceDetect` | Phát hiện khuôn mặt, trả về khung bao và 5 điểm mốc | 1 mô hình phát hiện (mặc định là cặp MSR + MNP) |
| `HumanFaceRecognizer` (dùng `HumanFaceFeat` bên trong) | Trích xuất vector đặc trưng, so khớp với cơ sở dữ liệu | 2 mô hình: `MFN_S8_V1` và `MBF_S8_V1` |

Ngoài mô hình mặc định, ESP-DL còn cho phép chọn hai biến thể phát hiện một giai đoạn là `ESPDET_PICO_224_224_FACE` và `ESPDET_PICO_416_416_FACE`. Các biến thể này được trình bày ở mục 3.3 để tài liệu đầy đủ.

---

## 2. Quy trình xử lý tổng thể

Nhận dạng khuôn mặt được xây dựng trực tiếp trên kết quả của phát hiện khuôn mặt. Quy trình gồm bốn bước:

1. **Phát hiện**: mô hình phát hiện tìm khuôn mặt trong khung hình, trả về khung bao, điểm tin cậy và 5 điểm mốc (hai mắt, đầu mũi, hai khóe miệng).
2. **Căn chỉnh**: năm điểm mốc được dùng để biến đổi hình học vùng mặt về ảnh chuẩn **112 × 112 RGB**, sao cho mắt và miệng luôn ở cùng vị trí. Nhờ đó vector đặc trưng ít phụ thuộc vào độ nghiêng và vị trí của đầu.
3. **Trích xuất đặc trưng**: mạng `HumanFaceFeat` biến ảnh đã căn chỉnh thành một vector đặc trưng cố định độ dài.
4. **So khớp**: vector được so sánh với các vector đã đăng ký bằng **độ tương đồng cosine**. Nếu điểm cao nhất vượt ngưỡng (mặc định **0,5**) thì khuôn mặt được xác định là người tương ứng, ngược lại là "không xác định".

Cơ sở dữ liệu khuôn mặt là một tệp lưu trong bộ nhớ flash (FATFS hoặc SPIFFS). Mỗi khuôn mặt đăng ký được lưu dưới dạng vector đặc trưng kèm một mã số nguyên tự tăng. Dữ liệu được giữ lại sau khi khởi động lại. Khi có nhiều khuôn mặt trong khung hình, bộ nhận dạng chọn **khuôn mặt lớn nhất** cho cả đăng ký lẫn nhận dạng.

---

## 3. Mô hình phát hiện khuôn mặt: `HumanFaceDetect`

### 3.1. Kiến trúc mặc định: MSR + MNP (hai giai đoạn)

Mô hình mặc định `MSR_S8_V1 + MNP_S8_V1` là kiến trúc **tầng (cascade) hai giai đoạn**. Tài liệu chính thức của Espressif mô tả ngắn gọn: giai đoạn đầu `MSR_S8_V1` đề xuất các ứng viên, sau đó mỗi ứng viên đi qua giai đoạn hai `MNP_S8_V1`.

Theo bài hướng dẫn Edge-AI with ESP32-S3 của Espressif, hai giai đoạn hoạt động như sau:

- **MSR** nhận toàn bộ khung hình đã thu về kích thước 120 × 160 (cao × rộng) và quét nhanh để tạo danh sách vùng ứng viên. Giai đoạn này cố ý ưu tiên tốc độ hơn độ chính xác, nhiệm vụ chính là loại bỏ phần lớn nền.
- **MNP** cắt từng vùng ứng viên, đưa về 48 × 48, rồi đánh giá chính xác hơn xem vùng đó có phải khuôn mặt không. Giai đoạn này tinh chỉnh khung bao, lọc dương tính giả và cho ra 5 điểm mốc.

```
Khung hình (120×160) ──► MSR ──► vùng ứng viên ──► cắt 48×48 ──► MNP ──► khung bao + điểm tin cậy + 5 điểm mốc
```

**Lưu ý về nguồn.** Ý tưởng "thô trước, tinh sau" và đầu ra 5 điểm mốc tương tự họ mạng **MTCNN** (Zhang và cộng sự, 2016), gồm ba tầng CNN nhỏ xử lý từ thô đến tinh. Đây là nhận xét so sánh về mặt ý tưởng. Espressif không công bố rằng MSR/MNP là một biến thể của MTCNN, cũng không công bố sơ đồ lớp chi tiết hay bài báo khoa học riêng cho hai mô hình này.

### 3.2. Đặc điểm kỹ thuật

- **Lượng tử hóa 8 bit** (hậu tố `S8`). Cả ba biến thể phát hiện đều dùng số nguyên 8 bit và chạy hoàn toàn trên CPU của ESP32-S3 thông qua bộ suy luận ESP-DL.
- **Kết quả trả về** cho mỗi khuôn mặt (`dl::detect::result_t`):
  - `box[4]`: tọa độ khung bao `[x1, y1, x2, y2]` theo pixel của ảnh đầu vào;
  - `score`: điểm tin cậy từ 0,0 đến 1,0, chỉ giữ kết quả vượt ngưỡng (mặc định 0,5);
  - `keypoint[10]`: năm điểm mốc, mỗi điểm là một cặp `(x, y)`.
- **Vị trí lưu mô hình** có ba lựa chọn qua `menuconfig`: flash dạng rodata, phân vùng flash (phân vùng phải tên `human_face_det` và đủ lớn), hoặc thẻ SD.

### 3.3. Ba biến thể phát hiện

| Biến thể | Loại | Kích thước đầu vào (cao × rộng × kênh) | Độ trễ mô hình trên ESP32-S3 | mAP50-95 (tập kiểm định riêng) |
|---|---|---|---|---|
| `MSR_S8_V1` + `MNP_S8_V1` (mặc định) | Hai giai đoạn | 120 × 160 × 3, sau đó 48 × 48 × 3 cho mỗi ứng viên | MSR 33,1 ms; MNP 5,8 ms mỗi ứng viên | 0,367 |
| `ESPDET_PICO_224_224_FACE` | Một giai đoạn | 224 × 224 × 3 | 131,6 ms | 0,504 |
| `ESPDET_PICO_416_416_FACE` | Một giai đoạn | 416 × 416 × 3 | 437,0 ms | 0,598 |

Trên ESP32-P4, các số liệu tương ứng thấp hơn đáng kể: MSR 13,1 ms, MNP 2,4 ms, ESPDet-Pico 224 là 49,6 ms và ESPDet-Pico 416 là 185,9 ms. Với một khuôn mặt trong khung hình trên ESP32-S3, tổng thời gian phát hiện của cặp MSR + MNP vào khoảng 37 đến 40 ms mỗi khung hình.

**Về họ ESPDet-Pico.** Đây là mô hình phát hiện một giai đoạn do Espressif phát triển trong dự án `esp-detection`, dựa trên **Ultralytics YOLOv11**. Theo mô tả trong dự án, mô hình có khoảng 0,36 triệu tham số, dùng tích chập tách theo chiều sâu (depthwise separable convolution) ở phần trích xuất đặc trưng và phát hiện ở ba mức tỷ lệ P3/8, P4/16, P5/32. Với bài toán phát hiện mèo, ESPDet-Pico 224 đạt mAP(0,5:0,95) tương đương YOLOv11n 80 lớp (2,6 triệu tham số, ảnh 640).

### 3.4. Ưu điểm và hạn chế

| Biến thể | Ưu điểm | Hạn chế |
|---|---|---|
| MSR + MNP | Nhanh nhất (khoảng 37 ms); cho sẵn 5 điểm mốc để căn chỉnh; đầu vào nhỏ nên tiết kiệm bộ nhớ | mAP thấp nhất (0,367) nên độ chính xác định vị khung bao kém hơn |
| ESPDet-Pico 224 | Cân bằng giữa độ chính xác và tốc độ; mAP 0,504 | Chậm hơn khoảng 3,5 lần so với MSR + MNP |
| ESPDet-Pico 416 | Chính xác nhất (mAP 0,598), phù hợp khuôn mặt nhỏ hoặc xa | Chậm nhất (437 ms trên S3), không phù hợp thời gian thực trên S3 |

---

## 4. Hai mô hình nhận dạng khuôn mặt: `MFN_S8_V1` và `MBF_S8_V1`

Cả hai mô hình đều được gọi qua lớp `HumanFaceFeat`, cùng nhận ảnh khuôn mặt đã căn chỉnh kích thước **112 × 112 × 3** và cùng được lượng tử hóa 8 bit. Cả hai hỗ trợ ESP32-S3 và ESP32-P4.

### 4.1. Cơ sở kiến trúc

Tài liệu chính thức của Espressif chỉ công bố tên gọi, số tham số, số phép tính, độ chính xác và độ trễ. Sơ đồ lớp chi tiết **không được công bố**. Phần dưới đây dựa trên tên gọi và tài liệu khoa học liên quan, vì vậy cần đọc như phân tích tham khảo.

- **MFN** có nhiều khả năng viết tắt của **MobileFaceNet**, một họ mạng nơ-ron tích chập cực nhẹ do Chen và cộng sự đề xuất năm 2018 (arXiv:1804.07573) dành riêng cho xác minh khuôn mặt trên thiết bị di động và nhúng. Các điểm thiết kế chính của bài báo gốc:
  - dùng khối *bottleneck* dư kiểu MobileNetV2 với tích chập tách theo chiều sâu để giảm tham số và phép tính;
  - thay lớp gộp trung bình toàn cục bằng **tích chập sâu toàn cục (Global Depthwise Convolution)**. Bài báo lập luận rằng các vị trí không gian trên bản đồ đặc trưng cuối của khuôn mặt có tầm quan trọng khác nhau, nên gộp trung bình đều làm giảm độ chính xác xác minh;
  - huấn luyện bằng hàm mất mát **ArcFace** trên tập MS-Celeb-1M đã tinh lọc;
  - kết quả bài báo gốc: dưới 1 triệu tham số, mô hình 4,0 MB đạt 99,55% độ chính xác trên LFW và 92,59% TAR@FAR=1e-6 trên MegaFace, đồng thời nhanh hơn MobileNetV2 hơn 2 lần trong điều kiện thí nghiệm giống nhau.
- **MBF** là phiên bản lớn hơn (3,4 triệu tham số, 0,90 GFLOPs so với 1,2 triệu và 0,46 GFLOPs của MFN). Tên gọi gợi ý cũng thuộc họ MobileFaceNet với độ rộng lớn hơn, nhưng Espressif không xác nhận điều này.
- Số tham số 1,2 triệu của MFN lớn hơn con số "dưới 1 triệu" trong bài báo gốc, nên MFN của Espressif có thể là bản điều chỉnh (ví dụ khác kích thước vector đầu ra). Đây là suy luận, chưa có xác nhận từ nguồn chính thức.

### 4.2. Thông số

| Mô hình | Số tham số (triệu) | GFLOPs | TAR@FAR=1E-4 trên IJB-C (%) |
|---|---|---|---|
| `MFN_S8_V1` (mặc định) | 1,2 | 0,46 | 90,03 |
| `MBF_S8_V1` | 3,4 | 0,90 | 93,94 |

**Ý nghĩa của chỉ số.** TAR@FAR là tỷ lệ chấp nhận đúng (True Accept Rate) tại một tỷ lệ chấp nhận sai (False Accept Rate) cố định. Với FAR = 10⁻⁴, tức 1 trên 10.000 cặp khác người bị nhận nhầm, giá trị 90,03% nghĩa là khoảng 9 trên 10 cặp cùng người được nhận đúng. IJB-C là bộ dữ liệu đánh giá nhận dạng khuôn mặt trong điều kiện không kiểm soát.

### 4.3. Độ trễ (phiên bản component 0.3.0)

| Mô hình – chip | Tiền xử lý | Suy luận mô hình | Hậu xử lý | Tổng xấp xỉ |
|---|---|---|---|---|
| `mfn_s8_v1` – ESP32-S3 | 5,6 ms | 248,8 ms | 0,08 ms | khoảng 255 ms |
| `mfn_s8_v1` – ESP32-P4 | 2,9 ms | 93,0 ms | 0,05 ms | khoảng 96 ms |
| `mbf_s8_v1` – ESP32-S3 | 5,6 ms | 1072,4 ms | 0,08 ms | khoảng 1078 ms |
| `mbf_s8_v1` – ESP32-P4 | 2,9 ms | 188,2 ms | 0,05 ms | khoảng 191 ms |

Độ trễ tiền xử lý thay đổi giữa các phiên bản component (bản 0.2.x ghi khoảng 8,4 ms trên S3), nên nên đối chiếu với phiên bản đang dùng.

### 4.4. Ưu điểm và hạn chế

| Mô hình | Ưu điểm | Hạn chế |
|---|---|---|
| `MFN_S8_V1` | Nhỏ (1,2 triệu tham số), chiếm ít flash; suy luận dưới 300 ms trên S3 nên gần thời gian thực; là mô hình mặc định trên ESP32-S3-EYE | Độ chính xác thấp hơn MBF khoảng 3,9 điểm phần trăm TAR |
| `MBF_S8_V1` | Chính xác hơn (TAR 93,94%); phù hợp khi cần giảm nhận nhầm | Chậm gấp khoảng 4,3 lần trên S3 (hơn 1 giây mỗi lần); tốn flash và bộ nhớ hơn |

Trên ESP32-P4, khoảng cách tốc độ giữa hai mô hình thu hẹp còn khoảng 2 lần, nên MBF là lựa chọn hợp lý hơn trên chip này.

---

## 5. Khuyến nghị lựa chọn

| Tình huống | Gợi ý |
|---|---|
| ESP32-S3, cần phản hồi nhanh (ví dụ khóa cửa) | Phát hiện: MSR + MNP; nhận dạng: `MFN_S8_V1` |
| ESP32-S3, ưu tiên hạn chế nhận nhầm, chấp nhận chậm | `MBF_S8_V1`, chỉ chạy nhận dạng khi đã có khuôn mặt ổn định trong khung hình |
| Khuôn mặt nhỏ hoặc ở xa | ESPDet-Pico 224 (cân bằng) hoặc 416 (chính xác, chỉ phù hợp P4 hoặc ứng dụng không cần thời gian thực) |
| ESP32-P4 | Có thể dùng MBF hoặc ESPDet-Pico 416 mà vẫn đạt tốc độ chấp nhận được |

---

## 6. Cách sử dụng

### 6.1. Phát hiện khuôn mặt

```cpp
HumanFaceDetect *detect = new HumanFaceDetect();

dl::image::img_t img = {.data = DATA, .width = WIDTH, .height = HEIGHT, .pix_type = PIX_TYPE};
auto &results = detect->run(img);
```

### 6.2. Trích xuất đặc trưng (chọn mô hình)

```cpp
HumanFaceFeat *feat = new HumanFaceFeat();                            // dùng mô hình mặc định trong menuconfig
HumanFaceFeat *feat_mfn = new HumanFaceFeat(HumanFaceFeat::MFN_S8_V1);
HumanFaceFeat *feat_mbf = new HumanFaceFeat(HumanFaceFeat::MBF_S8_V1);
```

Muốn truyền tham số tường minh, cả hai mô hình phải được nạp vào flash hoặc thẻ SD.

### 6.3. Tùy chọn cấu hình trong `menuconfig`

| Tùy chọn | Ý nghĩa |
|---|---|
| `CONFIG_FLASH_HUMAN_FACE_FEAT_MFN_S8_V1`, `CONFIG_FLASH_HUMAN_FACE_FEAT_MBF_S8_V1` | Có nạp mô hình nhận dạng vào flash hay không |
| `CONFIG_HUMAN_FACE_FEAT_MFN_S8_V1`, `CONFIG_HUMAN_FACE_FEAT_MBF_S8_V1` | Mô hình nhận dạng mặc định |
| `CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA` / `..._IN_FLASH_PARTITION` / `..._IN_SDCARD` | Vị trí lưu mô hình nhận dạng |
| `CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA` / `..._IN_FLASH_PARTITION` / `..._IN_SDCARD` | Vị trí lưu mô hình phát hiện |
| `CONFIG_HUMAN_FACE_DETECT_MODEL_SDCARD_DIR`, `CONFIG_HUMAN_FACE_FEAT_MODEL_SDCARD_DIR` | Thư mục mô hình trên thẻ SD (mặc định `models/s3` cho ESP32-S3, `models/p4` cho ESP32-P4) |

Nếu chọn phân vùng flash, tệp `partition.csv` phải có phân vùng `human_face_det` (phát hiện) và `human_face_feat` (nhận dạng), kích thước đủ chứa tệp mô hình. Khi chép mô hình sang thẻ SD, không được đổi tên tệp.

---

## 7. Đối chiếu với mã nguồn của dự án

Mã nguồn dự án (ESP32-S3, camera QQVGA, cơ sở dữ liệu `/spiffs/face.db`) khởi tạo `HumanFaceDetect` và `HumanFaceRecognizer` không kèm tham số, nên **mô hình thực tế được dùng là mô hình mặc định trong `menuconfig`**. Một số điểm liên quan đến các số liệu ở trên:

- Kích thước khung hình 160 × 120 khớp với đầu vào 120 × 160 (cao × rộng) của MSR, nên phần tiền xử lý thu nhỏ gần như không đáng kể.
- Suy luận nhận dạng mất khoảng 255 ms với MFN trên S3, vì vậy việc chỉ chạy nhận dạng mỗi 10 khung hình là hợp lý để giữ tốc độ hiển thị.
- Ngưỡng tương đồng mặc định 0,5 có thể điều chỉnh trong `HumanFaceRecognizer` hoặc qua `menuconfig`. Với ứng dụng khóa cửa, nên cân nhắc nâng ngưỡng để giảm nguy cơ nhận nhầm, và đăng ký 2 đến 3 lần ở các góc, ánh sáng khác nhau để tăng độ ổn định.

---

## 8. Giới hạn của tài liệu này

- Các số liệu trong bảng lấy từ tài liệu chính thức của Espressif tại thời điểm tổng hợp. Số liệu độ trễ có thể thay đổi giữa các phiên bản component.
- Sơ đồ lớp chi tiết của MSR, MNP, MFN và MBF không được Espressif công bố. Các nhận định về họ MobileFaceNet và sự tương đồng với MTCNN là suy luận từ tên gọi và tài liệu khoa học liên quan, đã được ghi chú tại nơi xuất hiện.
- Cách giải nghĩa tên viết tắt MSR ("Multi-Scale Regression") và MNP ("Multi-level Non-Maximum suppression and Pooling") lấy từ bài workshop của Espressif, không có trong README của mô hình.
- Thông tin về ESPDet-Pico (kiến trúc, số tham số) một phần lấy từ README dự án `esp-detection` và một phần từ trang tóm tắt DeepWiki của bên thứ ba.

---

## 9. Tài liệu tham khảo

1. Espressif. *Human Face Detection Models* (ESP-DL). https://github.com/espressif/esp-dl/blob/master/models/human_face_detect/README.md
2. Espressif. *Human Face Recognition Models* (ESP-DL). https://github.com/espressif/esp-dl/tree/master/models/human_face_recognition
3. ESP Component Registry. *espressif/human_face_recognition*. https://components.espressif.com/components/espressif/human_face_recognition
4. Espressif Developer Portal. *Edge-AI with ESP32-S3 Workshop: Assignment 2 (face detection)*. https://developer.espressif.com/workshops/edge-ai-with-esp32-s3/assignment-2/
5. Espressif Developer Portal. *Edge-AI with ESP32-S3 Workshop: Assignment 3 (face recognition)*. https://developer.espressif.com/workshops/edge-ai-with-esp32-s3/assignment-3/
6. Espressif. *ESP-Detection* (ESPDet-Pico, dựa trên Ultralytics YOLOv11). https://github.com/espressif/esp-detection
7. Espressif. *ESP-DL*. https://github.com/espressif/esp-dl
8. Chen, S., Liu, Y., Gao, X., Han, Z. (2018). *MobileFaceNets: Efficient CNNs for Accurate Real-Time Face Verification on Mobile Devices.* arXiv:1804.07573. https://arxiv.org/abs/1804.07573
9. Zhang, K., Zhang, Z., Li, Z., Qiao, Y. (2016). *Joint Face Detection and Alignment Using Multitask Cascaded Convolutional Networks* (MTCNN), IEEE Signal Processing Letters.
