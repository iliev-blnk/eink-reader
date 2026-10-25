// Driver for the UC8253 e-paper controller on the GDEY037T03 panel
// (WeAct 3.7", 240x416, black/white), 4-wire SPI.
//
// Written for this project from the UC8253 command set. The command values and
// timings were cross-checked against Good Display's sample code and GxEPD2
// (GPL-3.0, Jean-Marc Zingg); no code is copied from either.
//
// The framebuffer is in the panel's native format and orientation: 240 px wide,
// 416 rows, 8 px per byte, MSB = leftmost, bit 1 = white (the same as gfx_t).
// Not thread-safe: call from one task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#define EPD_WIDTH 240
#define EPD_HEIGHT 416
#define EPD_FB_SIZE (EPD_WIDTH / 8 * EPD_HEIGHT)

typedef struct {
    spi_host_device_t host;  // the bus must already be initialized
    gpio_num_t cs, dc, rst, busy;
    int clock_hz;
    // Full refresh with a forced-temperature waveform: ~1 s instead of ~3 s,
    // but outside the panel's specified low-temperature range.
    bool fast_full;
} epd_config_t;

// What the last refresh did and how long each phase took (for PERF logs and
// experiment E1). The window is the area actually refreshed after alignment.
typedef struct {
    bool full;
    int x, y, w, h;
    uint32_t write_us;      // SPI transfer of image data (both RAMs)
    uint32_t power_on_us;   // BUSY time after POWER ON
    uint32_t refresh_us;    // BUSY time after DISPLAY REFRESH: the visible update
    uint32_t power_off_us;  // BUSY time after POWER OFF
    uint32_t total_us;      // the whole call
} epd_timing_t;

// Adds the SPI device, configures the pins, resets the panel and checks that
// it answers (one power on/off cycle). Call it before adding other devices to
// the same bus, so the panel's CS is driven high from the start.
// Errors: ESP_ERR_INVALID_RESPONSE if BUSY never reacts (wiring or SPI mode),
// ESP_ERR_TIMEOUT if BUSY stays low.
esp_err_t epd_init(const epd_config_t *cfg);

// Full refresh: the panel flashes and every pixel is redrawn, clearing ghosting.
esp_err_t epd_full_refresh(const uint8_t *fb);

// Partial refresh of a window, without flashing. x and w are widened to whole
// bytes. Falls back to a full refresh when the controller doesn't hold the
// current screen yet (first refresh after init or after epd_sleep).
esp_err_t epd_partial_refresh(const uint8_t *fb, int x, int y, int w, int h);

// Puts the controller in deep sleep, its lowest-current state, and holds RST
// high so it stays there while the ESP32 sleeps too. The next refresh wakes it
// with a hardware reset and is always a full refresh, because the controller's
// image RAM isn't trusted after deep sleep (to be checked on the real panel).
esp_err_t epd_sleep(void);

const epd_timing_t *epd_last_timing(void);
