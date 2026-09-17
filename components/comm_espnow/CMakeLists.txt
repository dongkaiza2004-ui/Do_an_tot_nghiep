idf_component_register(
    SRCS "src/comm_espnow.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event esp_wifi face_engine
)
