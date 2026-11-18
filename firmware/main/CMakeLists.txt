idf_component_register(
    SRCS "main.c" "bringup.c" "board_io.c" "screen_bringup.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES boards core drivers ui esp_driver_gpio esp_driver_spi esp_pm esp_psram esp_timer nvs_flash spi_flash)
