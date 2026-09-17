idf_component_register(
    SRCS "src/display_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_st7735 driver
)
