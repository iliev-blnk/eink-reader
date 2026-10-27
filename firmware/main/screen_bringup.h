// Ecranele de punere în funcțiune. Portabile (doar core), deci sim/ desenează aceeași așezare.
#pragma once

#include "gfx.h"

typedef struct {
    const char *board;
    const char *chip;
    const char *memory;
    const char *sd;
    const char *battery;
    const char *wake;
    const char *last_button;
    int counter;
    const char *last_timing;
} bringup_info_t;

void screen_bringup_draw(gfx_t *g, const bringup_info_t *info);
// Redesenează doar caseta butoanelor și a contorului; apelantul reîmprospătează zona modificată.
void screen_bringup_draw_counter(gfx_t *g, const bringup_info_t *info);
void screen_sleep_draw(gfx_t *g);
