#include "font.h"

#include <string.h>

#include "crc32.h"

#define HEADER_SIZE 96

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const char *font_strerror(font_err_t err)
{
    switch (err) {
    case FONT_OK: return "ok";
    case FONT_ERR_SHORT: return "fișier prea scurt sau un tabel în afara lui";
    case FONT_ERR_MAGIC: return "nu e un font IBF";
    case FONT_ERR_VERSION: return "versiune, mărime de antet sau bpp nesuportate";
    case FONT_ERR_CRC: return "CRC greșit";
    case FONT_ERR_ALIGN: return "date sau tabel nealiniate";
    case FONT_ERR_TABLES: return "tabele care nu se potrivesc";
    case FONT_ERR_REQUIRED: return "lipsește U+0020, U+002D sau U+FFFD";
    }
    return "?";
}

font_err_t font_open(font_t *font, const uint8_t *d, size_t len)
{
    static const uint8_t magic[8] = {0x89, 'I', 'B', 'F', '\r', '\n', 0x1A, '\n'};
    if (len < HEADER_SIZE) return FONT_ERR_SHORT;
    if (memcmp(d, magic, 8) != 0) return FONT_ERR_MAGIC;
    if (crc32_update(0, d, 92) != rd32(d + 92)) return FONT_ERR_CRC;
    if (d[8] != 0 || rd16(d + 10) != HEADER_SIZE || d[12] != 1) return FONT_ERR_VERSION;
    if (crc32_update(0, d + HEADER_SIZE, len - HEADER_SIZE) != rd32(d + 88)) return FONT_ERR_CRC;
    if ((uintptr_t)d & 3) return FONT_ERR_ALIGN;

    uint32_t nranges = rd16(d + 26), nglyphs = rd32(d + 28), nkerns = rd32(d + 32);
    uint32_t off[4] = {rd32(d + 36), rd32(d + 40), rd32(d + 44), rd32(d + 48)};
    uint64_t size[4] = {8ull * nranges, 12ull * nglyphs, 6ull * nkerns, rd32(d + 52)};
    if (nglyphs > 0xFFFF) return FONT_ERR_TABLES;
    uint64_t end = HEADER_SIZE;
    for (int i = 0; i < 4; i++) {
        if (i < 3 && off[i] % 4) return FONT_ERR_ALIGN;
        if (off[i] < end || off[i] + size[i] > len) return FONT_ERR_SHORT;
        end = off[i] + size[i];
    }
    if (end != len) return FONT_ERR_SHORT;

    memset(font, 0, sizeof *font);
    memcpy(font->name, d + 56, 32);
    font->style = d[13];
    font->pixel_size = rd16(d + 16);
    font->line_height = rd16(d + 18);
    font->ascent = rd16(d + 20);
    font->descent = rd16(d + 22);
    font->x_height = rd16(d + 24);
    font->range_count = (uint16_t)nranges;
    font->glyph_count = nglyphs;
    font->kern_count = nkerns;
    font->ranges = (const font_range_t *)(d + off[0]);
    font->glyphs = (const font_glyph_t *)(d + off[1]);
    font->kerns = (const font_kern_t *)(d + off[2]);
    font->bitmaps = d + off[3];

    // Regulile 3–5, ca nicio căutare de mai târziu să nu poată citi în afara fișierului.
    uint32_t next = 0, prev_last = 0;
    for (uint32_t i = 0; i < nranges; i++) {
        const font_range_t *r = &font->ranges[i];
        if (r->count == 0 || r->first_glyph != next || (i && r->first_cp <= prev_last)) return FONT_ERR_TABLES;
        prev_last = r->first_cp + r->count - 1;
        next += r->count;
    }
    if (next != nglyphs) return FONT_ERR_TABLES;
    for (uint32_t i = 0; i < nglyphs; i++) {
        const font_glyph_t *g = &font->glyphs[i];
        if ((uint64_t)g->bitmap + (uint64_t)((g->width + 7) / 8) * g->height > size[3]) return FONT_ERR_TABLES;
        if (g->kern_as >= nglyphs || font->glyphs[g->kern_as].kern_as != g->kern_as) return FONT_ERR_TABLES;
    }
    for (uint32_t i = 0; i < nkerns; i++) {
        const font_kern_t *k = &font->kerns[i];
        if (k->left >= nglyphs || k->right >= nglyphs) return FONT_ERR_TABLES;
        if (i && (k->left < k[-1].left || (k->left == k[-1].left && k->right <= k[-1].right)))
            return FONT_ERR_TABLES;
    }
    int32_t fb = font_index(font, 0xFFFD);
    if (fb < 0 || font_index(font, 0x20) < 0 || font_index(font, 0x2D) < 0) return FONT_ERR_REQUIRED;
    font->fallback = (uint32_t)fb;
    return FONT_OK;
}

int32_t font_index(const font_t *font, uint32_t cp)
{
    int lo = 0, hi = (int)font->range_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const font_range_t *r = &font->ranges[mid];
        if (cp < r->first_cp) hi = mid - 1;
        else if (cp >= r->first_cp + r->count) lo = mid + 1;
        else return (int32_t)(r->first_glyph + (cp - r->first_cp));
    }
    return -1;
}

uint32_t font_index_or_fallback(const font_t *font, uint32_t cp)
{
    int32_t i = font_index(font, cp);
    return i < 0 ? font->fallback : (uint32_t)i;
}

const font_glyph_t *font_glyph(const font_t *font, uint32_t cp)
{
    return &font->glyphs[font_index_or_fallback(font, cp)];
}

static const font_kern_t *find_pair(const font_t *font, uint32_t a, uint32_t b)
{
    uint32_t key = a << 16 | b;
    int lo = 0, hi = (int)font->kern_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const font_kern_t *k = &font->kerns[mid];
        uint32_t kk = (uint32_t)k->left << 16 | k->right;
        if (kk == key) return k;
        if (kk < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int32_t font_kern(const font_t *font, uint32_t left, uint32_t right)
{
    if (font->kern_count == 0) return 0;
    const font_kern_t *k = find_pair(font, left, right);
    if (k) return k->value;
    uint32_t a = font->glyphs[left].kern_as, b = font->glyphs[right].kern_as;
    if (a == left && b == right) return 0;
    k = find_pair(font, a, b);
    return k ? k->value : 0;
}

int32_t font_text_width(const font_t *font, const char *utf8)
{
    int32_t w = 0;
    int32_t prev = -1;
    uint32_t cp;
    while ((cp = utf8_next(&utf8)) != 0) {
        uint32_t i = font_index_or_fallback(font, cp);
        if (prev >= 0) w += font_kern(font, (uint32_t)prev, i);
        w += font->glyphs[i].advance;
        prev = (int32_t)i;
    }
    return w;
}

uint32_t utf8_next_n(const char **s, const char *end)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (*s >= end) return 0;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) { cp = p[0]; extra = 0; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
    else { *s += 1; return 0xFFFD; }
    for (int i = 1; i <= extra; i++) {
        if ((const char *)p + i >= end || (p[i] & 0xC0) != 0x80) { *s += i; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    // Se resping formele prea lungi și surogatele.
    static const uint32_t min_cp[] = {0, 0x80, 0x800, 0x10000};
    if (cp < min_cp[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}

uint32_t utf8_next(const char **s)
{
    if (**s == 0) return 0;
    return utf8_next_n(s, *s + 1 + strnlen(*s + 1, 3));   // niciodată după NUL
}
