// PC simulator: runs the portable core (fonts, IBV reader, layout) and writes
// what the 240x416 panel would show as PNG files.
//
//   sim screens [-o DIR]                    boot and sleep screens
//   sim pages BOOK.ibv [-o DIR] [--from N] [-n COUNT] [--cover] [layout options]
//   sim stats BOOK.ibv... [layout options]  paginate whole books: timings, checks,
//                                           and a hash of every page (golden tests)
//   sim compare BOOK.ibv -o OUT.png [--page N] FONT... [layout options]
//                                           one page side by side in several fonts
//
// Layout options:
//   -f FONT          body font: FONT-r.ibf, plus FONT-b/-i/-bi.ibf if they exist
//                    (e.g. tools/fonttool/out/fam/literata-18); default: the
//                    built-in font_ui16
//   --margin PX      left/right/top margin (default 12)
//   --indent PX      first-line indent (default: the font's pixel size)
//   --line-gap PX    --para-gap PX
//   --ragged         no justification
//   --no-hyphen      ignore soft hyphens
//   --no-chapter-page   level-1 headings don't start a new page
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crc32.h"
#include "gfx.h"
#include "ibv.h"
#include "layout.h"
#include "png.h"
#include "screen_bringup.h"

#define W 240
#define H 416
#define FOOTER 22               // px below the text area for the footer line
#define MAX_FONTS 8

typedef struct {
    layout_cfg_t cfg;
    int margin;
    const char *font_name;      // for messages
} setup_t;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static const char *basename_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// Loads an .ibf into a buffer that lives until exit. NULL if the file doesn't
// exist; exits if it exists but doesn't open.
static const font_t *load_font(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);        // malloc is at least 8-aligned
    font_t *font = malloc(sizeof *font);
    if (!d || !font || fread(d, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s: read error\n", path);
        exit(1);
    }
    fclose(f);
    font_err_t err = font_open(font, d, (size_t)n);
    if (err) {
        fprintf(stderr, "%s: %s\n", path, font_strerror(err));
        exit(1);
    }
    return font;
}

static void load_family(setup_t *s, const char *prefix)
{
    static const char *suffix[4] = {"r", "b", "i", "bi"};
    char path[1024];
    memset(s->cfg.body, 0, sizeof s->cfg.body);
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s-%s.ibf", prefix, suffix[i]);
        s->cfg.body[i] = load_font(path);
    }
    if (!s->cfg.body[0]) {
        fprintf(stderr, "%s-r.ibf: not found\n", prefix);
        exit(1);
    }
    s->font_name = basename_of(prefix);
}

static void finish_setup(setup_t *s, int indent)
{
    if (!s->cfg.body[0]) s->cfg.body[0] = &font_ui16, s->font_name = "font_ui16";
    s->cfg.width = W - 2 * s->margin;
    s->cfg.height = H - s->margin - FOOTER;
    s->cfg.indent = indent >= 0 ? indent : s->cfg.body[0]->pixel_size;
}

// Parses the layout options at argv[*i]; returns false if it isn't one.
static bool layout_option(setup_t *s, int argc, char **argv, int *i, int *indent)
{
    const char *a = argv[*i];
    const char *v = *i + 1 < argc ? argv[*i + 1] : NULL;
    if (!strcmp(a, "--ragged")) s->cfg.justify = false;
    else if (!strcmp(a, "--no-hyphen")) s->cfg.hyphenate = false;
    else if (!strcmp(a, "--no-chapter-page")) s->cfg.chapter_page = false;
    else if (v && !strcmp(a, "-f")) load_family(s, v), ++*i;
    else if (v && !strcmp(a, "--margin")) s->margin = atoi(v), ++*i;
    else if (v && !strcmp(a, "--indent")) *indent = atoi(v), ++*i;
    else if (v && !strcmp(a, "--line-gap")) s->cfg.line_gap = atoi(v), ++*i;
    else if (v && !strcmp(a, "--para-gap")) s->cfg.para_gap = atoi(v), ++*i;
    else return false;
    return true;
}

static void default_setup(setup_t *s)
{
    memset(s, 0, sizeof *s);
    s->margin = 12;
    s->cfg.justify = true;
    s->cfg.hyphenate = true;
    s->cfg.chapter_page = true;
}

static ibv_t *open_book(const char *path)
{
    static ibv_t book;
    ibv_err_t err = ibv_open(&book, path);
    if (err) {
        fprintf(stderr, "%s: %s\n", path, ibv_strerror(err));
        return NULL;
    }
    return &book;
}

// Draws text cut to max_w px with an ellipsis.
static void text_fit(gfx_t *g, const font_t *f, int x, int y, const char *s, size_t len, int max_w)
{
    char buf[256];
    if (len > sizeof buf - 4) len = sizeof buf - 4;
    memcpy(buf, s, len);
    buf[len] = 0;
    if (gfx_text_width(f, buf) <= max_w) {
        gfx_text(g, f, x, y, buf, GFX_BLACK);
        return;
    }
    while (len > 0) {
        do len--; while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);   // back one codepoint
        while (len > 0 && buf[len - 1] == ' ') len--;
        memcpy(buf + len, "\xE2\x80\xA6", 4);
        if (gfx_text_width(f, buf) <= max_w) break;
    }
    gfx_text(g, f, x, y, buf, GFX_BLACK);
}

static void draw_footer(gfx_t *g, const setup_t *s, ibv_t *book, const layout_page_t *page)
{
    const font_t *f = &font_ui12;
    int y = H - 7;
    uint32_t off = 0;
    ibv_text_offset(book, page->start.para, page->start.byte, &off);
    char pct[16];
    snprintf(pct, sizeof pct, "%u%%", page->at_end ? 100u : (unsigned)((uint64_t)off * 100 / (book->text_bytes ? book->text_bytes : 1)));
    int pw = gfx_text_width(f, pct);
    gfx_text(g, f, W - s->margin - pw, y, pct, GFX_BLACK);
    ibv_toc_t t;
    int32_t ti = ibv_toc_find(book, page->start.para);
    if (ti >= 0 && ibv_toc(book, (uint32_t)ti, &t))
        text_fit(g, f, s->margin, y, t.title, t.title_len, W - 2 * s->margin - pw - 12);
}

static void draw_cover(gfx_t *g, ibv_t *book)
{
    uint16_t w, h;
    uint8_t *bits;
    gfx_clear(g, GFX_WHITE);
    if (!ibv_cover(book, &w, &h, &bits)) return;
    int x0 = (W - w) / 2, y0 = (H - h) / 2, stride = (w + 7) / 8;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (!(bits[y * stride + x / 8] & (0x80 >> (x % 8)))) gfx_pixel(g, x0 + x, y0 + y, GFX_BLACK);
    free(bits);
}

// Lays out and draws the page at `pos`. Returns false on a read error.
static bool render_page(gfx_t *g, const setup_t *s, ibv_t *book, layout_pos_t pos, layout_page_t *page)
{
    ibv_err_t err = layout_page(&s->cfg, book, pos, page);
    if (!err) {
        gfx_clear(g, GFX_WHITE);
        err = layout_draw(&s->cfg, book, page, g, s->margin, s->margin);
    }
    if (err) {
        fprintf(stderr, "page at %u:%u: %s\n", pos.para, pos.byte, ibv_strerror(err));
        return false;
    }
    draw_footer(g, s, book, page);
    return true;
}

static int cmd_screens(int argc, char **argv)
{
    const char *outdir = argc > 2 && !strcmp(argv[1], "-o") ? argv[2] : ".";
    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    gfx_t g;
    gfx_init(&g, buf, W, H);
    char path[512];

    // What a normal boot shows, then the longest strings the firmware can
    // produce, to check that nothing runs off the 240 px width.
    const bringup_info_t screens[] = {
        {
            .board = "devkit-proto",
            .chip = "ESP32-S3 v0.2, 2 cores",
            .memory = "16M flash, 8M PSRAM",
            .sd = "OK, 15.9 GB, 3 files",
            .battery = "3.92 V",
            .wake = "power-on · boot 1",
            .last_button = "NEXT",
            .counter = 3,
            .last_timing = "prev: partial 412 ms",
        },
        {
            .board = "devkit-proto",
            .chip = "ESP32-S3 v0.2, 2 cores",
            .memory = "16M flash, no PSRAM",
            .sd = "not FAT32 formatted",
            .battery = "none (0.13 V)",
            .wake = "other reset · boot 1234",
            .last_button = "PREV long",
            .counter = -1234,
            .last_timing = "prev: partial 1234 ms",
        },
    };
    const char *names[] = {"bringup", "bringup-long"};
    for (int i = 0; i < 2; i++) {
        screen_bringup_draw(&g, &screens[i]);
        snprintf(path, sizeof path, "%s/%s.png", outdir, names[i]);
        if (png_write(&g, path)) return 1;
    }
    screen_sleep_draw(&g);
    snprintf(path, sizeof path, "%s/sleep.png", outdir);
    if (png_write(&g, path)) return 1;
    printf("wrote %s/{bringup,bringup-long,sleep}.png\n", outdir);
    return 0;
}

static int cmd_pages(int argc, char **argv)
{
    setup_t s;
    default_setup(&s);
    const char *path = NULL, *outdir = ".";
    int from = 0, count = 10, indent = -1;
    bool cover = false;
    for (int i = 1; i < argc; i++) {
        if (layout_option(&s, argc, argv, &i, &indent)) continue;
        if (!strcmp(argv[i], "--cover")) cover = true;
        else if (i + 1 < argc && !strcmp(argv[i], "-o")) outdir = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--from")) from = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "-n")) count = atoi(argv[++i]);
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else { fprintf(stderr, "pages: bad argument %s\n", argv[i]); return 2; }
    }
    if (!path) { fprintf(stderr, "pages: no book\n"); return 2; }
    finish_setup(&s, indent);
    ibv_t *book = open_book(path);
    if (!book) return 1;

    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    gfx_t g;
    gfx_init(&g, buf, W, H);
    char name[256], out[1024];
    snprintf(name, sizeof name, "%s", basename_of(path));
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    if (cover) {
        draw_cover(&g, book);
        snprintf(out, sizeof out, "%s/%s-cover.png", outdir, name);
        if (png_write(&g, out)) return 1;
    }
    static layout_page_t page;
    layout_pos_t pos = {0, 0};
    int written = 0;
    for (int n = 0; n < from + count; n++) {
        if (!render_page(&g, &s, book, pos, &page)) return 1;
        if (n >= from) {
            snprintf(out, sizeof out, "%s/%s-%04d.png", outdir, name, n + 1);
            if (png_write(&g, out)) return 1;
            written++;
        }
        if (page.at_end) break;
        pos = page.end;
    }
    printf("%s: %d pages written to %s (%s)\n", name, written, outdir, s.font_name);
    ibv_close(book);
    return 0;
}

// Paginates every book to the end and checks what the layout promises:
// pages chain (end = next start), always advance, and nothing is drawn
// outside the text area. The hash covers every page's pixels, footer included.
static int cmd_stats(int argc, char **argv)
{
    setup_t s;
    default_setup(&s);
    const char *books[256];
    int nbooks = 0, indent = -1;
    for (int i = 1; i < argc; i++) {
        if (layout_option(&s, argc, argv, &i, &indent)) continue;
        if (argv[i][0] != '-' && nbooks < 256) books[nbooks++] = argv[i];
        else { fprintf(stderr, "stats: bad argument %s\n", argv[i]); return 2; }
    }
    finish_setup(&s, indent);
    printf("# font %s, text area %dx%d, layout hash %08x\n", s.font_name, s.cfg.width, s.cfg.height,
           layout_hash(&s.cfg));
    printf("%-28s %6s %7s %6s %8s %8s %6s %6s  %s\n", "book", "pages", "paras", "blocks", "ms",
           "us/page", "lines", "ovhg", "hash");
    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    gfx_t g;
    gfx_init(&g, buf, W, H);
    static layout_page_t page;
    int failures = 0;
    for (int b = 0; b < nbooks; b++) {
        ibv_t *book = open_book(books[b]);
        if (!book) { failures++; continue; }
        layout_pos_t pos = {0, 0};
        int overhang = 0;
        uint32_t pages = 0, hash = 0, min_lines = 999, prev_off = 0;
        double layout_ms = 0;
        bool ok = true;
        for (;;) {
            double t0 = now_ms();
            ibv_err_t err = layout_page(&s.cfg, book, pos, &page);
            layout_ms += now_ms() - t0;
            if (err) { fprintf(stderr, "%s: %s\n", books[b], ibv_strerror(err)); ok = false; break; }
            gfx_clear(&g, GFX_WHITE);
            gfx_take_dirty(&g, &(gfx_rect_t){0});
            if (layout_draw(&s.cfg, book, &page, &g, s.margin, s.margin)) { ok = false; break; }
            gfx_rect_t d;
            if (gfx_take_dirty(&g, &d)) {
                // Ink past the text area. Side bearings and italic descenders
                // ('f', 'j' leaning left) stick out a few px by design; a line
                // that is too long shows up as much more.
                int o[3] = {s.margin - d.x, d.x + d.w - s.margin - s.cfg.width, d.y + d.h - s.margin - s.cfg.height};
                for (int i = 0; i < 3; i++)
                    if (o[i] > overhang) overhang = o[i];
            }
            draw_footer(&g, &s, book, &page);
            hash = crc32_update(hash, buf, sizeof buf);
            pages++;
            if (page.line_count > 0 && (uint32_t)page.line_count < min_lines && !page.at_end) min_lines = (uint32_t)page.line_count;
            uint32_t off;
            ibv_text_offset(book, page.end.para, page.end.byte, &off);
            if (page.at_end) break;
            if (page.end.para < pos.para || (page.end.para == pos.para && page.end.byte <= pos.byte) || off < prev_off) {
                fprintf(stderr, "%s: page %u doesn't advance (%u:%u)\n", books[b], pages, pos.para, pos.byte);
                ok = false;
                break;
            }
            prev_off = off;
            pos = page.end;
        }
        printf("%-28.28s %6u %7u %6u %8.1f %8.0f %6u %4dpx  %08x%s\n", basename_of(books[b]), pages,
               book->paragraph_count, book->blocks_loaded, layout_ms, layout_ms * 1000 / (pages ? pages : 1),
               min_lines == 999 ? 0 : min_lines, overhang, hash, ok ? "" : "  FAILED");
        if (overhang > s.cfg.body[0]->pixel_size / 4) {
            fprintf(stderr, "%s: ink %d px outside the text area\n", books[b], overhang);
            ok = false;
        }
        failures += !ok;
        ibv_close(book);
    }
    return failures ? 1 : 0;
}

// Copies a W x H screen into a wider canvas at column `x` (a multiple of 8).
static void blit(gfx_t *dst, const gfx_t *src, int x)
{
    for (int y = 0; y < src->height && y < dst->height; y++)
        memcpy(dst->buf + y * dst->stride + x / 8, src->buf + y * src->stride, (size_t)src->stride);
}

static int cmd_compare(int argc, char **argv)
{
    setup_t s;
    default_setup(&s);
    const char *path = NULL, *out = NULL, *fonts[MAX_FONTS];
    int nfonts = 0, page_no = 1, indent = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f")) { fprintf(stderr, "compare: fonts are positional\n"); return 2; }
        if (layout_option(&s, argc, argv, &i, &indent)) continue;
        if (i + 1 < argc && !strcmp(argv[i], "-o")) out = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--page")) page_no = atoi(argv[++i]);
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else if (argv[i][0] != '-' && nfonts < MAX_FONTS) fonts[nfonts++] = argv[i];
        else { fprintf(stderr, "compare: bad argument %s\n", argv[i]); return 2; }
    }
    if (!path || !out || !nfonts) { fprintf(stderr, "compare: need BOOK, -o OUT.png and fonts\n"); return 2; }
    ibv_t *book = open_book(path);
    if (!book) return 1;

    const int gap = 16;
    gfx_t canvas;
    int cw = nfonts * (W + gap) - gap;
    uint8_t *cbuf = malloc(GFX_BUF_SIZE(cw, H + 20));
    gfx_init(&canvas, cbuf, cw, H + 20);
    gfx_clear(&canvas, GFX_WHITE);
    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    gfx_t g;
    gfx_init(&g, buf, W, H);
    static layout_page_t page;
    for (int f = 0; f < nfonts; f++) {
        int saved_indent = indent;
        load_family(&s, fonts[f]);
        finish_setup(&s, saved_indent);
        layout_pos_t pos = {0, 0};
        for (int n = 1; n <= page_no; n++) {
            if (!render_page(&g, &s, book, pos, &page)) return 1;
            if (page.at_end) break;
            if (n < page_no) pos = page.end;
        }
        int x = f * (W + gap);
        blit(&canvas, &g, x);
        gfx_rect(&canvas, x, 0, W, H, GFX_BLACK);
        gfx_text(&canvas, &font_ui12, x + 2, H + 15, s.font_name, GFX_BLACK);
    }
    int rc = png_write(&canvas, out);
    if (!rc) printf("wrote %s\n", out);
    free(cbuf);
    ibv_close(book);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "screens")) return cmd_screens(argc - 1, argv + 1);
    if (!strcmp(argv[1], "pages")) return cmd_pages(argc - 1, argv + 1);
    if (!strcmp(argv[1], "stats")) return cmd_stats(argc - 1, argv + 1);
    if (!strcmp(argv[1], "compare")) return cmd_compare(argc - 1, argv + 1);
    fprintf(stderr, "usage: sim screens|pages|stats|compare ... (see sim.c)\n");
    return 2;
}
