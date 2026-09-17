# comm_uart (Node A)

## Chức năng
Lắng nghe UART kết nối với Node B, **giải mã khung lệnh** theo `uart_protocol.h` (dùng
chung định nghĩa struct/enum với Node B — file này nên đặt ở cả 2 project vì đã thống nhất
KHÔNG dùng code/thư mục chung giữa 2 node, chỉ đồng bộ thủ công nội dung file protocol),
kiểm tra checksum, rồi **điều phối lệnh** tới đúng component xử lý:

| Lệnh nhận | Chuyển tới |
|---|---|
| `SET_MODEL`, `SET_THRESHOLD` | `face_engine` |
| `SET_BRIGHTNESS`, `SET_CONTRAST`, `SET_FLIP`, `SET_MIRROR` | `app_state` → `cam_driver` áp dụng |
| `SHOW_FPS` | `app_state` |
| `ADD_USER_START`, `ADD_USER_CAPTURE`, `ADD_USER_CANCEL` | `face_engine` (state machine enroll) |
| `SUB_USER`, `LIST_USER_REQ` | `face_engine` |
| `START_STREAM`, `STOP_STREAM` | `app_state` (bật/tắt việc chạy vòng lặp AI/hiển thị, nếu cần tiết kiệm điện) |

Sau khi xử lý, gửi lại phản hồi tương ứng (`RSP_ACK`/`RSP_NACK`, kết quả enroll/xóa, danh
sách user, báo cáo FPS định kỳ).

## Đầu vào
- Dữ liệu thô từ UART driver (ESP-IDF `driver/uart.h`)

## Đầu ra
- Lời gọi hàm tới `app_state`/`face_engine`
- Khung phản hồi gửi ngược lại Node B qua cùng UART

## API công khai (gợi ý)
```c
esp_err_t comm_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud_rate);
void      comm_uart_start(void);   // task nền: đọc byte, ráp khung, validate checksum, dispatch
void      comm_uart_send_response(uart_cmd_id_t rsp_cmd, const void *payload, uint8_t len);
```

## Phụ thuộc
- `uart_protocol.h` (định nghĩa giao thức)
- `app_state`, `face_engine` (nơi thực thi lệnh)

## Được dùng bởi
`app_main` (khởi tạo lúc boot)

## Ghi chú
- Cần cơ chế **timeout/resync khung**: nếu nhận byte `START` giữa chừng 1 khung đang ráp dở
  (do nhiễu/mất byte), phải hủy khung cũ và bắt đầu ráp lại từ byte `START` mới, tránh treo
  chờ khung không bao giờ đủ.
- Vì `ADD_USER_CAPTURE` cần `face_engine` xử lý xong (chạy enroll trên frame hiện tại) mới
  có kết quả để trả `RSP_ADD_USER_RESULT`, nên xử lý lệnh này nên **không block** task đọc
  UART — đẩy yêu cầu vào queue cho `face_engine`, nhận kết quả qua callback/queue riêng rồi
  mới gửi phản hồi.
