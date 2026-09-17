idf_component_register(
    SRCS "src/face_engine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES human_face_detect human_face_recognition esp32-camera app_state esp_timer
)
