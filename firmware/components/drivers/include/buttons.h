// Trei butoane la GND cu pull-up: cu debounce, apăsare scurtă și lungă, puse
// într-o coadă ca evenimente. v0 citește la fiecare 10 ms; întreruperile și
// somnul ușor vin odată cu managerul de alimentare.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum { BUTTON_PREV, BUTTON_NEXT, BUTTON_MENU, BUTTON_COUNT } button_t;

typedef enum { PRESS_SHORT, PRESS_LONG } press_t;

typedef struct {
    button_t button;
    press_t press;
    // Frontul care a încheiat apăsarea (esp_timer, µs): eliberarea pentru una
    // scurtă, apăsarea pentru una lungă. Cu precizia citirii, 10 ms; pentru E2
    // timpul exact îl dă analizorul logic pe pin.
    int64_t edge_us;
} button_event_t;

// Pornește taskul care citește butoanele. Un buton deja apăsat (cel care a
// trezit dispozitivul) e ignorat până e eliberat.
esp_err_t buttons_init(const gpio_num_t pins[BUTTON_COUNT]);

// Așteaptă următorul eveniment; false la expirarea timpului.
bool buttons_wait(button_event_t *ev, TickType_t timeout);

bool buttons_any_down(void);

// Oprește citirea și face din toate butoanele surse de trezire din somnul
// adânc (ext1, oricare în 0). Pinii își păstrează pull-up-urile în somn.
esp_err_t buttons_enable_wakeup(void);

// Butonul din spatele unei măști de stare a trezirii ext1, sau -1.
int buttons_from_wake_mask(uint64_t mask);

const char *button_name(button_t b);
