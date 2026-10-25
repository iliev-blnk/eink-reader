#include "screen_bringup.h"

#include <stdio.h>

#define MARGIN 8
#define COUNTER_Y 282
#define COUNTER_H 56

static int line(gfx_t *g, int y, const char *label, const char *value)
{
    int x = gfx_text(g, &font_ui16, MARGIN, y, label, GFX_BLACK);
    gfx_text(g, &font_ui16, x + 4, y, value, GFX_BLACK);
    return y + font_ui16.line_height;
}

void screen_bringup_draw(gfx_t *g, const bringup_info_t *info)
{
    const font_t *f = &font_ui16;
    gfx_clear(g, GFX_WHITE);

    gfx_fill_rect(g, 0, 0, g->width, f->line_height + 8, GFX_BLACK);
    gfx_text(g, f, MARGIN, 4 + f->ascent, "E-ink reader: bring-up", GFX_WHITE);

    int y = f->line_height + 8 + 6 + f->ascent;
    y = line(g, y, "Board:", info->board);
    y = line(g, y, "Chip:", info->chip);
    y = line(g, y, "Mem:", info->memory);
    y = line(g, y, "SD:", info->sd);
    y = line(g, y, "Battery:", info->battery);
    y = line(g, y, "Wake:", info->wake);

    y += 4;
    gfx_hline(g, MARGIN, y - f->ascent, g->width - 2 * MARGIN, GFX_BLACK);
    y += 8;
    gfx_text(g, f, MARGIN, y, "ăâîșț ĂÂÎȘȚ „citate”", GFX_BLACK);
    y += f->line_height;
    gfx_text(g, f, MARGIN, y, "Привет, мир! Ёё «»", GFX_BLACK);

    screen_bringup_draw_counter(g, info);

    int fy = g->height - MARGIN - 2 * f->line_height + f->ascent;
    gfx_hline(g, MARGIN, fy - f->ascent - 4, g->width - 2 * MARGIN, GFX_BLACK);
    gfx_text(g, f, MARGIN, fy, "PREV/NEXT: partial refresh", GFX_BLACK);
    gfx_text(g, f, MARGIN, fy + f->line_height, "MENU: full · hold: sleep", GFX_BLACK);
}

void screen_bringup_draw_counter(gfx_t *g, const bringup_info_t *info)
{
    const font_t *f = &font_ui16;
    char buf[64];
    gfx_fill_rect(g, MARGIN, COUNTER_Y, g->width - 2 * MARGIN, COUNTER_H, GFX_WHITE);
    gfx_rect(g, MARGIN, COUNTER_Y, g->width - 2 * MARGIN, COUNTER_H, GFX_BLACK);
    snprintf(buf, sizeof buf, "%s  ·  count %d", info->last_button, info->counter);
    gfx_text(g, f, MARGIN + 6, COUNTER_Y + 4 + f->ascent, buf, GFX_BLACK);
    gfx_text(g, f, MARGIN + 6, COUNTER_Y + 4 + f->ascent + f->line_height, info->last_timing, GFX_BLACK);
}

void screen_sleep_draw(gfx_t *g)
{
    const font_t *f = &font_ui16;
    gfx_clear(g, GFX_WHITE);
    const char *a = "Sleeping";
    const char *b = "Press any button to wake";
    int y = g->height / 2;
    gfx_text(g, f, (g->width - gfx_text_width(f, a)) / 2, y, a, GFX_BLACK);
    gfx_text(g, f, (g->width - gfx_text_width(f, b)) / 2, y + f->line_height, b, GFX_BLACK);
}
