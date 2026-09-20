idf_component_register(
    SRCS "src/comm_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES driver app_state face_engine cam_driver
)
