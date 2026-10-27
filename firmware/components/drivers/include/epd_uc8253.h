// Driverul controlerului e-paper UC8253 de pe panoul GDEY037T03
// (WeAct 3,7", 240x416, alb-negru), SPI pe 4 fire.
//
// Scris pentru proiectul ăsta după setul de comenzi UC8253. Valorile comenzilor
// și timpii au fost verificați față de codul exemplu de la Good Display și față
// de GxEPD2 (GPL-3.0, Jean-Marc Zingg); nu e copiat cod din niciunul.
//
// Framebufferul e în formatul și orientarea native ale panoului: 240 px lățime,
// 416 rânduri, 8 px pe octet, MSB = cel mai din stânga, bitul 1 = alb (la fel ca
// gfx_t). Nu e sigur pentru mai multe fire: se apelează dintr-un singur task.
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
    spi_host_device_t host;  // magistrala trebuie să fie deja inițializată
    gpio_num_t cs, dc, rst, busy;
    int clock_hz;
    // Reîmprospătare completă cu forma de undă pentru o temperatură forțată:
    // ~1 s în loc de ~3 s, dar în afara domeniului specificat al panoului la
    // temperaturi joase.
    bool fast_full;
} epd_config_t;

// Ce a făcut ultima reîmprospătare și cât a durat fiecare fază (pentru
// jurnalele PERF și experimentul E1). Fereastra e zona reîmprospătată efectiv,
// după aliniere.
typedef struct {
    bool full;
    int x, y, w, h;
    uint32_t write_us;      // transferul SPI al imaginii (ambele RAM-uri)
    uint32_t power_on_us;   // timpul BUSY după POWER ON
    uint32_t refresh_us;    // timpul BUSY după DISPLAY REFRESH: actualizarea vizibilă
    uint32_t power_off_us;  // timpul BUSY după POWER OFF
    uint32_t total_us;      // tot apelul
} epd_timing_t;

// Adaugă dispozitivul SPI, configurează pinii, resetează panoul și verifică dacă
// răspunde (un ciclu de pornire și oprire). Se apelează înainte de a adăuga alte
// dispozitive pe aceeași magistrală, ca CS-ul panoului să fie în 1 de la început.
// Erori: ESP_ERR_INVALID_RESPONSE dacă BUSY nu reacționează deloc (cablajul sau
// modul SPI), ESP_ERR_TIMEOUT dacă BUSY rămâne în 0.
esp_err_t epd_init(const epd_config_t *cfg);

// Reîmprospătare completă: panoul clipește și fiecare pixel se redesenează, ștergând imaginile fantomă.
esp_err_t epd_full_refresh(const uint8_t *fb);

// Reîmprospătarea parțială a unei ferestre, fără clipire. x și w se lărgesc la
// octeți întregi. Trece la o reîmprospătare completă când controlerul încă nu
// ține ecranul curent (prima reîmprospătare după inițializare sau după epd_sleep).
esp_err_t epd_partial_refresh(const uint8_t *fb, int x, int y, int w, int h);

// Pune controlerul în somn adânc, starea lui cu cel mai mic curent, și ține RST
// în 1 ca să rămână acolo cât doarme și ESP32. Următoarea reîmprospătare îl
// trezește cu un reset hardware și e mereu completă, pentru că după somnul adânc
// nu ne putem baza pe RAM-ul de imagine al controlerului (de verificat pe panoul real).
esp_err_t epd_sleep(void);

const epd_timing_t *epd_last_timing(void);
