# display_driver

Lớp phần cứng cho màn hình ST7735, chỉ biết "vẽ", không biết "vẽ cái gì".

## Vai trò
- Khởi tạo SPI + panel ST7735 (xoay, offset, mirror).
- Cung cấp hàm vẽ nguyên thủy: draw_bitmap (khung camera),
  draw_rect (khung mặt), draw_text (chữ đơn giản cho menu).
- Quản lý buffer/khóa (mutex) nếu nhiều task cùng vẽ (camera + UI).

## Không làm gì
- Không chứa logic menu, không biết về model hay trạng thái app.

## Phụ thuộc
- teriyakigod__esp_lcd_st7735
- driver/spi_master