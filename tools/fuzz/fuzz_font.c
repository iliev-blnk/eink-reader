// Fuzzing pentru fonturile IBF de pe card: font_open() trebuie să refuze orice
// font care ar face o căutare, un desen sau paginarea să citească în afara
// fișierului. Un font acceptat se folosește la tot: glife, kerning, lățimi,
// desen în framebuffer și paginarea unei cărți adevărate (FUZZ_BOOK).
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "ibv.h"
#include "layout.h"
#include "memfile.h"

static uint8_t *book;
static size_t book_len;
static layout_fit_t fit;
static uint8_t fb[GFX_BUF_SIZE(240, 416)];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc, (void)argv;
    const char *p = getenv("FUZZ_BOOK");
    if (p) book = fuzz_read_real(p, &book_len);
    return 0;
}

static const char *const samples[] = {
    "Hello, world! AVAWAY To. fi ffl",
    "Ce înseamnă „asta”? — Ţară, şi ăîâ.",
    "Дубровский, ёлка; «кавычки»",
    "\xF0\x9F\x98\x80 \xEF\xBF\xBD \xC3\x28 \xED\xA0\x80 \xF4\x90\x80\x80",
};

static void paginate(const font_t *f)
{
    if (!book) return;
    fuzz_set_file(book, book_len);
    ibv_t b;
    if (ibv_open(&b, "/fuzz/carte.ibv") != IBV_OK) return;
    layout_cfg_t cfg = {
        .body = {f, f, f, f}, .heading = f,
        .width = 216, .height = 380, .indent = f->pixel_size, .line_gap = 2,
        .justify = true, .hyphenate = true, .chapter_page = true, .keep_lines = true, .fit = &fit,
    };
    layout_fit_reset(&fit);
    static layout_page_t page;
    gfx_t g;
    gfx_init(&g, fb, 240, 416);
    layout_pos_t pos = {0, 0};
    for (int n = 0; n < 6; n++) {
        if (layout_page(&cfg, &b, pos, &page) != IBV_OK) break;
        layout_draw(&cfg, &b, &page, &g, 12, 12);
        if (page.at_end || layout_pos_cmp(page.end, pos) <= 0) break;
        pos = page.end;
    }
    layout_hash(&cfg);
    ibv_close(&b);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Pe dispozitiv fontul stă într-un buffer de exact mărimea fișierului, aliniat la 4.
    uint8_t *d = malloc(size ? size : 1);
    memcpy(d, data, size);
    font_t f;
    if (font_open(&f, d, size) == FONT_OK) {
        gfx_t g;
        gfx_init(&g, fb, 240, 416);
        for (size_t i = 0; i < sizeof samples / sizeof *samples; i++) {
            font_text_width(&f, samples[i]);
            gfx_text(&g, &f, 10, 40 * (int)i + 20, samples[i], GFX_BLACK);
            gfx_text(&g, &f, -5, -5, samples[i], GFX_BLACK);          // tăiat la margini
            gfx_text(&g, &f, 230, 410, samples[i], GFX_BLACK);
        }
        for (uint32_t i = 0; i < f.glyph_count && i < 2048; i++) {
            gfx_glyph(&g, &f, i, 100, 200, GFX_BLACK);
            font_kern(&f, i, (i * 7919u) % f.glyph_count);
        }
        for (uint32_t cp = 0; cp < 0x500; cp++) font_glyph(&f, cp);
        font_glyph(&f, 0x10FFFF);
        font_glyph(&f, UINT32_MAX);
        paginate(&f);
    }
    free(d);
    fuzz_set_file(NULL, 0);
    return 0;
}
