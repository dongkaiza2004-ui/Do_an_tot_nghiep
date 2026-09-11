# comm_espnow

Truyền dữ liệu nặng (ảnh, kết quả kèm ảnh) qua ESP-NOW.

## Vai trò
- Khởi tạo ESP-NOW, quản lý danh sách peer.
- Chia nhỏ (fragment) dữ liệu ảnh thành nhiều gói do giới hạn kích thước
  gói ESP-NOW (~250 byte), đánh số thứ tự.
- Ráp lại (reassembly) dữ liệu ở đầu nhận, kiểm tra thiếu gói/timeout.
- Nhận frame/kết quả từ face_engine để gửi đi; nhận dữ liệu từ peer khác
  và đẩy vào queue cho module liên quan xử lý.

## Không làm gì
- Không xử lý bản tin điều khiển ngắn — việc đó thuộc comm_uart.

## Phụ thuộc
- esp_now (ESP-IDF)
- app_events (định nghĩa struct gói tin, frame_msg_t)