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

int gfx_text(gfx_t *g, const font_t *font, int x, int y, const char *utf8, gfx_color_t c)
{
    int start = x;
    uint32_t cp;
    while ((cp = utf8_next(&utf8)) != 0) {
        const font_glyph_t *gl = font_glyph(font, cp);
        if (!gl) continue;
        const uint8_t *bm = font->bitmaps + gl->offset;
        int gstride = (gl->width + 7) / 8;
        int gx = x + gl->x_off, gy = y + gl->y_off;
        for (int r = 0; r < gl->height; r++) {
            int py = gy + r;
            if (py < 0 || py >= g->height) continue;
            for (int col = 0; col < gl->width; col++) {
                int px = gx + col;
                if (px < 0 || px >= g->width) continue;
                if (bm[r * gstride + col / 8] & (0x80 >> (col & 7))) put(g, px, py, c);
            }
        }
        x += gl->advance;
    }
    int top = y - font->ascent;
    int w = x - start, h = font->line_height, cx = start, cy = top;
    if (clip(g, &cx, &cy, &w, &h)) mark_dirty(g, cx, cy, w, h);
    return x;
}

int gfx_text_width(const font_t *font, const char *utf8)
{
    int w = 0;
    uint32_t cp;
    while ((cp = utf8_next(&utf8)) != 0) {
        const font_glyph_t *gl = font_glyph(font, cp);
        if (gl) w += gl->advance;
    }
    return w;
}

bool gfx_take_dirty(gfx_t *g, gfx_rect_t *out)
{
    if (!g->has_dirty) return false;
    *out = g->dirty;
    g->has_dirty = false;
    return true;
}
