// Battery voltage through a resistor divider on an ADC1 pin.
#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

// divider: battery voltage / pin voltage (2.0 for 100k/100k).
esp_err_t battery_init(gpio_num_t pin, float divider);

// Battery voltage in mV, averaged over a few readings; -1 on error.
// Calibrated with the chip's eFuse curve when available.
int battery_read_mv(void);
