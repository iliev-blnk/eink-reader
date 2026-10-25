// Bring-up screens. Portable (core only), so sim/ renders the same layout.
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
// Redraws only the button/counter box; the caller refreshes the dirty area.
void screen_bringup_draw_counter(gfx_t *g, const bringup_info_t *info);
void screen_sleep_draw(gfx_t *g);
