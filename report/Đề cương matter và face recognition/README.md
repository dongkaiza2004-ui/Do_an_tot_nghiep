# Báo cáo Thiết kế Hệ thống Khóa cửa Thông minh
## Tích hợp chuẩn Matter và Nhận diện khuôn mặt trên nền tảng ESP32-S3

---

## 1. Giới thiệu tổng quan

Tài liệu này mô tả kiến trúc và nguyên lý hoạt động của hệ thống khóa cửa thông minh, được xây dựng trên hai vi điều khiển ESP32-S3 phối hợp với nhau thông qua giao tiếp UART. Hệ thống tích hợp chuẩn kết nối **Matter**, cho phép người dùng giám sát và điều khiển khóa cửa thông qua các nền tảng nhà thông minh phổ biến như Apple Home, Google Home và Amazon Alexa.

Điểm cốt lõi của hệ thống là khả năng nhận diện khuôn mặt để xác thực người dùng và tự động điều khiển việc đóng/mở khóa, kết hợp với khả năng quản lý từ xa thông qua ứng dụng di động.

### 1.1. Mục tiêu thiết kế

- Tách biệt rõ ràng giữa khối xử lý hình ảnh/trí tuệ nhân tạo và khối điều khiển khóa/kết nối mạng, nhằm đảm bảo tính ổn định và dễ bảo trì.
- Đảm bảo mọi thao tác điều khiển từ ứng dụng đều đi qua một điểm truy cập duy nhất, có kiểm soát.
- Đảm bảo quyết định mở khóa được xử lý với độ trễ thấp, không phụ thuộc vào các tác vụ nặng như truyền tải hình ảnh.
- Tuân thủ mô hình dữ liệu chuẩn của Matter để đảm bảo khả năng tương thích với các hệ sinh thái nhà thông minh hiện có.

### 1.2. Phạm vi tài liệu

Tài liệu tập trung mô tả kiến trúc phần cứng ở mức hệ thống, vai trò của từng thành phần, giao thức truyền thông giữa các thành phần, và các luồng nghiệp vụ chính. Tài liệu không đi sâu vào chi tiết triển khai mã nguồn.

---

## 2. Kiến trúc tổng thể

Hệ thống được chia thành hai thành phần điều khiển chính, gọi là **Node A** và **Node B**, giao tiếp với nhau qua hai kênh song song, cùng với sự tham gia của ứng dụng người dùng thông qua chuẩn Matter.

```
┌──────────────┐    UART (lệnh điều khiển & sự kiện)    ┌─────────────────────┐    Matter (Wi-Fi/BLE)     ┌─────────────┐
│    Node A    │◄──────────────────────────────────────►│      Node B         │◄─────────────────────────►│  Ứng dụng   │
│ Camera + AI  │                                        │ Matter + Điều khiển │                           │  người dùng │
│  + Màn hình  │──ESP-NOW (1 ảnh cho mỗi sự kiện)──────►│      khóa cửa       │                           └─────────────┘
│   TFT tại    │                                        │                     │
│     chỗ      │                                        └─────────┬───────────┘ 
└──────────────┘                                                  │
                                                        ┌─────────┴─────────┐
                                                        │  Relay/Motor khóa │
                                                        │  Cảm biến cửa     │
                                                        └───────────────────┘
```

### 2.1. Nguyên tắc phân vai

Hệ thống tuân theo nguyên tắc phân tách trách nhiệm rõ ràng:

- **Node A** đảm nhiệm việc quan sát và nhận diện. Node A không có kết nối mạng, không nhận biết sự tồn tại của ứng dụng, và không có quyền tự quyết định việc mở khóa.
- **Node B** đóng vai trò trung tâm điều phối và là điểm giao tiếp duy nhất với thế giới bên ngoài. Mọi yêu cầu từ ứng dụng đều được xử lý tại Node B trước khi được chuyển tiếp xuống Node A nếu cần thiết.

Cách phân chia này giúp giới hạn bề mặt tấn công của thành phần điều khiển khóa vật lý, đồng thời cho phép nâng cấp hoặc thay đổi từng thành phần một cách độc lập.

---

## 3. Node A — Đơn vị xử lý hình ảnh và nhận diện

### 3.1. Chức năng đảm nhiệm

| Chức năng | Mô tả |
|---|---|
| Thu nhận hình ảnh | Điều khiển camera, thu khung hình liên tục |
| Nhận diện khuôn mặt | Chạy mô hình trí tuệ nhân tạo để trích xuất và so khớp đặc trưng khuôn mặt |
| Hiển thị cục bộ | Xuất hình ảnh trực tiếp lên màn hình TFT tại chỗ, độc lập với các thành phần khác |
| Lưu trữ dữ liệu người dùng | Duy trì cơ sở dữ liệu ánh xạ giữa mã người dùng (user ID) và đặc trưng khuôn mặt (embedding) trên bộ nhớ nội bộ (NVS/SPIFFS) |
| Đăng ký người dùng mới | Thực hiện quy trình thu thập và lưu đặc trưng khuôn mặt khi nhận được yêu cầu từ Node B |
| Báo cáo kết quả nhận diện | Gửi sự kiện nhận diện (khớp, không xác định, hoặc nghi ngờ giả mạo) về Node B ngay khi có kết quả |
| Ghi nhận hình ảnh sự kiện | Chụp một khung hình duy nhất tại thời điểm phát sinh sự kiện nhận diện, truyền đi qua kênh ESP-NOW |
| Hỗ trợ nhiều ảnh cho một người dùng | Cho phép đăng ký nhiều lần cho cùng một mã người dùng (nhiều góc mặt/điều kiện ánh sáng khác nhau) nhằm tăng độ chính xác nhận diện |
| Hỗ trợ nhiều mô hình nhận diện | Duy trì song song hai cơ sở dữ liệu đặc trưng khuôn mặt riêng biệt, tương ứng với từng mô hình nhận diện; việc chuyển đổi mô hình không làm mất dữ liệu đã đăng ký của mô hình còn lại |
| Kiểm tra chống giả mạo | Khi được kích hoạt, yêu cầu người dùng thực hiện một cử chỉ xác nhận (quay đầu) trước khi công nhận kết quả nhận diện là hợp lệ |

### 3.2. Giới hạn phạm vi trách nhiệm

Node A không thiết lập kết nối Wi-Fi, Bluetooth, hoặc bất kỳ giao thức mạng nào ra bên ngoài. Node A cũng không tham gia vào quá trình ra quyết định mở hay khóa cửa — vai trò của Node A giới hạn ở việc cung cấp thông tin nhận diện chính xác và kịp thời cho Node B.

### 3.3. Giới hạn dung lượng lưu trữ

Dữ liệu đặc trưng khuôn mặt được lưu trên một phân vùng flash riêng (SPIFFS), tách biệt hoàn toàn với phân vùng chứa firmware. Với dung lượng phân vùng lưu trữ hiện tại, hệ thống có thể chứa được khoảng vài trăm vector đặc trưng, tương đương vài trăm người dùng nếu mỗi người chỉ đăng ký một ảnh, hoặc ít hơn tương ứng nếu đăng ký nhiều ảnh mỗi người. Do dữ liệu của hai mô hình nhận diện được lưu trong hai tệp riêng biệt trên cùng phân vùng, dung lượng khả dụng cho mỗi mô hình được chia theo tỷ lệ tương ứng. Con số cụ thể phụ thuộc vào cấu hình phân vùng flash được lựa chọn khi build firmware và có thể điều chỉnh tăng thêm nếu cần, do vi điều khiển còn dư nhiều dung lượng flash chưa sử dụng.

---

## 4. Node B — Đơn vị điều khiển khóa và giao tiếp Matter

### 4.1. Chức năng đảm nhiệm

| Chức năng | Mô tả |
|---|---|
| Triển khai giao thức Matter | Khởi tạo và duy trì Door Lock Cluster theo đặc tả Matter, cùng các cluster bắt buộc và tùy chọn liên quan |
| Tiếp nhận lệnh từ ứng dụng | Xử lý các lệnh như thêm/xóa người dùng, khóa/mở thủ công, truy vấn trạng thái |
| Điều phối với Node A | Chuyển tiếp các yêu cầu cấu hình và đăng ký người dùng xuống Node A khi cần thiết |
| Ra quyết định mở khóa | Khi nhận được sự kiện nhận diện thành công từ Node A, kiểm tra tính hợp lệ của người dùng (quyền truy cập, khung giờ cho phép) trước khi điều khiển mở khóa |
| Điều khiển cơ cấu chấp hành | Truyền tín hiệu điều khiển đến relay hoặc động cơ điều khiển chốt khóa vật lý |
| Giám sát trạng thái cửa | Đọc tín hiệu từ cảm biến cửa (reed switch) để cập nhật trạng thái đóng/mở thực tế |
| Ghi nhận sự kiện và cảnh báo | Lưu lại lịch sử truy cập, phát sinh cảnh báo khi có dấu hiệu bất thường |
| Tiếp nhận và chuyển tiếp hình ảnh | Nhận ảnh chụp sự kiện từ Node A, đối chiếu với nhật ký sự kiện tương ứng, chuyển tiếp đến ứng dụng hoặc máy chủ lưu trữ |

### 4.2. Vai trò trung tâm

Node B là thành phần duy nhất trong hệ thống có kết nối mạng và tương tác trực tiếp với ứng dụng người dùng. Toàn bộ quyết định liên quan đến việc cấp quyền truy cập, thời gian hiệu lực, và việc điều khiển cơ cấu khóa vật lý đều được xử lý tập trung tại đây.

---

## 5. Các kênh giao tiếp trong hệ thống

| Kênh truyền | Thành phần liên quan | Mục đích sử dụng |
|---|---|---|
| UART | Node A ↔ Node B | Truyền lệnh cấu hình, yêu cầu đăng ký người dùng, và sự kiện nhận diện theo thời gian thực |
| ESP-NOW | Node A → Node B | Truyền một khung hình chụp lại mỗi khi phát sinh sự kiện nhận diện |
| Matter (Wi-Fi/BLE) | Node B ↔ Ứng dụng | Ghép nối thiết bị, điều khiển khóa, truy vấn trạng thái, quản lý người dùng |
| Tín hiệu số (GPIO) | Node B → Relay/Động cơ | Điều khiển đóng/mở chốt khóa vật lý |
| Tín hiệu số (GPIO) | Cảm biến cửa → Node B | Cập nhật trạng thái cửa đóng/mở |

**Nguyên tắc thiết kế quan trọng:** quyết định mở khóa không được phép phụ thuộc vào việc truyền tải hình ảnh. Sự kiện nhận diện qua UART có dung lượng nhỏ, được ưu tiên xử lý ngay lập tức; hình ảnh sự kiện gửi qua ESP-NOW chỉ phục vụ mục đích lưu trữ và tra cứu lại sau này, không nằm trên đường xử lý then chốt của hệ thống.

---

## 6. Giao thức truyền thông UART

### 6.1. Cấu trúc khung truyền

```
| START (0xAA) | CMD (1 byte) | LEN (1 byte) | PAYLOAD (0–64 byte) | CHECKSUM (1 byte) | END (0x55) |
```

Việc phân tích khung truyền tại bên nhận phải được thực hiện theo cơ chế máy trạng thái, dựa trên trường độ dài (LEN) để xác định chính xác ranh giới của phần dữ liệu, thay vì tìm kiếm byte kết thúc (0x55) trong luồng dữ liệu. Cách tiếp cận này là cần thiết vì giá trị của byte bắt đầu và byte kết thúc hoàn toàn có thể trùng với dữ liệu bên trong phần payload.

Khi truyền đi, bên gửi chỉ truyền đúng số byte dữ liệu thực tế theo trường LEN, không truyền toàn bộ vùng đệm cố định. Giá trị checksum được tính theo phép toán XOR trên các trường CMD, LEN và PAYLOAD.

### 6.2. Danh mục lệnh điều khiển — Node B gửi xuống Node A

| Mã lệnh | Tên hằng số | Mô tả và dữ liệu kèm theo |
|---|---|---|
| `0x01` | `CMD_SET_MODEL` | Chọn mô hình nhận diện — tham số: mã mô hình |
| `0x02` | `CMD_SET_THRESHOLD` | Thiết lập ngưỡng tương đồng để xác định khớp mặt — tham số: giá trị ngưỡng |
| `0x03` | `CMD_SET_BRIGHTNESS` | Điều chỉnh độ sáng camera — tham số: giá trị điều chỉnh |
| `0x04` | `CMD_SET_CONTRAST` | Điều chỉnh độ tương phản camera — tham số: giá trị điều chỉnh |
| `0x05` | `CMD_SET_FLIP` | Bật/tắt lật ảnh — tham số: bật/tắt |
| `0x06` | `CMD_SET_MIRROR` | Bật/tắt hiệu ứng gương — tham số: bật/tắt |
| `0x07` | `CMD_SHOW_FPS` | Bật/tắt hiển thị tốc độ khung hình trên màn hình TFT — tham số: bật/tắt |
| `0x08` | `CMD_ADD_USER_START` | Bắt đầu quy trình đăng ký người dùng mới — tham số: mã người dùng, tên hiển thị |
| `0x09` | `CMD_ADD_USER_CAPTURE` | Xác nhận chụp ảnh đăng ký (sau khi người dùng xác nhận trên ứng dụng) |
| `0x0A` | `CMD_ADD_USER_CANCEL` | Hủy quy trình đăng ký đang thực hiện |
| `0x0B` | `CMD_SUB_USER` | Xóa một người dùng khỏi hệ thống — tham số: mã người dùng |
| `0x0C` | `CMD_LIST_USER_REQ` | Yêu cầu trả về danh sách người dùng hiện có |
| `0x0D` | `CMD_START_STREAM` | *Dự phòng, chưa sử dụng trong phiên bản hiện tại — dùng cho tính năng truyền hình ảnh liên tục nếu triển khai trong tương lai* |
| `0x0E` | `CMD_STOP_STREAM` | *Dự phòng, đi kèm với lệnh 0x0D* |
| `0x0F` | `CMD_CLEAR_ALL_USERS` | Xóa toàn bộ người dùng và dữ liệu đặc trưng khuôn mặt đã đăng ký của mô hình đang sử dụng (không kèm tham số, dùng chủ yếu trong giai đoạn thử nghiệm/vận hành lại từ đầu) |
| `0x10` | `CMD_ENABLE_LIVENESS_CHECK` | Bật/tắt cơ chế kiểm tra chống giả mạo — tham số: bật/tắt |

> **Ghi chú:** hai mô hình nhận diện hiện có (chọn qua `CMD_SET_MODEL`) có sự đánh đổi giữa độ chính xác và tốc độ xử lý — mô hình có độ chính xác cao hơn có thời gian suy luận lâu hơn đáng kể trên phần cứng ESP32-S3, dẫn đến hiển thị hình ảnh trên Node A bị khựng rõ rệt hơn trong lúc xử lý nhận diện. Việc lựa chọn mô hình cần cân nhắc giữa độ chính xác mong muốn và trải nghiệm hiển thị thời gian thực.

### 6.3. Danh mục phản hồi và sự kiện — Node A gửi lên Node B

| Mã lệnh | Tên hằng số | Mô tả và dữ liệu kèm theo |
|---|---|---|
| `0x80` | `RSP_ACK` | Xác nhận thực hiện thành công — tham số: mã lệnh gốc |
| `0x81` | `RSP_NACK` | Xác nhận thực hiện thất bại — tham số: mã lệnh gốc, mã lỗi |
| `0x82` | `RSP_ADD_USER_RESULT` | Kết quả đăng ký người dùng — tham số: trạng thái thành công, mã người dùng, tên |
| `0x83` | `RSP_SUB_USER_RESULT` | Kết quả xóa người dùng — tham số: trạng thái thành công, mã người dùng |
| `0x84` | `RSP_USER_LIST_ITEM` | Một mục trong danh sách người dùng — tham số: mã người dùng, tên, cờ đánh dấu mục cuối |
| `0x85` | `RSP_USER_LIST_END` | Báo hiệu đã truyền hết danh sách người dùng |
| `0x86` | `RSP_FPS_REPORT` | Báo cáo định kỳ tốc độ khung hình — tham số: giá trị FPS |
| `0x87` | `RSP_FACE_MATCHED` | Nhận diện thành công — tham số: mã sự kiện, mã người dùng, độ tin cậy |
| `0x88` | `RSP_FACE_UNKNOWN` | Không xác định được người dùng — tham số: mã sự kiện |
| `0x89` | `RSP_LIVENESS_FAIL` | Nghi ngờ giả mạo (sử dụng ảnh hoặc video để đánh lừa hệ thống) — tham số: mã sự kiện |

> **Ghi chú:** ba lệnh `RSP_FACE_MATCHED`, `RSP_FACE_UNKNOWN` và `RSP_LIVENESS_FAIL` là các bổ sung cần thiết so với danh mục lệnh ban đầu, nhằm đảm bảo Node B nhận được thông tin về kết quả nhận diện theo thời gian thực — đây là điều kiện tiên quyết để hệ thống có thể tự động ra quyết định mở khóa.

Trường **mã sự kiện (event ID)** trong các lệnh trên có giá trị tăng dần theo từng sự kiện phát sinh, được sử dụng để đối chiếu với hình ảnh tương ứng gửi qua kênh ESP-NOW.

### 6.4. Cấu trúc gói tin hình ảnh qua ESP-NOW

```
Phần đầu:  mã sự kiện | chiều rộng ảnh | chiều cao ảnh | độ dài dữ liệu JPEG
Phần thân: dữ liệu ảnh định dạng JPEG
```

### 6.5. Cơ chế kiểm tra chống giả mạo

Khi tính năng chống giả mạo được kích hoạt (`CMD_ENABLE_LIVENESS_CHECK`), Node A áp dụng phương thức xác thực chủ động kiểu thách thức–phản hồi (challenge–response), được thực hiện **hoàn toàn nội bộ tại Node A**, không cần Node B điều phối từng bước — phù hợp với nguyên tắc Node A tự chủ trong việc hiển thị và xác thực cục bộ (§2.1, §3.1):

1. Ngay khi nhận diện khớp một người dùng đã đăng ký, thay vì báo kết quả ngay lập tức, Node A hiển thị hướng dẫn lên màn hình TFT tại chỗ, yêu cầu người dùng thực hiện động tác quay đầu.
2. Node A theo dõi vị trí tương đối giữa các điểm mốc trên khuôn mặt (mắt, mũi) qua một chuỗi khung hình liên tiếp trong một khoảng thời gian giới hạn, nhằm xác nhận có sự thay đổi góc nhìn khuôn mặt tương ứng với chuyển động quay đầu thật — một khuôn mặt phẳng (ảnh in, ảnh hiển thị trên màn hình) khi bị xoay hoặc di chuyển trước camera không tạo ra kiểu biến đổi tương đối này giữa các điểm mốc.
3. Nếu phát hiện chuyển động hợp lệ trong thời gian cho phép, Node A mới gửi `RSP_FACE_MATCHED` như bình thường. Nếu hết thời gian mà không phát hiện được, Node A gửi `RSP_LIVENESS_FAIL` thay thế.

**Giới hạn đã biết:** cơ chế này chống được các hình thức giả mạo phổ biến bằng ảnh tĩnh (ảnh in, ảnh hiển thị trên màn hình thiết bị di động), nhưng không chống được video quay sẵn có ghi lại động tác quay đầu của người thật. Phương án dựa trên phân tích độ sắc nét/kết cấu ảnh (nhằm phát hiện ảnh in hoặc ảnh chụp lại qua màn hình) đã được thử nghiệm nhưng bị loại bỏ do độ phân giải camera hiện tại không đủ để tạo ra sự khác biệt đáng tin cậy giữa ảnh thật và ảnh giả mạo ở điều kiện ánh sáng thông thường.

---

## 7. Các luồng nghiệp vụ chính

### 7.1. Đăng ký người dùng mới

1. Ứng dụng gửi yêu cầu thêm người dùng và thông tin xác thực thông qua Matter đến Node B.
2. Node B chuyển tiếp yêu cầu bắt đầu đăng ký xuống Node A.
3. Node A kích hoạt chế độ chờ, tiến hành quét khuôn mặt.
4. Khi người dùng xác nhận trên ứng dụng, Node B gửi lệnh xác nhận chụp xuống Node A.
5. Node A thực hiện chụp, trích xuất đặc trưng khuôn mặt, lưu trữ cục bộ, và gửi kết quả về Node B.
6. Node B cập nhật thông tin xác thực trên Matter Cluster và phản hồi kết quả về ứng dụng.

### 7.2. Nhận diện và mở khóa tự động

1. Node A liên tục thực hiện nhận diện khuôn mặt trong nền, đồng thời hiển thị hình ảnh trực tiếp trên màn hình TFT.
2. Khi phát sinh kết quả nhận diện khớp một người dùng, nếu tính năng chống giả mạo đang được kích hoạt, Node A thực hiện quy trình xác thực bằng thách thức–phản hồi mô tả tại §6.5 trước khi báo cáo kết quả; nếu tính năng này đang tắt, Node A báo cáo kết quả ngay.
3. Node A gán một mã sự kiện mới, chụp lại một khung hình tại thời điểm đó.
4. Node A gửi sự kiện nhận diện qua UART đến Node B; đây là dữ liệu được ưu tiên xử lý do có dung lượng nhỏ và yêu cầu độ trễ thấp.
5. Node A đồng thời gửi hình ảnh sự kiện qua ESP-NOW đến Node B; quá trình này có thể hoàn tất muộn hơn một chút do dung lượng dữ liệu lớn hơn.
6. Khi nhận được sự kiện nhận diện thành công, Node B kiểm tra tính hợp lệ của người dùng (trạng thái kích hoạt, khung giờ được phép truy cập). Nếu hợp lệ, Node B điều khiển mở khóa và cập nhật trạng thái; nếu không hợp lệ, Node B từ chối và ghi nhận cảnh báo.
7. Khi nhận được sự kiện không xác định hoặc nghi ngờ giả mạo lặp lại nhiều lần, Node B ghi nhận cảnh báo và thông báo đến ứng dụng thông qua sự kiện Matter.
8. Khi nhận được hình ảnh tương ứng qua ESP-NOW, Node B đối chiếu mã sự kiện với nhật ký đã ghi nhận và chuyển tiếp hình ảnh đến ứng dụng hoặc hệ thống lưu trữ.

### 7.3. Xóa người dùng

Ứng dụng gửi yêu cầu xóa người dùng qua Matter đến Node B. Node B chuyển tiếp yêu cầu xuống Node A để xóa dữ liệu đặc trưng khuôn mặt tương ứng. Sau khi nhận kết quả từ Node A, Node B cập nhật lại thông tin trên Matter Cluster và phản hồi về ứng dụng.

### 7.4. Truy vấn danh sách người dùng

Ứng dụng gửi yêu cầu đến Node B, Node B chuyển tiếp xuống Node A. Node A trả về lần lượt từng người dùng, kết thúc bằng tín hiệu báo hết danh sách. Node B tổng hợp dữ liệu và trả kết quả về ứng dụng.

### 7.5. Điều khiển khóa thủ công

Khi ứng dụng gửi lệnh khóa hoặc mở khóa trực tiếp thông qua Matter, Node B xử lý ngay lập tức mà không cần thông qua Node A, do đây là thao tác không liên quan đến việc nhận diện khuôn mặt.

### 7.6. Giám sát trạng thái cửa

Node B liên tục đọc tín hiệu từ cảm biến cửa để cập nhật trạng thái đóng/mở thực tế, phản ánh lên thuộc tính tương ứng trên Matter Cluster để ứng dụng có thể theo dõi theo thời gian thực. Node A không tham gia vào quy trình này.

---

## 8. Bảng phân chia trách nhiệm tổng hợp

| Chức năng | Node A | Node B |
|---|:---:|:---:|
| Thu nhận hình ảnh và nhận diện khuôn mặt | Có | Không |
| Hiển thị hình ảnh trực tiếp trên màn hình | Có | Không |
| Lưu trữ dữ liệu đặc trưng khuôn mặt | Có | Không |
| Kết nối Wi-Fi / Bluetooth / Matter | Không | Có |
| Tiếp nhận lệnh từ ứng dụng | Không | Có (điểm truy cập duy nhất) |
| Điều khiển cơ cấu chấp hành khóa | Không | Có |
| Đọc cảm biến trạng thái cửa | Không | Có |
| Ra quyết định cấp quyền mở khóa | Không (chỉ báo cáo) | Có |
| Ghi nhận và truyền ảnh sự kiện | Có (thu và truyền) | Có (tiếp nhận và chuyển tiếp) |
| Ghi nhận và xử lý cảnh báo | Báo cáo sự kiện gốc | Tổng hợp, đánh giá mức độ, thông báo |

---

## 9. Định hướng phát triển trong tương lai

- Mở rộng phương thức xác thực bổ sung, chẳng hạn thẻ RFID hoặc vân tay, thông qua việc bổ sung lệnh mới tại Node A mà không ảnh hưởng đến phần xử lý Matter tại Node B.
- Nâng cấp cơ chế chống giả mạo hiện tại (§6.5) lên mức đảm bảo hơn nếu yêu cầu an ninh của hệ thống tăng cao — cơ chế thách thức–phản hồi bằng quay đầu hiện tại không chống được tấn công bằng video quay sẵn có người thật thực hiện đúng động tác. Hướng nâng cấp có thể bao gồm sử dụng mô hình trí tuệ nhân tạo chuyên biệt cho phát hiện giả mạo, hoặc bổ sung cảm biến chuyên dụng (ví dụ camera hồng ngoại/depth).
- Chuyển đổi phương thức kết nối mạng từ Wi-Fi sang Thread nếu cần, việc này chỉ yêu cầu thay đổi tại Node B.
- Nâng cấp thuật toán kiểm tra tính toàn vẹn dữ liệu trên UART (ví dụ chuyển từ XOR sang CRC8) nếu phát sinh lỗi truyền trong thực tế vận hành.
- Xây dựng phương án ánh xạ cụ thể giữa chỉ số người dùng/thông tin xác thực trên Matter Cluster với mã người dùng nội bộ, có tính đến giới hạn số lượng bản ghi mà cluster hỗ trợ.
- Kích hoạt lại cơ chế truyền hình ảnh liên tục (`CMD_START_STREAM` / `CMD_STOP_STREAM`) trong trường hợp có nhu cầu giám sát trực tiếp thay vì chỉ ghi nhận ảnh theo sự kiện.
- Mở rộng dung lượng phân vùng lưu trữ đặc trưng khuôn mặt (§3.3) nếu quy mô số lượng người dùng thực tế vượt quá khả năng hiện tại.

---

## Phụ lục: Công cụ hỗ trợ phát triển

Trong quá trình phát triển, một công cụ PC (Python, giao diện đồ họa) đã được xây dựng để gửi/nhận trực tiếp các khung UART theo đúng giao thức mô tả tại mục 6, phục vụ việc kiểm thử Node A độc lập khi chưa có Node B thật. Công cụ hiển thị đầy đủ log hai chiều (khung gửi/nhận dạng thô và đã giải mã), hỗ trợ toàn bộ các lệnh trong bảng §6.2 và giải mã toàn bộ các phản hồi/sự kiện trong bảng §6.3. Công cụ này không phải một phần của hệ thống vận hành thực tế, chỉ phục vụ mục đích phát triển và kiểm thử.

---

## 10. Kết luận

Kiến trúc hai node với sự phân tách rõ ràng giữa khối xử lý nhận diện và khối điều khiển kết nối mang lại sự ổn định, khả năng bảo trì độc lập, và mức độ an toàn phù hợp cho một hệ thống khóa cửa. Việc tuân thủ chuẩn Matter đảm bảo khả năng tương thích rộng rãi với các hệ sinh thái nhà thông minh hiện có, trong khi giao thức UART nội bộ được thiết kế để đảm bảo độ trễ thấp cho quyết định mở khóa — yếu tố quan trọng nhất đối với một sản phẩm an ninh.
