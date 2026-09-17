idf_component_register(
    SRCS "src/light_ctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc app_state
)
