// Desenarea într-un framebuffer de 1 bit. C portabil, fără dependențe de ESP-IDF:
// același cod rulează pe dispozitiv și în simulatorul de pe PC.
//
// Formatul pixelilor e cel din RAM-ul panoului UC8253: pe rânduri, 8 px pe octet,
// MSB = cel mai din stânga, bitul 1 = alb, bitul 0 = negru.
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
    int width, height, stride;  // stride = octeți pe rând
    gfx_rect_t dirty;           // reuniunea a tot ce s-a desenat de la ultima preluare
    bool has_dirty;
} gfx_t;

#define GFX_BUF_SIZE(w, h) ((((w) + 7) / 8) * (h))

void gfx_init(gfx_t *g, uint8_t *buf, int width, int height);
void gfx_clear(gfx_t *g, gfx_color_t c);
void gfx_pixel(gfx_t *g, int x, int y, gfx_color_t c);
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, gfx_color_t c);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, gfx_color_t c);
void gfx_hline(gfx_t *g, int x, int y, int w, gfx_color_t c);

// Desenează glifa `index` din `font` cu penița la x și linia de bază la y.
void gfx_glyph(gfx_t *g, const font_t *font, uint32_t index, int x, int y, gfx_color_t c);

// Desenează un text UTF-8 cu linia de bază la y, cu kerning. Penița se mișcă în
// 1/64 px, iar fiecare glifă cade la poziția rotunjită a peniței. Întoarce x-ul
// de după ultima glifă.
int gfx_text(gfx_t *g, const font_t *font, int x, int y, const char *utf8, gfx_color_t c);
// Lățimea textului în px întregi (rotunjită în sus).
int gfx_text_width(const font_t *font, const char *utf8);

// Întoarce zona schimbată de la ultimul apel (pentru reîmprospătarea parțială) și o golește.
bool gfx_take_dirty(gfx_t *g, gfx_rect_t *out);
