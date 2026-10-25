// Bitmap font as used by the renderer (compiled-in fonts; SD fonts later load
// into the same structure). Glyph bitmaps: rows padded to whole bytes, MSB first,
// 1 = ink. Glyphs are sorted by codepoint.
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    uint32_t offset;   // into font_t.bitmaps
    uint8_t width;     // bitmap box, px
    uint8_t height;
    int8_t x_off;      // bitmap left edge relative to the pen position
    int8_t y_off;      // bitmap top edge relative to the baseline (negative = above)
    uint8_t advance;   // pen advance, px
} font_glyph_t;

typedef struct {
    const char *name;
    uint8_t ascent;      // baseline distance from the top of a line
    uint8_t descent;
    uint8_t line_height;
    uint16_t glyph_count;
    const font_glyph_t *glyphs;
    const uint8_t *bitmaps;
} font_t;

// Glyph for a codepoint, or the U+FFFD / '?' fallback, or NULL.
const font_glyph_t *font_glyph(const font_t *font, uint32_t cp);

// Decodes one UTF-8 sequence and advances *s. Returns 0 at the end of the
// string and U+FFFD for malformed input.
uint32_t utf8_next(const char **s);

extern const font_t font_ui16;
