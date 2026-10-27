// Tensiunea bateriei printr-un divizor rezistiv pe un pin ADC1.
#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

// divizorul: tensiunea bateriei / tensiunea pe pin (2,0 pentru 100k/100k).
esp_err_t battery_init(gpio_num_t pin, float divider);

// Tensiunea bateriei în mV, media câtorva citiri; -1 la eroare.
// Calibrată cu curba din eFuse a cipului, când există.
int battery_read_mv(void);
