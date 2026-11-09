idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES protocol comm_uart comm_espnow access_events sd_storage esp_http_server json
    EMBED_TXTFILES "web/dashboard.html"
)
