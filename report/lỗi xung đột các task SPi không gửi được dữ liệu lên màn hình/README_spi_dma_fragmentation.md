# Phân tích lỗi "SPI transmit (queue) color failed" trên Node A (ESP32-S3 Camera + AI)

## 1. Tóm tắt vấn đề

Khi Node A đồng thời bật hai chức năng giao tiếp — UART (nhận lệnh điều khiển từ Node B) và ESP-NOW (gửi ảnh sự kiện sang Node B) — hệ thống liên tục phát sinh lỗi:

```
E (xxxx) lcd_panel.io.spi: panel_io_spi_tx_color(395): spi transmit (queue) color failed
```

Lỗi xuất hiện ngay từ khung hình đầu tiên, lặp lại ở mọi khung hình sau đó, khiến màn hình TFT không thể cập nhật hình ảnh. Điều bất thường là lỗi **chỉ xảy ra khi cả UART và ESP-NOW cùng hoạt động**; nếu chỉ bật riêng lẻ một trong hai, hệ thống chạy hoàn toàn bình thường. Đáng chú ý hơn, lỗi xuất hiện **ngay cả khi chưa có bất kỳ dữ liệu thực nào được gửi hoặc nhận qua hai kênh giao tiếp này** — nghĩa là bản thân việc *khởi tạo* hai driver, chứ không phải việc *truyền dữ liệu* qua chúng, đã đủ để gây ra sự cố.

Sau quá trình loại trừ tuần tự nhiều giả thuyết (xung đột chân GPIO, tranh chấp kênh DMA, tranh chấp CPU giữa hai nhân xử lý, cạn kiệt tổng dung lượng bộ nhớ), nguyên nhân gốc rễ được xác định là **hiện tượng phân mảnh bộ nhớ nội bộ có khả năng DMA (DMA-capable Internal RAM)**, xảy ra ở tầng thấp của thư viện điều khiển SPI trong ESP-IDF.

## 2. Kiến thức nền cần nắm trước khi phân tích

### 2.1. Hai loại bộ nhớ trên ESP32-S3: SRAM nội bộ và PSRAM ngoài

ESP32-S3 sở hữu hai vùng bộ nhớ có đặc tính vật lý khác biệt hoàn toàn, dù cùng nằm trên một module:

- **Internal SRAM (RAM nội bộ):** nằm ngay trong lõi vi xử lý, kết nối qua bus dữ liệu tốc độ cao, độ trễ truy cập thấp. Đây là vùng bộ nhớ duy nhất mà các bộ điều khiển DMA (Direct Memory Access) của các ngoại vi như SPI, I2S có thể truy cập trực tiếp.
- **PSRAM (RAM ngoài):** là một chip nhớ riêng biệt, kết nối với lõi xử lý qua giao tiếp Quad/Octal SPI. PSRAM có dung lượng lớn hơn nhiều (hàng megabyte so với vài trăm kilobyte của SRAM nội bộ) nên thường được dùng cho các bộ đệm lớn như khung hình camera, nhưng đổi lại có độ trễ truy cập cao hơn.

Điểm mấu chốt: **bộ điều khiển DMA của SPI không thể đọc dữ liệu trực tiếp từ PSRAM**. Đây là giới hạn phần cứng, không phải giới hạn phần mềm.

### 2.2. Cơ chế "bounce buffer" của driver SPI khi gặp dữ liệu nguồn nằm ở PSRAM

Vì trong dự án, khung hình camera được cấu hình lưu trữ tại PSRAM (`CAMERA_FB_IN_PSRAM`), mỗi khi hàm `esp_lcd_panel_draw_bitmap()` được gọi với con trỏ trỏ vào vùng PSRAM này, thư viện `spi_master` của ESP-IDF sẽ tự động phát hiện điều đó thông qua hàm nội bộ `esp_ptr_dma_capable()`. Vì con trỏ không trỏ tới SRAM nội bộ, hàm này trả về `false`.

Khi đó, driver bắt buộc phải thực hiện thêm 1 bước trung gian trước khi truyền dữ liệu: **tự động cấp phát động (dynamic allocation) một vùng đệm tạm thời — gọi là bounce buffer — có kích thước bằng đúng dữ liệu cần gửi (trong dự án này là 160 × 128 × 2 byte ≈ 40.960 byte cho mỗi khung hình), nằm trong vùng SRAM nội bộ có cờ `MALLOC_CAP_DMA`**, sao chép dữ liệu từ PSRAM sang vùng đệm này, rồi mới thực hiện truyền DMA từ vùng đệm đó ra ngoại vi SPI. Quá trình cấp phát và giải phóng bounce buffer này lặp lại ở **mỗi khung hình**, không phải chỉ một lần.

## 3. Cơ chế gây lỗi: hiện tượng phân mảnh bộ nhớ

### 3.1. UART và Wi-Fi/ESP-NOW chiếm dụng bộ nhớ DMA ngay từ bước khởi tạo

Cả hai driver này đều cần vùng nhớ có khả năng DMA để hoạt động, và cả hai đều xin cấp phát các khối nhớ đó **ngay khi được khởi tạo, không cần chờ có dữ liệu thực tế đi qua**:

- **Driver Wi-Fi (nền tảng của ESP-NOW):** khi gọi `esp_wifi_init()`, driver cấp phát sẵn một tập hợp lớn các bộ đệm nhận/gửi (dynamic RX buffer, dynamic TX buffer, static RX buffer...) trong vùng SRAM nội bộ có khả năng DMA, để sẵn sàng xử lý sóng vô tuyến bất kỳ lúc nào — bất kể đã có gói tin nào được gửi/nhận hay chưa.
- **Driver UART:** khi gọi `uart_driver_install()`, driver cấp phát ring buffer phục vụ việc thu/phát dữ liệu qua cổng nối tiếp.

Các thao tác cấp phát này diễn ra rải rác về mặt thời gian và không liên tục về mặt địa chỉ bộ nhớ, xen giữa các bước khởi tạo khác của hệ thống.

### 3.2. Vì sao việc xin/trả bộ nhớ liên tục lại "xé vụn" vùng nhớ khả dụng

Bộ cấp phát bộ nhớ động (heap allocator) quản lý vùng SRAM nội bộ như một dải địa chỉ liên tục. Khi nhiều thành phần khác nhau liên tục xin cấp phát (`malloc`) và giải phóng (`free`) các khối có kích thước khác nhau tại các thời điểm khác nhau, các khối nhớ còn trống sẽ dần bị chia cắt thành nhiều đoạn nhỏ, rời rạc, xen kẽ với các khối đang được sử dụng — hiện tượng này gọi là **phân mảnh bộ nhớ (memory fragmentation)**.

Hệ quả quan trọng nhất của hiện tượng này là: **tổng dung lượng bộ nhớ còn trống** và **kích thước của khối nhớ liền mạch lớn nhất còn trống** là hai đại lượng hoàn toàn khác nhau. Một hệ thống có thể báo cáo còn trống 130.000 byte, nhưng nếu số byte đó bị chia vụn thành hàng trăm mảnh nhỏ (mỗi mảnh chỉ vài trăm đến vài nghìn byte), thì **không có mảnh đơn lẻ nào đủ lớn để chứa một khối dữ liệu liên tục 40.960 byte**.

Đây chính xác là những gì xảy ra trong hệ thống: quá trình đo đạc thực tế cho thấy dung lượng trống của vùng nhớ DMA-capable luôn ở mức cao (70.000–130.000 byte) ngay cả khi lỗi đang xảy ra liên tục — con số này xác nhận rằng vấn đề không nằm ở *tổng dung lượng* mà nằm ở *độ liền mạch* của bộ nhớ.

### 3.3. Vì sao thông báo lỗi không kèm theo chi tiết nguyên nhân

Trong mã nguồn của hàm cấp phát bounce buffer (`setup_priv_desc()` trong `spi_master.c`), khi việc cấp phát thất bại, thư viện chỉ ghi lại thông tin này ở mức log `ESP_LOGD` (Debug) — mức log không được hiển thị theo cấu hình mặc định của dự án (vốn đặt ở mức `ESP_LOGI` — Info trở lên). Do đó, lỗi cấp phát bộ nhớ thất bại diễn ra hoàn toàn im lặng ở tầng thấp; tầng giao tiếp phía trên (`esp_lcd_panel_io_spi`) chỉ nhận được mã lỗi trả về và in ra duy nhất một dòng thông báo chung chung: `spi transmit (queue) color failed`, không đề cập gì đến nguyên nhân thực sự là thiếu bộ nhớ liền mạch.

Đặc điểm này giải thích vì sao lỗi khó chẩn đoán bằng cách đọc log thông thường, và vì sao quá trình xác định nguyên nhân đòi hỏi phải chủ động đo đạc thêm bằng các hàm như `heap_caps_get_free_size()`, cũng như đối chiếu trực tiếp với mã nguồn tầng thấp của ESP-IDF.

## 4. Chuỗi nhân quả đầy đủ

1. Khung hình camera được lưu tại PSRAM (theo cấu hình `CAMERA_FB_IN_PSRAM`).
2. Hàm vẽ màn hình nhận một con trỏ trỏ vào PSRAM, trong khi bộ điều khiển DMA của SPI chỉ có thể đọc dữ liệu từ SRAM nội bộ.
3. Vì vậy, ở mỗi khung hình, driver SPI phải cấp phát động một vùng đệm tạm (bounce buffer) khoảng 40 kilobyte trong SRAM nội bộ để sao chép dữ liệu qua trước khi truyền DMA.
4. Việc khởi tạo driver Wi-Fi (phục vụ ESP-NOW) và driver UART, ngay cả khi chưa truyền dữ liệu thực tế, đã cấp phát trước một lượng lớn các khối bộ nhớ nhỏ trong cùng vùng SRAM nội bộ có khả năng DMA.
5. Việc cấp phát rải rác này làm vùng nhớ bị phân mảnh: tổng dung lượng trống vẫn cao, nhưng không còn khối liền mạch nào đủ lớn (≥ 40 kilobyte) để phục vụ nhu cầu của bước 3.
6. Hàm cấp phát bounce buffer thất bại, trả về lỗi "hết bộ nhớ" một cách im lặng (chỉ ghi log ở mức Debug).
7. Tầng điều khiển màn hình nhận được lỗi này và báo ra ngoài bằng thông báo chung chung `spi transmit (queue) color failed`, không có gì cho thấy nguyên nhân là do thiếu bộ nhớ liền mạch.

Chuỗi nhân quả này giải thích trọn vẹn mọi đặc điểm quan sát được trong thực tế: lỗi xảy ra ngay từ khung hình đầu tiên (vì phân mảnh đã hình thành ngay sau bước khởi tạo, không cần đợi dữ liệu được truyền); lỗi chỉ xuất hiện khi cả hai driver cùng hoạt động (vì khi đó tổng số khối nhớ nhỏ bị xé lẻ mới đủ nhiều để triệt tiêu hết các khối liền mạch từ 40 kilobyte trở lên); và chỉ số dung lượng trống tổng vẫn cao trong khi lỗi vẫn xảy ra (vì đại lượng cần thiết để đánh giá đúng vấn đề là kích thước khối liền mạch lớn nhất, không phải tổng dung lượng trống).

## 5. Các giả thuyết đã được loại trừ trong quá trình chẩn đoán

Trước khi xác định đúng nguyên nhân trên, nhiều giả thuyết khác đã được đặt ra và kiểm chứng bằng thực nghiệm, sau đó bị bác bỏ dựa trên bằng chứng log thực tế:

- **Xung đột chân GPIO:** đã được kiểm tra và loại trừ ở giai đoạn đầu (phát hiện và khắc phục một xung đột GPIO thực sự khác — giữa chân ADC của cảm biến ánh sáng và chân chip-select của màn hình — nhưng đây là một lỗi độc lập, không liên quan đến lỗi SPI đang phân tích).
- **Tranh chấp kênh DMA giữa SPI và UART:** bị loại trừ vì ESP32-S3 không cho phép chỉ định thủ công kênh DMA cho SPI (chỉ hỗ trợ chế độ tự động cấp phát), nên hai ngoại vi này không thể tranh chấp kênh DMA theo cách đã giả định.
- **Tranh chấp CPU giữa hai nhân xử lý (core affinity):** việc ghim các tác vụ xử lý hình ảnh sang một nhân riêng biệt, tách khỏi nhân đang chạy driver Wi-Fi, không giải quyết được vấn đề, cho thấy nguyên nhân không nằm ở việc tranh chấp thời gian xử lý của CPU.
- **Thiếu tổng dung lượng bộ nhớ DMA-capable:** bị bác bỏ trực tiếp bằng số liệu đo đạc — dung lượng trống đo được vẫn ở mức cao ngay tại thời điểm lỗi xảy ra liên tục.

Việc loại trừ tuần tự các giả thuyết này, dù không tìm ra nguyên nhân ngay lập tức, đã thu hẹp dần phạm vi khả nghi và cuối cùng dẫn đến việc đối chiếu trực tiếp với mã nguồn tầng thấp của thư viện `spi_master`, nơi nguyên nhân thực sự được xác nhận.

## 6. Giải pháp khắc phục

Nguyên tắc khắc phục là **loại bỏ hoàn toàn nhu cầu cấp phát động bounce buffer ở mỗi khung hình**, bằng cách chủ động cấp phát trước một vùng đệm cố định, nằm sẵn trong SRAM nội bộ có khả năng DMA, ngay tại thời điểm khởi tạo hệ thống — trước khi driver Wi-Fi và UART kịp làm phân mảnh vùng nhớ này.

Quy trình xử lý mới cho mỗi khung hình trở thành:

1. Sao chép (memcpy) dữ liệu khung hình từ PSRAM vào vùng đệm cố định đã cấp phát sẵn trong SRAM nội bộ.
2. Truyền vùng đệm này (vốn đã nằm trong SRAM nội bộ, được driver SPI nhận diện là DMA-capable) cho hàm vẽ màn hình.

Vì vùng đệm đích lúc này đã thỏa mãn điều kiện DMA-capable ngay từ đầu, hàm `setup_priv_desc()` trong driver SPI sẽ không còn cần thực hiện bước cấp phát động nào nữa, và do đó không còn phụ thuộc vào tình trạng phân mảnh của bộ nhớ tại thời điểm chạy.

Chi phí đánh đổi của giải pháp này là một thao tác sao chép bộ nhớ (memcpy) khoảng 40 kilobyte cho mỗi khung hình, là chi phí không đáng kể so với lợi ích loại bỏ hoàn toàn một lớp lỗi khó chẩn đoán và không ổn định.

## 7. Bài học kỹ thuật rút ra

- Khi làm việc với các nền tảng vi điều khiển có nhiều loại bộ nhớ (SRAM nội bộ và PSRAM ngoài), cần xác định rõ những ngoại vi nào yêu cầu bộ nhớ DMA-capable, và tránh truyền trực tiếp con trỏ PSRAM cho các ngoại vi đó nếu có thể chủ động kiểm soát được.
- Chỉ số "tổng dung lượng bộ nhớ trống" không phản ánh đầy đủ khả năng đáp ứng của hệ thống đối với các yêu cầu cấp phát khối lớn; cần theo dõi thêm chỉ số kích thước khối liền mạch lớn nhất khi chẩn đoán các lỗi liên quan đến cấp phát bộ nhớ.
- Với các khối dữ liệu lớn cần truyền định kỳ qua DMA (như khung hình cho màn hình), nên ưu tiên cấp phát tĩnh (một lần, ngay từ đầu chương trình) thay vì cấp phát động lặp lại theo chu kỳ, đặc biệt trong hệ thống có nhiều driver khác cùng hoạt động và có khả năng gây phân mảnh bộ nhớ.
- Mức log mặc định của một số thư viện hệ thống có thể ẩn đi thông tin chẩn đoán quan trọng; khi gặp lỗi khó lý giải từ log tiêu chuẩn, việc đối chiếu trực tiếp với mã nguồn tầng thấp của framework là một hướng tiếp cận cần thiết.
