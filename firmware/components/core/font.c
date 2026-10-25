#include "font.h"

#include <stddef.h>

const font_glyph_t *font_glyph(const font_t *font, uint32_t cp)
{
    int lo = 0, hi = (int)font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = font->glyphs[mid].codepoint;
        if (c == cp) return &font->glyphs[mid];
        if (c < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    if (cp != 0xFFFD && cp != '?') {
        const font_glyph_t *g = font_glyph(font, 0xFFFD);
        return g ? g : font_glyph(font, '?');
    }
    return NULL;
}

uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (*p == 0) return 0;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) { cp = p[0]; extra = 0; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
    else { *s += 1; return 0xFFFD; }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    // Reject overlong forms and surrogates.
    static const uint32_t min_cp[] = {0, 0x80, 0x800, 0x10000};
    if (cp < min_cp[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}
