# display_driver (Node A)

## Chức năng
Driver mức thấp cho màn hình **ST7735 128x160** gắn trực tiếp trên Node A. Chỉ chịu trách
nhiệm phần cứng: khởi tạo bus SPI, khởi tạo panel, và vẽ 1 buffer bitmap RGB565 lên màn
hình. **Không dùng LVGL** — Node A chỉ cần hiển thị video trực tiếp liên tục, không có menu
tương tác, nên dùng cách vẽ bitmap thẳng (giữ nguyên cách làm trong `main.cpp` gốc:
`esp_lcd_panel_draw_bitmap`) để tránh chi phí dựng UI framework không cần thiết.

## Đầu vào
- Buffer bitmap RGB565 đã hoàn chỉnh (đã có overlay) từ `overlay_render`

## Đầu ra
- Hình ảnh hiển thị trực tiếp trên TFT (không có "output" dữ liệu nào khác)

## API công khai (gợi ý)
```c
esp_err_t display_driver_init(void);     // init SPI bus + panel ST7735, xoay/mirror theo layout thực tế
void      display_driver_draw(const uint16_t *buf, int w, int h, int y_offset);
```

## Phụ thuộc
- `esp_lcd` (ESP-IDF), `teriyakigod__esp_lcd_st7735` (managed component)

## Được dùng bởi
`overlay_render` (gọi sau khi vẽ xong overlay), hoặc `face_engine`/`app_main` gọi trực tiếp
sau bước overlay tùy cách tổ chức task.

## Ghi chú
- Giữ nguyên toàn bộ thông số đã xác nhận đúng trong `main.cpp` gốc: chân SPI, `swap_xy`,
  `mirror`, `LCD_Y_OFFSET` để căn giữa ảnh 160x120 trong vùng vẽ 160x128 — không cần tính
  toán lại từ đầu.
- Vì đây là node duy nhất cần tốc độ vẽ cao nhất (stream liên tục), nên ưu tiên giữ
  `pclk_hz` (tốc độ SPI) ở mức cao nhất phần cứng cho phép để tối đa FPS hiển thị.
