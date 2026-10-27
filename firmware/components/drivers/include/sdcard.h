// Cardul microSD pe SPI (magistrală comună) cu FAT, montat în VFS ca restul
// firmware-ului să folosească stdio simplu (fopen("/sd/...")).
#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;  // magistrala trebuie să fie deja inițializată
    gpio_num_t cs;
    const char *mount_point;  // de exemplu "/sd"
} sdcard_config_t;

typedef struct {
    uint64_t capacity_bytes;
    int freq_khz;  // frecvența SPI la care merge cardul
    char name[8];  // numele produsului din CID-ul cardului
} sdcard_info_t;

// Montează cardul. Încearcă întâi 20 MHz, frecvența normală, apoi 5 MHz pentru
// firele lungi de pe breadboard. Nu formatează niciodată: un card fără volum FAT
// întoarce ESP_FAIL.
esp_err_t sdcard_mount(const sdcard_config_t *cfg, sdcard_info_t *info);

esp_err_t sdcard_unmount(void);
