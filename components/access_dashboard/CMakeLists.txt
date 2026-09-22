idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES protocol comm_uart comm_espnow access_events esp_http_server json
)
