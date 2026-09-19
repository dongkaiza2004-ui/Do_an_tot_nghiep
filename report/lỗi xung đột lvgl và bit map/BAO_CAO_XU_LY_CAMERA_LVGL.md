# Báo cáo kỹ thuật: Tích hợp luồng camera trực tiếp vào giao diện LVGL trên ESP32-S3

## 1. Bối cảnh và mục tiêu ban đầu

Dự án cần hiển thị hình ảnh trực tiếp từ camera (module `esp32-camera`, độ phân giải 160×120, định dạng RGB565) lên một vùng nhỏ (124×120 pixel) bên trong màn hình `Facerecognition` của giao diện LVGL, đồng thời chạy song song mô hình phát hiện khuôn mặt (`human_face_detect`) và nhận diện khuôn mặt (`human_face_recognition`) trên từng khung hình.

Yêu cầu đặt ra không chỉ là hiển thị đúng nội dung, mà còn phải đảm bảo:
- Tốc độ khung hình (FPS) đủ mượt để xem như video trực tiếp.
- Không gây xung đột với phần giao diện còn lại (menu, label trạng thái, điều hướng).
- Sử dụng hợp lý tài nguyên bộ nhớ hạn chế của vi điều khiển (SRAM nội bộ và PSRAM).

Tài liệu này ghi lại toàn bộ quá trình thiết kế, các vấn đề phát sinh, nguyên nhân kỹ thuật của từng vấn đề, và giải pháp kiến trúc cuối cùng.

---

## 2. Kiến trúc ban đầu: hiển thị qua đối tượng `lv_img`

### 2.1. Cách hoạt động

Phiên bản đầu tiên sử dụng đối tượng chuẩn của LVGL là `lv_img`, gắn với một cấu trúc `lv_img_dsc_t` trỏ tới một vùng nhớ (buffer) do chương trình tự cấp phát. Mỗi khi có một khung hình mới từ camera:

1. Dữ liệu ảnh được cắt (crop) từ kích thước gốc 160×120 xuống còn 124×120 để vừa khung hiển thị, ghi vào buffer nói trên bằng `memcpy`.
2. Hàm `lv_obj_invalidate()` được gọi để báo cho LVGL biết vùng ảnh cần được vẽ lại.
3. LVGL, trong task nội bộ của nó (`lvgl_port` task), tại thời điểm thích hợp trong chu kỳ vẽ, sẽ tự đọc lại buffer và gửi dữ liệu ra màn hình vật lý thông qua driver màn hình.

### 2.2. Ưu điểm

Cách làm này an toàn tuyệt đối vì toàn bộ việc ghi dữ liệu ra phần cứng màn hình do một task duy nhất (task của LVGL) đảm nhiệm. Không phát sinh xung đột truy cập phần cứng.

### 2.3. Hạn chế

Tốc độ khung hình đo được dao động quanh 10–11 FPS. Nguyên nhân là dữ liệu ảnh phải trải qua nhiều bước trung gian trước khi tới được panel vật lý: từ buffer camera, sang buffer hiển thị riêng, rồi mới tới buffer nội bộ của LVGL, và cuối cùng LVGL mới đẩy ra phần cứng theo đúng lịch trình vẽ của nó (không phải ngay lập tức khi có dữ liệu mới).

---

## 3. Vấn đề hiệu năng: SRAM nội bộ cạn kiệt và giật hình

### 3.1. Hiện tượng

Sau khi tích hợp thêm camera và mô hình AI, dung lượng SRAM nội bộ còn trống giảm mạnh từ khoảng 122 KB xuống còn 42 KB. Đồng thời, màn hình `Facerecognition` xuất hiện hiện tượng giật, khựng hình theo chu kỳ.

### 3.2. Nguyên nhân

**Về bộ nhớ:** Bộ chuyển đổi hiển thị `esp_lvgl_port` theo mặc định cấp phát buffer vẽ ở SRAM nội bộ, vì cơ chế DMA (truyền dữ liệu tốc độ cao không qua CPU) trên các dòng chip ESP32 đời cũ không truy cập được PSRAM. Với ESP32-S3, hạn chế này không còn, nhưng thư viện vẫn giữ hành vi mặc định an toàn cũ nếu không được cấu hình khác đi.

**Về hiệu năng CPU:** Tác vụ xử lý camera (`face_engine_task`) và tác vụ vẽ giao diện (`lvgl_port` task) đều được tạo bằng `xTaskCreate()` mà không chỉ định lõi CPU cụ thể. ESP32-S3 có hai lõi xử lý; FreeRTOS có quyền tự do xếp hai tác vụ này vào cùng một lõi. Khi mô hình AI đang tính toán (tốn nhiều chu kỳ CPU), nó tạm thời chiếm dụng lõi đó, khiến tác vụ vẽ giao diện bị trì hoãn, biểu hiện ra ngoài là hiện tượng giật hình.

### 3.3. Giải pháp áp dụng

**Chuyển buffer hiển thị sang PSRAM:** cấu hình `esp_lvgl_port` với `flags.buff_spiram = true` để đặt buffer vẽ chính ở PSRAM, đồng thời khai báo thêm một buffer trung chuyển nhỏ (`trans_size`) ở SRAM để phục vụ DMA — vì DMA chỉ có thể truyền trực tiếp từ SRAM, nên dữ liệu được sao chép qua buffer nhỏ này theo từng đoạn trước khi gửi đi. Kết quả: SRAM nội bộ còn trống tăng trở lại lên khoảng 120 KB, đổi lại một chi phí sao chép nhỏ không đáng kể trên màn hình có độ phân giải thấp (128×160).

**Ghim tác vụ vào lõi riêng biệt:** sử dụng `xTaskCreatePinnedToCore()` để ép tác vụ vẽ giao diện (LVGL) chạy cố định ở lõi 0, và tác vụ xử lý camera/AI chạy cố định ở lõi 1. Hai lõi hoạt động độc lập, không còn tranh giành thời gian xử lý của nhau; chỉ còn gặp nhau trong khoảng thời gian rất ngắn khi tác vụ camera cần khóa (`lock`) để cập nhật dữ liệu lên giao diện.

---

## 4. Chuyển sang vẽ trực tiếp: mục tiêu tăng tốc độ khung hình

### 4.1. Lý do thay đổi kiến trúc

Sau khi khắc phục vấn đề bộ nhớ và giật hình, tốc độ khung hình vẫn bị giới hạn bởi số bước trung gian trong đường đi của dữ liệu ảnh qua LVGL. Để giảm độ trễ, phương án được thử nghiệm là bỏ qua cơ chế `lv_img`, gọi thẳng hàm cấp thấp `esp_lcd_panel_draw_bitmap()` — hàm giao tiếp trực tiếp với driver điều khiển màn hình ST7735, không thông qua lớp trừu tượng của LVGL.

### 4.2. Vấn đề phát sinh: xung đột phần cứng giữa hai luồng vẽ

Sau khi chuyển đổi, màn hình xuất hiện hiện tượng nhiễu, xé hình, hoặc chồng chập nội dung cũ và mới, đặc biệt rõ khi rời khỏi màn hình camera và tương tác với menu ngay sau đó.

**Nguyên nhân cốt lõi:** cơ chế khóa `lvgl_port_lock()`/`lvgl_port_unlock()` chỉ bảo vệ **cấu trúc dữ liệu nội bộ của LVGL** (cây đối tượng giao diện, trạng thái, style), không bảo vệ **trạng thái vật lý của giao tiếp SPI** với màn hình. Hàm `esp_lcd_panel_draw_bitmap()` gửi lệnh và dữ liệu xuống hàng đợi truyền của controller SPI rồi trả về ngay lập tức; quá trình truyền dữ liệu thực tế vẫn tiếp diễn ở nền sau khi hàm đã trả về và mutex đã được giải phóng. Nếu ngay trong khoảng thời gian đó, tác vụ LVGL cũng gửi một lệnh vẽ khác (ví dụ animation chuyển màn hình, hoặc cập nhật icon), hai luồng dữ liệu SPI bị xen kẽ vào nhau trên cùng một đường truyền, tạo ra hình ảnh sai lệch, thể hiện dưới dạng các dải nhiễu ngang hoặc hình ảnh bị "xé" theo chiều ngang.

Ngoài xung đột phần cứng, còn tồn tại một vấn đề logic độc lập: LVGL duy trì nội bộ một bản ghi "vùng nào trên màn hình đã được vẽ đúng, không cần vẽ lại". Khi chương trình tự vẽ đè lên một vùng bằng lệnh cấp thấp mà không thông qua LVGL, bản ghi nội bộ này không được cập nhật. Hậu quả là khi LVGL sau đó chỉ cần vẽ lại một phần nhỏ của giao diện (ví dụ một icon vừa đổi trạng thái focus, theo đúng cơ chế tối ưu "chỉ vẽ lại vùng thay đổi" — partial redraw), nó sẽ bỏ qua các vùng xung quanh vốn đã bị dữ liệu camera ghi đè trước đó, khiến những vùng đó tiếp tục hiển thị sai cho đến khi có một sự kiện nào khác buộc toàn bộ màn hình phải được vẽ lại.

### 4.3. Các phương án khắc phục đã thử và lý do chưa triệt để

**Phương án 1 — Kiểm tra lại màn hình hiện tại ngay trước khi vẽ:** Bổ sung điều kiện kiểm tra `lv_scr_act()` ngay trước lệnh `esp_lcd_panel_draw_bitmap()`, nhằm đảm bảo không vẽ nhầm nếu người dùng đã chuyển màn hình trong lúc mô hình AI đang xử lý (một khung hình có thể mất 50–150 mili-giây để xử lý xong). Phương án này giải quyết đúng trường hợp "vẽ nhầm nội dung", nhưng không giải quyết được xung đột ở tầng vật lý đã mô tả ở trên, vì bản thân việc gọi hàm vẽ trực tiếp vẫn tồn tại song song với LVGL.

**Phương án 2 — Tạm vô hiệu hóa cơ chế vẽ lại của LVGL:** Sử dụng hàm `lv_disp_enable_invalidation()` để tắt hoàn toàn khả năng LVGL tự vẽ lại bất cứ thứ gì trong suốt thời gian camera đang hoạt động, với kỳ vọng loại bỏ hoàn toàn khả năng LVGL chen ngang. Phương án này phát sinh một lỗi mới: nếu người dùng thực hiện thao tác chuyển màn hình (ví dụ bấm nút quay lại) đúng vào lúc cơ chế vẽ lại đang bị tắt, chính hiệu ứng chuyển màn hình của LVGL (animation) cũng bị bỏ qua — màn hình logic đã đổi (`lv_scr_act()` trả về màn hình mới), nhưng nội dung vật lý trên panel không được cập nhật. Đến khi cơ chế vẽ lại được bật trở lại, LVGL đã "tin rằng" mình đang hiển thị đúng màn hình mới, nên các thao tác tiếp theo (ví dụ đổi icon đang được focus) chỉ vẽ lại đúng phần nội dung thay đổi, để nguyên phần còn lại vẫn là dữ liệu hình ảnh camera cũ.

Cả hai phương án trên đều thuộc dạng **đồng bộ hóa bằng quy ước thời điểm** (cố gắng canh đúng lúc để tránh xung đột), vốn dễ bị phá vỡ khi có thêm một luồng sự kiện bất đồng bộ khác (ở đây là thao tác điều hướng của người dùng qua UART) xen vào đúng lúc.

---