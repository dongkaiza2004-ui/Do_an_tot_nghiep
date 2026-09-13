idf_component_register(
    SRCS "src/sys_monitor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_timer app_update ui_menu lvgl spiffs
)