// Three buttons to GND with pull-ups: debounced, short and long press, queued
// as events. v0 polls every 10 ms; interrupts + light sleep come with the
// power manager.
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
} button_event_t;

// Starts the polling task. A button that is already down (the one that woke
// the device) is ignored until it's released.
esp_err_t buttons_init(const gpio_num_t pins[BUTTON_COUNT]);

// Waits for the next event; false on timeout.
bool buttons_wait(button_event_t *ev, TickType_t timeout);

bool buttons_any_down(void);

// Stops polling and arms all buttons as deep-sleep wake sources (ext1, any
// low). The pins keep their pull-ups through sleep.
esp_err_t buttons_enable_wakeup(void);

// The button behind an ext1 wake status mask, or -1.
int buttons_from_wake_mask(uint64_t mask);

const char *button_name(button_t b);
