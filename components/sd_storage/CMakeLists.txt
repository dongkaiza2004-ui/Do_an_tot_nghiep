idf_component_register(SRCS "src/sd_storage.c" "src/time_sync.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio access_events
                       PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc vfs esp_netif esp_event)
