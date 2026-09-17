idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app_state cam_driver display_driver overlay_render light_ctrl face_engine comm_uart comm_espnow nvs_flash spiffs
)
