// Indicatorul de baterie MAX17048 (ModelGauge), pe I2C. Doar pe placa proprie.
#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

// Pornește magistrala I2C și verifică registrul VERSION. ESP_ERR_NOT_FOUND
// dacă nu răspunde nimic la adresa 0x36.
esp_err_t fuelgauge_init(gpio_num_t sda, gpio_num_t scl);

// Tensiunea celulei în mV, sau -1.
int fuelgauge_read_mv(void);

// Starea de încărcare în zecimi de procent (0–1000), sau -1. În primele secunde
// după ce indicatorul primește tensiune, valoarea e doar o estimare din tensiune.
int fuelgauge_read_soc_permille(void);
