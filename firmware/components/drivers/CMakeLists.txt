idf_component_register(
    SRCS "epd_uc8253.c" "sdcard.c" "buttons.c" "battery.c" "usbdisk.c" "fuelgauge.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi sdmmc
    PRIV_REQUIRES esp_adc esp_driver_i2c esp_driver_sdspi esp_pm esp_timer fatfs)
