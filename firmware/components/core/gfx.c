#include "gfx.h"

#include <string.h>

static void mark_dirty(gfx_t *g, int x, int y, int w, int h)
{
    if (!g->has_dirty) {
        g->dirty = (gfx_rect_t){x, y, w, h};
        g->has_dirty = true;
        return;
    }
    int x1 = g->dirty.x + g->dirty.w, y1 = g->dirty.y + g->dirty.h;
    if (x < g->dirty.x) g->dirty.x = x;
    if (y < g->dirty.y) g->dirty.y = y;
    if (x + w > x1) x1 = x + w;
    if (y + h > y1) y1 = y + h;
    g->dirty.w = x1 - g->dirty.x;
    g->dirty.h = y1 - g->dirty.y;
}

// Clips a rectangle to the screen; returns false if nothing is left.
static bool clip(const gfx_t *g, int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > g->width) *w = g->width - *x;
    if (*y + *h > g->height) *h = g->height - *y;
    return *w > 0 && *h > 0;
}

void gfx_init(gfx_t *g, uint8_t *buf, int width, int height)
{
    g->buf = buf;
    g->width = width;
    g->height = height;
    g->stride = (width + 7) / 8;
    g->has_dirty = false;
}

void gfx_clear(gfx_t *g, gfx_color_t c)
{
    memset(g->buf, c == GFX_WHITE ? 0xFF : 0x00, (size_t)g->stride * g->height);
    mark_dirty(g, 0, 0, g->width, g->height);
}

static inline void put(gfx_t *g, int x, int y, gfx_color_t c)
{
    uint8_t *b = &g->buf[y * g->stride + x / 8];
    uint8_t m = 0x80 >> (x & 7);
    if (c == GFX_WHITE) *b |= m;
    else *b &= ~m;
}

void gfx_pixel(gfx_t *g, int x, int y, gfx_color_t c)
{
    if (x < 0 || y < 0 || x >= g->width || y >= g->height) return;
    put(g, x, y, c);
    mark_dirty(g, x, y, 1, 1);
}

void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, gfx_color_t c)
{
    if (!clip(g, &x, &y, &w, &h)) return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) put(g, xx, yy, c);
    mark_dirty(g, x, y, w, h);
}

void gfx_hline(gfx_t *g, int x, int y, int w, gfx_color_t c)
{
    gfx_fill_rect(g, x, y, w, 1, c);
}

void gfx_rect(gfx_t *g, int x, int y, int w, int h, gfx_color_t c)
{
    gfx_fill_rect(g, x, y, w, 1, c);
    gfx_fill_rect(g, x, y + h - 1, w, 1, c);
    gfx_fill_rect(g, x, y, 1, h, c);
    gfx_fill_rect(g, x + w - 1, y, 1, h, c);
}

void gfx_glyph(gfx_t *g, const font_t *font, uint32_t index, int x, int y, gfx_color_t c)
{
    const font_glyph_t *gl = &font->glyphs[index];
    if (gl->width == 0 || gl->height == 0) return;
    const uint8_t *bm = font->bitmaps + gl->bitmap;
    int gstride = (gl->width + 7) / 8;
    int gx = x + gl->x_off, gy = y + gl->y_off;
    int w = gl->width, h = gl->height;
    if (gx >= 0 && gy >= 0 && gx + w <= g->width && gy + h <= g->height) {
        // Whole glyph on screen: shift each source byte into two framebuffer
        // bytes. Ink clears bits for black and sets them for white.
        int shift = gx & 7;
        for (int r = 0; r < h; r++) {
            uint8_t *dst = &g->buf[(gy + r) * g->stride + gx / 8];
            const uint8_t *src = bm + r * gstride;
            for (int b = 0; b < gstride; b++) {
                uint8_t s = src[b];
                if (!s) continue;
                uint8_t hi = (uint8_t)(s >> shift), lo = (uint8_t)(s << (8 - shift));
                if (c == GFX_BLACK) {
                    dst[b] &= (uint8_t)~hi;
                    if (shift && lo) dst[b + 1] &= (uint8_t)~lo;
                } else {
                    dst[b] |= hi;
                    if (shift && lo) dst[b + 1] |= lo;
                }
            }
        }
        mark_dirty(g, gx, gy, w, h);
        return;
    }
    for (int r = 0; r < h; r++) {
        int py = gy + r;
        if (py < 0 || py >= g->height) continue;
        for (int col = 0; col < w; col++) {
            int px = gx + col;
            if (px < 0 || px >= g->width) continue;
            if (bm[r * gstride + col / 8] & (0x80 >> (col & 7))) put(g, px, py, c);
        }
    }
    int cx = gx, cy = gy;
    if (clip(g, &cx, &cy, &w, &h)) mark_dirty(g, cx, cy, w, h);
}

int gfx_text(gfx_t *g, const font_t *font, int x, int y, const char *utf8, gfx_color_t c)
{
    int32_t pen = x * FONT_UNIT;
    int32_t prev = -1;
    uint32_t cp;
    while ((cp = utf8_next(&utf8)) != 0) {
        uint32_t i = font_index_or_fallback(font, cp);
        if (prev >= 0) pen += font_kern(font, (uint32_t)prev, i);
        gfx_glyph(g, font, i, (pen + FONT_UNIT / 2) / FONT_UNIT, y, c);
        pen += font->glyphs[i].advance;
        prev = (int32_t)i;
    }
    return (pen + FONT_UNIT - 1) / FONT_UNIT;
}

int gfx_text_width(const font_t *font, const char *utf8)
{
    return (font_text_width(font, utf8) + FONT_UNIT - 1) / FONT_UNIT;
}

bool gfx_take_dirty(gfx_t *g, gfx_rect_t *out)
{
    if (!g->has_dirty) return false;
    *out = g->dirty;
    g->has_dirty = false;
    return true;
}
