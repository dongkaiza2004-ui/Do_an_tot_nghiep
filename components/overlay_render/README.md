# overlay_render

## Chức năng
Vẽ thông tin trực quan **đè lên bitmap camera** trước khi đưa ra `display_driver` để hiện
tại chỗ trên TFT của Node A. Kể từ khi ESP-NOW đổi sang chỉ gửi ảnh sự kiện (không stream
liên tục), component này **chỉ phục vụ màn hình local**, không còn ảnh hưởng tới dữ liệu
gửi qua ESP-NOW.

Các loại overlay:
- Khung chữ nhật quanh khuôn mặt phát hiện được (màu khác nhau tùy trạng thái: đang nhận
  diện / đã khớp / người lạ)
- Tên người dùng (nếu khớp CSDL) hoặc nhãn "Unknown"
- Bộ đếm FPS (chỉ vẽ khi `app_state.show_fps == true`)
- Hướng dẫn khi đang enroll: ví dụ dòng chữ "Đưa mặt vào khung, chờ xác nhận..." khi
  `app_state.enroll_in_progress == true`

## Đầu vào
- Buffer bitmap gốc (RGB565) từ `cam_driver`/`face_engine`
- Kết quả detect/recognize từ `face_engine` (box, tên, similarity)
- Cờ hiển thị từ `app_state` (`show_fps`, `enroll_in_progress`)

## Đầu ra
- Buffer bitmap **đã vẽ overlay**, chuyển tiếp cho `display_driver` để xuất ra SPI

## API công khai (gợi ý)
```c
void overlay_render_draw_live(uint16_t *buf, int w, int h,
                               const face_result_t *results, int result_count,
                               float current_fps);
```

## Phụ thuộc
- `app_state` (đọc cờ hiển thị)

## Được dùng bởi
`display_driver`

## Ghi chú
- Vì ảnh sự kiện gửi qua `comm_espnow` là **ảnh chụp riêng** (không dùng chung buffer với
  local display), có thể cân nhắc thêm 1 hàm `overlay_render_draw_event()` riêng nếu muốn
  ảnh sự kiện cũng có khung đánh dấu khuôn mặt (tối giản hơn, không có FPS/hint) — hiện để
  ở `face_engine` quyết định ảnh sự kiện có overlay hay là ảnh gốc sạch, tùy nhu cầu log.
