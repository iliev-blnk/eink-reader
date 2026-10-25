// microSD card over SPI (shared bus) with FAT, mounted into the VFS so the
// rest of the firmware uses plain stdio (fopen("/sd/...")).
#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;  // the bus must already be initialized
    gpio_num_t cs;
    const char *mount_point;  // e.g. "/sd"
} sdcard_config_t;

typedef struct {
    uint64_t capacity_bytes;
    int freq_khz;  // SPI clock the card runs at
    char name[8];  // product name from the card's CID
} sdcard_info_t;

// Mounts the card. Tries the normal 20 MHz first, then 5 MHz for long jumper
// wires. Never formats: a card without a FAT volume returns ESP_FAIL.
esp_err_t sdcard_mount(const sdcard_config_t *cfg, sdcard_info_t *info);

esp_err_t sdcard_unmount(void);
