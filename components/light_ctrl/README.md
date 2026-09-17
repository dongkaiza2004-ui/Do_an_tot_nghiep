# light_ctrl

## Chức năng
Đọc cảm biến ánh sáng môi trường (LDR qua ADC, hoặc cảm biến số như BH1750 qua I2C — tùy
phần cứng thực tế sẽ chọn) theo chu kỳ, so sánh với ngưỡng cấu hình, và **tự động bật/tắt
đèn hỗ trợ** (qua GPIO điều khiển relay/MOSFET/LED) khi trời tối — hoạt động **độc lập**,
không cần chờ lệnh từ Node B.

## Đầu vào
- Giá trị đọc từ cảm biến ánh sáng (ADC hoặc I2C, tùy loại cảm biến chọn)
- `light_threshold_lux` từ `app_state` (giá trị mặc định cứng, có thể mở rộng cho phép
  Node B chỉnh từ xa qua 1 lệnh UART mới nếu cần sau này)

## Đầu ra
- Tín hiệu GPIO bật/tắt đèn

## API công khai (gợi ý)
```c
esp_err_t light_ctrl_init(void);   // init ADC/I2C tùy loại cảm biến, init GPIO đèn
void      light_ctrl_start(void);  // tạo task nền, chu kỳ đọc ví dụ mỗi 5 giây
```

## Phụ thuộc
- `app_state` (đọc ngưỡng, nếu cho phép cấu hình từ xa)
- Driver ADC hoặc I2C của ESP-IDF, tùy cảm biến chọn

## Được dùng bởi
Không có component nào phụ thuộc ngược lại — đây là module "lá" (leaf), chỉ đọc cảm biến và
điều khiển phần cứng, không cung cấp dữ liệu cho luồng xử lý camera/AI.

## Ghi chú
- **Cần xác nhận loại cảm biến cụ thể** (LDR analog đơn giản qua ADC, hay module số như
  BH1750/TSL2561 qua I2C) để viết chính xác phần đọc dữ liệu — hiện để interface chung
  chung vì chưa chốt loại cảm biến.
- Nên có **hysteresis** (hai ngưỡng bật/tắt lệch nhau, ví dụ bật khi < 50 lux, tắt khi >
  80 lux) thay vì 1 ngưỡng duy nhất, để tránh đèn nhấp nháy liên tục khi ánh sáng dao động
  quanh ngưỡng.
