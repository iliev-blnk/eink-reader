idf_component_register(
    SRCS "main.c" "screen_bringup.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES boards core drivers esp_driver_gpio esp_driver_spi esp_psram esp_timer spi_flash)
