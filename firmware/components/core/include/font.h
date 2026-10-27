// Fonturi bitmap în formatul IBF (spec/font-format.md). Un font e fie un
// fișier încărcat în memorie și deschis cu font_open(), care face tabelele să
// arate direct în fișier, fie tabele C compilate în firmware (fonts/*.c,
// generate de tools/fonttool/fonttool.py c).
//
// Bitmapurile glifelor: rânduri completate la octeți întregi, MSB = pixelul cel
// mai din stânga, 1 = cerneală. Avansurile și kerningul sunt în 1/64 px (FONT_UNIT).
#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "IBF tables are little-endian and used in place"
#endif

#define FONT_UNIT 64            // 1/64 px pe px
#define FONT_STYLE_BOLD 1
#define FONT_STYLE_ITALIC 2

// Cele trei se potrivesc octet cu octet cu înregistrările din fișier (specificația, 3.2–3.4).
typedef struct {
    uint32_t first_cp;
    uint16_t count;
    uint16_t first_glyph;
} font_range_t;

typedef struct {
    uint32_t bitmap;   // deplasamentul în font_t.bitmaps
    uint16_t advance;  // avansul peniței, 1/64 px
    uint8_t width;     // cutia bitmapului, px
    uint8_t height;
    int8_t x_off;      // marginea stângă a cutiei față de peniță
    int8_t y_off;      // marginea de sus a cutiei față de linia de bază (negativ = deasupra)
    uint16_t kern_as;  // glifa ale cărei perechi de kerning le folosește aceasta (propriul indice dacă nu e)
} font_glyph_t;

typedef struct {
    uint16_t left, right;  // indicii glifelor
    int16_t value;         // 1/64 px, adăugat la avansul glifei din stânga
} font_kern_t;

_Static_assert(sizeof(font_range_t) == 8, "font_range_t must match the file");
_Static_assert(sizeof(font_glyph_t) == 12, "font_glyph_t must match the file");
_Static_assert(sizeof(font_kern_t) == 6, "font_kern_t must match the file");

typedef struct {
    char name[33];
    uint8_t style;       // FONT_STYLE_*
    uint16_t pixel_size;
    uint16_t line_height;  // de la o linie de bază la alta, px
    uint16_t ascent;       // px deasupra liniei de bază
    uint16_t descent;
    uint16_t x_height;
    uint16_t range_count;
    uint32_t glyph_count;
    uint32_t kern_count;
    const font_range_t *ranges;
    const font_glyph_t *glyphs;
    const font_kern_t *kerns;
    const uint8_t *bitmaps;
    uint32_t fallback;   // indicele glifei U+FFFD
} font_t;

typedef enum {
    FONT_OK = 0,
    FONT_ERR_SHORT,      // mai scurt decât antetul, sau un tabel în afara fișierului
    FONT_ERR_MAGIC,
    FONT_ERR_VERSION,    // versiune majoră, mărime a antetului sau bpp necunoscute
    FONT_ERR_CRC,
    FONT_ERR_ALIGN,      // datele sau un tabel nu sunt aliniate la 4 octeți
    FONT_ERR_TABLES,     // intervale, glife sau perechi de kerning care nu se potrivesc
    FONT_ERR_REQUIRED,   // lipsește U+0020, U+002D sau U+FFFD
} font_err_t;

// Verifică un fișier de font (regulile validatorului 1–6) și face *font să arate
// în el. `data` trebuie aliniat la 4 octeți și să rămână valabil cât e folosit fontul.
font_err_t font_open(font_t *font, const uint8_t *data, size_t len);
const char *font_strerror(font_err_t err);

// Indicele glifei pentru un cod Unicode, sau -1.
int32_t font_index(const font_t *font, uint32_t cp);
// Indicele glifei pentru un cod Unicode, sau al glifei U+FFFD.
uint32_t font_index_or_fallback(const font_t *font, uint32_t cp);
// Glifa pentru un cod Unicode, sau glifa U+FFFD.
const font_glyph_t *font_glyph(const font_t *font, uint32_t cp);
// Kerningul dintre două glife vecine (indici), 1/64 px. Specificația, 3.4.
int32_t font_kern(const font_t *font, uint32_t left, uint32_t right);
// Lățimea unui text UTF-8 în 1/64 px, cu kerning.
int32_t font_text_width(const font_t *font, const char *utf8);

// Decodează o secvență UTF-8 și avansează *s. Întoarce 0 la sfârșitul șirului
// și U+FFFD pentru o secvență greșită.
uint32_t utf8_next(const char **s);
// La fel pentru un text cu lungime: se oprește la `end` (și întoarce 0 acolo).
uint32_t utf8_next_n(const char **s, const char *end);

extern const font_t font_ui16;
extern const font_t font_ui12;
