// Fuzzing pentru cititorul de cărți: o carte IBV oarecare trebuie deschisă sau
// refuzată fără nicio citire în afara memoriei, iar o carte deschisă trebuie să
// se poată citi, pagina și desena toată, ca în aplicația de citire.
#include <stdlib.h>
#include <string.h>

#include "fontsel.h"
#include "gfx.h"
#include "ibv.h"
#include "layout.h"
#include "memfile.h"

#define MAX_RECORDS 4096       // ca o intrare să nu țină secunde
#define MAX_PAGES 64

static layout_fit_t fit;
static uint8_t fb[GFX_BUF_SIZE(240, 416)];

static void read_records(ibv_t *b)
{
    uint32_t n = b->paragraph_count < MAX_RECORDS ? b->paragraph_count : MAX_RECORDS;
    for (uint32_t p = 0; p <= n; p++) {      // și unul după ultimul: IBV_ERR_RANGE
        ibv_record_t r;
        if (ibv_record(b, p, &r) != IBV_OK) continue;
        volatile uint32_t sum = 0;
        for (uint32_t i = 0; i < r.text_len; i++) sum += (uint8_t)r.text[i];
        ibv_styles_t s;
        ibv_styles_init(&s, &r);
        for (uint32_t i = 0; i <= r.text_len + 1; i++) sum += ibv_style_at(&s, i);
        uint32_t off;
        ibv_text_offset(b, p, 0, &off);
        ibv_text_offset(b, p, r.text_len, &off);
        ibv_text_offset(b, p, UINT32_MAX, &off);
        ibv_toc_find(b, p);
    }
    ibv_toc_find(b, UINT32_MAX);
    for (uint32_t i = 0; i <= b->toc_count; i++) {
        ibv_toc_t t;
        if (!ibv_toc(b, i, &t)) continue;
        volatile uint32_t sum = t.para + t.level;
        for (uint32_t j = 0; j < t.title_len; j++) sum += (uint8_t)t.title[j];
    }
}

static void paginate(ibv_t *b, bool fitted, bool justify)
{
    const char *lang = b->meta[IBV_LANG];
    fontsel_t fs = fontsel_pick(lang, 2);
    char path[64];
    fontsel_path(path, sizeof path, "/sd/fonts", fs, "r");
    layout_cfg_t cfg = {
        .body = {&font_ui16},
        .width = 216, .height = 380, .indent = 16, .line_gap = 2,
        .justify = justify, .hyphenate = true, .chapter_page = true, .keep_lines = true,
        .fit = fitted ? &fit : NULL,
    };
    layout_fit_reset(&fit);
    static layout_page_t page, scratch;
    gfx_t g;
    gfx_init(&g, fb, 240, 416);
    layout_pos_t pos = {0, 0};
    for (int n = 0; n < MAX_PAGES; n++) {
        if (layout_page(&cfg, b, pos, &page) != IBV_OK) return;
        gfx_clear(&g, GFX_WHITE);
        layout_draw(&cfg, b, &page, &g, 12, 12);
        if (n == 3) {
            layout_pos_t out;
            layout_prev(&cfg, b, page.start, &scratch, &out, NULL);
            layout_pos_t mid = {page.start.para, page.start.byte + 1};
            layout_locate(&cfg, b, mid, &scratch, &out);
        }
        if (page.at_end || layout_pos_cmp(page.end, pos) <= 0) return;
        pos = page.end;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_set_file(data, size);
    ibv_t b;
    if (ibv_open_meta(&b, "/fuzz/carte.ibv") == IBV_OK) {
        uint16_t w, h;
        uint8_t *bits;
        if (ibv_cover(&b, &w, &h, &bits)) {
            volatile uint8_t x = w && h ? bits[(size_t)((w + 7) / 8) * h - 1] : 0;   // toată coperta e alocată
            (void)x;
            free(bits);
        }
        ibv_close(&b);
    }
    if (ibv_open(&b, "/fuzz/carte.ibv") == IBV_OK) {
        read_records(&b);
        paginate(&b, true, true);
        paginate(&b, false, false);
        ibv_close(&b);
    }
    fuzz_set_file(NULL, 0);
    return 0;
}
