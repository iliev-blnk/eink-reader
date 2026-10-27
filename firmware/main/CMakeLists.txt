idf_component_register(
    SRCS "main.c" "bringup.c" "screen_bringup.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES boards core drivers ui esp_driver_gpio esp_driver_spi esp_psram esp_timer nvs_flash spi_flash)
