// 1-bit framebuffer drawing. Portable C, no ESP-IDF dependencies: the same code
// runs on the device and in the PC simulator.
//
// Pixel format matches the UC8253 panel RAM: row-major, 8 px per byte, MSB = leftmost,
// bit 1 = white, bit 0 = black.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

typedef enum { GFX_BLACK = 0, GFX_WHITE = 1 } gfx_color_t;

typedef struct {
    int x, y, w, h;
} gfx_rect_t;

typedef struct {
    uint8_t *buf;
    int width, height, stride;  // stride = bytes per row
    gfx_rect_t dirty;           // union of everything drawn since the last take
    bool has_dirty;
} gfx_t;

#define GFX_BUF_SIZE(w, h) ((((w) + 7) / 8) * (h))

void gfx_init(gfx_t *g, uint8_t *buf, int width, int height);
void gfx_clear(gfx_t *g, gfx_color_t c);
void gfx_pixel(gfx_t *g, int x, int y, gfx_color_t c);
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, gfx_color_t c);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, gfx_color_t c);
void gfx_hline(gfx_t *g, int x, int y, int w, gfx_color_t c);

// Draws UTF-8 text with its baseline at y. Returns the x after the last glyph.
int gfx_text(gfx_t *g, const font_t *font, int x, int y, const char *utf8, gfx_color_t c);
int gfx_text_width(const font_t *font, const char *utf8);

// Returns the area changed since the last call (for partial refresh) and resets it.
bool gfx_take_dirty(gfx_t *g, gfx_rect_t *out);
