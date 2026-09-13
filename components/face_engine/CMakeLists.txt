idf_component_register(
    SRCS "src/face_engine.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES cam_driver human_face_detect human_face_recognition
                  ui_menu esp_lvgl_port lvgl spiffs
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
)