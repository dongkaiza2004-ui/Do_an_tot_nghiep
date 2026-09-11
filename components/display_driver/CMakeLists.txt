# Khai báo component cho ESP-IDF
idf_component_register(
    # 1. Liệt kê tất cả các file mã nguồn (.c) nằm trong thư mục src/
    SRCS 
        "src/display_driver.c"

    # 2. Thư mục chứa các file .h CÔNG KHAI (Public Headers)
    # Các file khác từ main.c hoặc component khác có thể #include các file trong này
    INCLUDE_DIRS 
        "include"

    # 3. Thư mục chứa các file .h NỘI BỘ (Private Headers)
    # Cho phép các file .c trong src/ truy cập file như display_internal.h
    PRIV_INCLUDE_DIRS 
        "."

    # 4. Liệt kê các thư viện phụ thuộc (Dependencies)
    # Component này dùng các API của esp_lcd, esp_lvgl_port và driver (PWM đèn nền)
    REQUIRES 
        esp_lcd 
        esp_lvgl_port
        driver
)