idf_component_register(
    SRCS "esp_lcd_qspi_amoled.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd"
    PRIV_REQUIRES "driver"
)
