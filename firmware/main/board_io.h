// Ce ține de placă și e comun cititorului și aplicației de punere în funcțiune:
// alimentarea cardului și a panoului, plus magistralele lor SPI.
#pragma once

#include "esp_err.h"

// Pornește șinele cardului și panoului (doar pe plăcile cu comutatoare) și
// eliberează pinii ținuți pe 0 în somn. Prima, înaintea oricărui alt pin al lor.
void board_power_on(void);

// CS-ul cardului în 1, apoi magistrala cardului și, dacă panoul are alta, și pe a lui.
esp_err_t board_spi_init(void);

// Înaintea somnului adânc, după epd_sleep() și sdcard_unmount(): pune pe 0
// liniile cardului și panoului, le oprește șinele și ține pinii așa în somn.
// Pe DevKit nu face nimic.
void board_power_off(void);

// Somnul ușor automat (FW-7): liniile spre card și panou și comutatoarele
// șinelor își păstrează configurația și nivelul cât doarme cipul. Fără asta,
// ESP-IDF le deconectează: CS-urile și RST ar rămâne în aer, iar șinele s-ar opri.
void board_light_sleep_pins(void);
