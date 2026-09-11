# face_engine

Lớp duy nhất biết chi tiết về esp-dl / HumanFaceDetect / HumanFaceRecognizer.

## Vai trò
- Khởi tạo và quản lý các model detect + recognition (có thể nhiều model
  cùng lúc, ví dụ MFN_S8_V1 và MBF_S8_V1).
- API để đổi model đang dùng tại runtime (switch_recognition_model,
  switch_detect_model) mà không cần rebuild app.
- API set/get threshold khớp mặt.
- API enroll / recognize / xoá khuôn mặt, thao tác trên database
  (SPIFFS face.db).
- Nhận frame từ cam_driver, trả kết quả (bounding box, id, similarity)
  qua queue cho display_driver và comm_espnow dùng.

## Không làm gì
- Không tự đọc Serial/UART, không tự vẽ.

## Phụ thuộc
- espressif__human_face_detect, espressif__human_face_recognition
- app_config (đọc model/threshold hiện tại)
- app_events