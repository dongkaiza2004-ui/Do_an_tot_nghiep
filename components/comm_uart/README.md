# comm_uart

Nhận/gửi bản tin điều khiển ngắn qua UART.

## Vai trò
- Định nghĩa protocol lệnh đơn giản (dạng text hoặc binary ngắn), ví dụ:
  đổi model, set threshold, enroll, xoá id, ping/status.
- Parse lệnh nhận được, gọi vào app_config/face_engine tương ứng.
- Gửi phản hồi/trạng thái ngược lại qua UART khi cần.

## Không làm gì
- Không truyền dữ liệu nặng (ảnh) — việc đó thuộc comm_espnow.

## Phụ thuộc
- driver/uart
- app_config, face_engine