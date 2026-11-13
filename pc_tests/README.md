# Bộ test chạy trên PC cho cả Node A và Node B (không cần board)

## Chạy (1 lệnh)

```bash
# Ubuntu / WSL2 (lần đầu):  sudo apt install -y gcc python3 git
git fetch --all                      # để có origin/nodeA, origin/nodeB
python3 pc_tests/run_tests.py        # chạy từ bất kỳ thư mục nào trong repo
```

Script tự: lấy code thật từ nhánh `nodeA`/`nodeB` bằng `git show` (không cần checkout) → biên dịch
8 chương trình test trên PC cùng AddressSanitizer/UBSan → chạy theo đúng thứ tự phụ thuộc → in kết quả
→ ghi `pc_tests/build/report.md` (có mục "Hạn chế/lỗi đã tái hiện" để dán vào báo cáo).
Mã thoát `0` = không có FAIL. `-v` xem thêm ghi chú đo được; `--no-sanitize` nếu máy không hỗ trợ.
Hai node ở hai máy khác nhau cũng không sao: chỉ cần mỗi bên đã push lên nhánh của mình.

## Test cái gì

| Nhóm | Code thật được test | Nội dung chính |
|---|---|---|
| `event_id` | `event_id.h` (bản sửa lỗi trùng ID) | không trùng ID qua 200 lần reboot, reboot đúng ranh giới 32/64/96, số lần ghi flash, NVS lỗi |
| `uart_node_b` | B `comm_uart.c` | parser khung, sai checksum/END/LEN, 1000 khung ngẫu nhiên, lật 78.888 bit, 200.000 byte rác, gửi đúng từng byte, listener |
| `node_a_uart` | A `comm_uart.c` (mock `face_engine`/`app_state`/`cam_driver`) | **17 lệnh**: gọi đúng hàm, đúng tham số, ACK/NACK, thiếu payload, 300.000 byte rác không kích hoạt xóa người dùng |
| `node_b_to_a_uart` | cả hai | 17 lệnh do Node B sinh ra → Node A hiểu đúng, phản hồi đúng |
| `node_a_espnow_send` | A `comm_espnow.c` | chia mảnh 230 byte ở các biên, header từng mảnh, giải phóng bộ nhớ, gửi lỗi |
| `node_b_espnow_recv` | B `comm_espnow.c` | ghép mảnh: ngược thứ tự, trùng, thiếu (timeout 4s), 2 ảnh xen kẽ, ảnh thứ 3, gói sai, tràn hàng đợi |
| `node_a_to_b_espnow` | cả hai | 8 ảnh (1 B → 150 KB) do Node A chia mảnh → Node B ghép **đúng từng byte**; xáo trộn + trùng lặp |
| `sd_storage` | B `sd_storage.c` | lưu/đọc ảnh, tên file, tham số sai, ghi đè cùng ID, xoay vòng ảnh |
| `reboot_scenarios` | B `sd_storage.c` + `event_id.h` | tái hiện lỗi trùng ID (cách cũ) và chứng minh bản sửa hết lỗi |
| `node_b_access_events` | B `access_events.c` | lịch sử vòng tròn 32, subscriber, restore |
| `node_b_event_log` | B `sd_storage.c` (events.log) | định dạng dòng log, nạp lại, dòng hỏng, xoay vòng >256 KB liên tục qua ranh giới |
| `node_a_app_state` | A `app_state.c` (NVS giả) | lưu/nạp cấu hình qua reboot, ngưỡng không trôi sau 200 lần |
| `contract_A_vs_B` | `uart_protocol.h`, `espnow_frame.h` | hai node dùng định dạng khung giống hệt nhau |

### Đọc kết quả
- `PASS` / `FAIL`: như thường.
- `REPRO`: test **chứng minh một hạn chế/lỗi tồn tại** trên code hiện tại (kèm giải thích bên dưới dòng đó). Đây là kết quả *mong đợi*, không phải thất bại. Dùng làm bằng chứng "trước khi sửa".
- `NOREPRO`: hạn chế không còn tái hiện (ví dụ bạn đã sửa) – không tính là lỗi.

## Hạn chế/lỗi bộ test đã tái hiện trên code hiện tại (14 test `REPRO`, gom thành 12 mục)

| # | Node | Mô tả | Mức |
|---|---|---|---|
| 1 | A | `event_id` quay về 1 sau mỗi lần khởi động (đã có bản sửa `event_id.h`) | cao |
| 2 | B | Hậu quả của #1: ảnh bị ghi đè, và khi thẻ đầy thì ảnh **mới** bị xóa trước ảnh cũ (2 test) | cao |
| 3 | B | Nhật ký `events.log` chứa `event_id` trùng sau khi Node A khởi động lại | trung bình |
| 4 | B | Ghép mảnh khi `event_id` trùng với ảnh đang ghép dở: ảnh mới mất, hoặc ra **ảnh lai** 2 ảnh, không cảnh báo (2 test) | cao |
| 5 | A | `comm_espnow.c`: `jpeg_data != NULL` mà `jpeg_len == 0` thì **không `free`** → rò bộ nhớ | thấp |
| 6 | A | `app_state`: `liveness_enabled` **không lưu NVS**; Node B chỉ gửi lệnh khi người dùng bấm dashboard → sau mất điện, chống giả mạo âm thầm TẮT | **cao (an ninh)** |
| 7 | A | Handler UART chuyển nguyên `model_id=99`, ngưỡng NaN/-5, độ sáng 120 xuống tầng dưới (chưa biết tầng dưới có chặn không) | thấp |
| 8 | A | `app_state` nạp `model=7` từ NVS mà không kiểm tra miền giá trị | thấp |
| 9 | B | Ghép mảnh không kiểm tra `fragment_total == ceil(jpeg_len/230)`: ảnh "hoàn chỉnh" nhưng thiếu dữ liệu | trung bình |
| 10 | B | Mảnh giữa ngắn hơn 230 vẫn được chấp nhận → lỗ hổng trong ảnh | trung bình |
| 11 | B | Gói bị cắt ngắn hơn `payload_len` vẫn tính là mảnh đầy đủ (phần thiếu = 0) | thấp |
| 12 | B | Không có trần `jpeg_len`: khai báo 8 MB vẫn được cấp phát (PSRAM chỉ 8 MB) | trung bình |

Đã kiểm chứng và **không phải lỗi**: ngưỡng nhận diện không bị trôi qua 200 lần lưu/nạp NVS; độ tin cậy trong log lệch ≤ 0,001.

## Gắn bản sửa `event_id` vào Node A (nhánh `nodeA`)

1. Chép `pc_tests/event_id.h` → `components/face_engine/include/event_id.h`
   (khi file này đã có trong nodeA, script tự test **bản đó** thay vì bản đóng gói).
2. `components/face_engine/CMakeLists.txt`: thêm `nvs_flash` vào `REQUIRES`/`PRIV_REQUIRES`.
3. `components/face_engine/src/face_engine.cpp`:

```cpp
#include "nvs.h"
#include "event_id.h"

static event_id_gen_t s_event_gen;          // thay cho: static uint32_t s_event_id_counter = 0;

static bool evt_nvs_load(uint32_t *out, void *) {
    nvs_handle_t h;
    if (nvs_open("face_evt", NVS_READONLY, &h) != ESP_OK) return false;   // chưa có -> bắt đầu từ 0
    esp_err_t e = nvs_get_u32(h, "hwm", out);
    nvs_close(h);
    return e == ESP_OK;
}
static bool evt_nvs_store(uint32_t v, void *) {
    nvs_handle_t h;
    if (nvs_open("face_evt", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_u32(h, "hwm", v);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}
```
   - Trong `face_engine_init()`, trước `return ESP_OK;`:
     ```cpp
     if (!event_id_init(&s_event_gen, evt_nvs_load, evt_nvs_store, nullptr, EVENT_ID_DEFAULT_RESERVE))
         ESP_LOGW(TAG, "Khong luu duoc moc event_id vao NVS");
     ```
   - Hai chỗ `++s_event_id_counter` (cấp ID) đổi thành `event_id_next(&s_event_gen)`.

## Chưa test trên PC (để phần chip thật hoặc làm sau)
- Nhận diện khuôn mặt, liveness, camera, TFT, độ chính xác (FAR/FRR) – phụ thuộc phần cứng và ESP-DL.
- Logic quyết định mở khóa (`main/access_gateway.cpp`, C++/Matter), HTTP dashboard, Matter, servo/relay, reed switch.
- ESP-NOW qua sóng, thẻ SD FAT thật, NVS/flash thật, đa luồng FreeRTOS thật.

## Cách hoạt động / giới hạn
- `stubs/` giả lập tối thiểu ESP-IDF (FreeRTOS, UART, SDMMC, ESP-NOW, NVS, đồng hồ) để biên dịch nguyên file `.c` thật.
  Task FreeRTOS không chạy thật: test chạy tay hàm task rồi thoát vòng `while(1)` bằng `longjmp` khi hết dữ liệu.
- `sd_storage.h` chỉ bị đổi 2 hằng số trong bản sao: đường dẫn thẻ → `sdtest/`, `SD_STORAGE_MAX_IMAGES` → 5.
  Nếu bạn đổi tên 2 macro này, script báo lỗi rõ ràng.
- Thẻ SD giả là thư mục trên đĩa PC; không kiểm tra hành vi FAT/8.3 thật.
- Test "Node A → Node B" trao đổi qua file (`a_frags.bin`, `b_to_a.bin`) vì hai node có hàm trùng tên, không link chung 1 binary được.
