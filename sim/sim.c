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
//   --no-keep-lines  allow widows and orphans
//   --greedy         rupere greedy în rânduri (fără încadrare totală)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "crc32.h"
#include "gfx.h"
#include "ibv.h"
#include "layout.h"
#include "fontsel.h"
#include "pageidx.h"
#include "png.h"
#include "screen_bringup.h"

#define W 240
#define H 416
#define FOOTER 22               // px below the text area for the footer line
#define MAX_FONTS 8

typedef struct {
    layout_cfg_t cfg;
    int margin;
    int indent;                 // -1: the body font's size
    const char *font_name;      // for messages
    const char *font_dir;       // --fonts: pick the family by the book's language
    int size_step;
    char font_buf[256];
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
    s->indent = indent;
    s->cfg.indent = indent >= 0 ? indent : s->cfg.body[0]->pixel_size;
}

// With --fonts, loads the font fontsel picks for this book, as the device does.
static void book_setup(setup_t *s, const ibv_t *book)
{
    if (!s->font_dir) return;
    fontsel_t f = fontsel_pick(book->meta[IBV_LANG], s->size_step);
    char prefix[1024];
    snprintf(prefix, sizeof prefix, "%s/%s-%u", s->font_dir, f.family, (unsigned)f.size);
    load_family(s, prefix);
    snprintf(s->font_buf, sizeof s->font_buf, "%s-%u", f.family, (unsigned)f.size);
    s->font_name = s->font_buf;
    s->cfg.indent = s->indent >= 0 ? s->indent : s->cfg.body[0]->pixel_size;
}

// Parses the layout options at argv[*i]; returns false if it isn't one.
static bool layout_option(setup_t *s, int argc, char **argv, int *i, int *indent)
{
    const char *a = argv[*i];
    const char *v = *i + 1 < argc ? argv[*i + 1] : NULL;
    if (!strcmp(a, "--ragged")) s->cfg.justify = false;
    else if (!strcmp(a, "--no-hyphen")) s->cfg.hyphenate = false;
    else if (!strcmp(a, "--no-chapter-page")) s->cfg.chapter_page = false;
    else if (!strcmp(a, "--no-keep-lines")) s->cfg.keep_lines = false;
    else if (!strcmp(a, "--greedy")) s->cfg.fit = NULL;
    else if (v && !strcmp(a, "-f")) load_family(s, v), ++*i;
    else if (v && !strcmp(a, "--fonts")) s->font_dir = v, ++*i;
    else if (v && !strcmp(a, "--size")) s->size_step = atoi(v), ++*i;
    else if (v && !strcmp(a, "--margin")) s->margin = atoi(v), ++*i;
    else if (v && !strcmp(a, "--indent")) *indent = atoi(v), ++*i;
    else if (v && !strcmp(a, "--line-gap")) s->cfg.line_gap = atoi(v), ++*i;
    else if (v && !strcmp(a, "--para-gap")) s->cfg.para_gap = atoi(v), ++*i;
    else return false;
    return true;
}

static void default_setup(setup_t *s)
{
    static layout_fit_t fit;
    memset(s, 0, sizeof *s);
    s->cfg.fit = &fit;
    s->margin = 12;
    s->cfg.justify = true;
    s->cfg.hyphenate = true;
    s->cfg.chapter_page = true;
    s->cfg.keep_lines = true;
    s->size_step = FONTSEL_DEFAULT_STEP;
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

// Footer: chapter title, and "page / pages" once the index is complete (`idx`
// may be NULL), a percentage before that.
static void draw_footer(gfx_t *g, const setup_t *s, ibv_t *book, const layout_page_t *page, const pageidx_t *idx)
{
    const font_t *f = &font_ui12;
    int y = H - 7;
    uint32_t off = 0;
    ibv_text_offset(book, page->start.para, page->start.byte, &off);
    char pct[24];
    int32_t n = idx && idx->complete ? pageidx_find(idx, page->start) : -1;
    if (n >= 0) snprintf(pct, sizeof pct, "%d / %u", n + 1, idx->count);
    else snprintf(pct, sizeof pct, "%u%%", page->at_end ? 100u : (unsigned)((uint64_t)off * 100 / (book->text_bytes ? book->text_bytes : 1)));
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
static bool render_page(gfx_t *g, const setup_t *s, ibv_t *book, layout_pos_t pos, layout_page_t *page,
                        const pageidx_t *idx)
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
    draw_footer(g, s, book, page, idx);
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
    book_setup(&s, book);

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
    pageidx_t idx;
    pageidx_init(&idx, layout_hash(&s.cfg));
    ibv_err_t err = pageidx_step(&idx, &s.cfg, book, 1 << 30, &page);
    if (err) { fprintf(stderr, "%s: %s\n", path, ibv_strerror(err)); return 1; }
    layout_pos_t pos = {0, 0};
    int written = 0;
    for (int n = 0; n < from + count; n++) {
        if (!render_page(&g, &s, book, pos, &page, &idx)) return 1;
        if (n >= from) {
            snprintf(out, sizeof out, "%s/%s-%04d.png", outdir, name, n + 1);
            if (png_write(&g, out)) return 1;
            written++;
        }
        if (page.at_end) break;
        pos = page.end;
    }
    printf("%s: %d pages written to %s (%s)\n", name, written, outdir, s.font_name);
    pageidx_free(&idx);
    ibv_close(book);
    return 0;
}

// Counts a page's widow (it starts with a paragraph's last line alone) and
// orphan (it ends with a paragraph's first line alone). Headings don't count.
// Characters of the book the body font has no glyph for (drawn as U+FFFD).
// Spaces and the invisible break characters the layout handles don't count.
static uint32_t missing_glyphs(ibv_t *book, const font_t *font)
{
    uint32_t missing = 0;
    for (uint32_t p = 0; p < book->paragraph_count; p++) {
        ibv_record_t r;
        if (ibv_record(book, p, &r)) break;
        const char *t = r.text, *end = r.text + r.text_len;
        while (t < end) {
            uint32_t cp = utf8_next_n(&t, end);
            if (cp <= 0x20 || cp == 0xA0 || cp == 0xAD || (cp >= 0x200B && cp <= 0x200D) || cp == 0x2028
                || cp == 0x2060 || cp == 0xFEFF)
                continue;
            missing += font_index(font, cp) < 0;
        }
    }
    return missing;
}

// Calitatea ruperii în rânduri (experimentul E6). Spațiile unui rând
// justificat se desenează mai late (sau mai înguste) decât un spațiu simplu cu
// r = extra / gaps / lățimea spațiului: 0 e un spațiu simplu, 0,5 un spațiu și
// jumătate, -0,33 cel mai mult cât îngustează încadrarea totală. Rândurile
// nejustificate (ultimele rânduri, text ragged) nu se numără.
typedef struct {
    uint32_t text_lines, hyphens, double_hyphens, n, cap;
    double *r;
    bool last_hyphen;
} quality_t;

static void quality_page(quality_t *q, const setup_t *s, const layout_page_t *pg)
{
    const font_t *f = s->cfg.body[0];
    const double space = f->glyphs[font_index_or_fallback(f, ' ')].advance;
    for (int i = 0; i < pg->line_count; i++) {
        const layout_line_t *ln = &pg->lines[i];
        if (ln->flags & (LINE_HEADING | LINE_ORNAMENT)) { q->last_hyphen = false; continue; }
        q->text_lines++;
        bool hy = ln->flags & LINE_HYPHEN;
        q->hyphens += hy;
        q->double_hyphens += hy && q->last_hyphen;
        q->last_hyphen = hy;
        if (!ln->gaps) continue;
        if (q->n == q->cap) q->cap = q->cap ? 2 * q->cap : 4096, q->r = realloc(q->r, q->cap * sizeof *q->r);
        q->r[q->n++] = ln->extra / (double)ln->gaps / space;
    }
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void widows_orphans(ibv_t *book, const layout_page_t *pg, int *widows, int *orphans)
{
    if (pg->line_count == 0) return;
    const layout_line_t *a = &pg->lines[0], *z = &pg->lines[pg->line_count - 1];
    ibv_record_t r;
    if (pg->start.byte > 0 && !(a->flags & (LINE_HEADING | LINE_ORNAMENT))
        && (pg->line_count == 1 || pg->lines[1].para != a->para) && !ibv_record(book, a->para, &r)
        && a->end >= r.text_len)
        ++*widows;
    if (!pg->at_end && z->start == 0 && !(z->flags & (LINE_HEADING | LINE_ORNAMENT)) && pg->end.para == z->para
        && pg->end.byte > 0)
        ++*orphans;
}

// Paginates every book to the end and checks what the layout promises: pages
// chain (end = next start) and always advance, nothing is drawn outside the
// text area, going back a page and finding the page of a position (without
// the index) agree with the index, and the index survives its sidecar file.
// The hash covers every page's pixels, footer included.
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
    uint32_t lh = layout_hash(&s.cfg);
    printf("# font %s, text area %dx%d, layout hash %08x%s\n", s.font_dir ? "by language" : s.font_name, s.cfg.width,
           s.cfg.height, lh,
           s.cfg.keep_lines ? "" : ", widows and orphans allowed");
    printf("# line breaking: %s\n", s.cfg.fit ? "total fit" : "greedy");
    printf("# prev: pages laid out to go back one page without the index (max, mean over up to 64 pages)\n");
    printf("# hyph: %% of lines hyphenated, 2hy: two in a row; r: how much wider than a plain space justified\n"
           "#   spaces are (0.5 = a space and a half), mean, 95th percentile; loose: %% of lines with r > 0.5\n");
    printf("%-28s %6s %7s %8s %8s %5s %4s %4s %5s %6s %6s %5s %4s %5s %5s %6s %6s  %-8s  %s\n", "book", "pages", "paras",
           "ms", "us/page", "lines", "ovhg", "wid", "orph", "prev", "mean", "hyph", "2hy", "r", "r95", "loose", "miss",
           "hash", "font");
    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    gfx_t g;
    gfx_init(&g, buf, W, H);
    static layout_page_t page;
    char sidecar[64];
    snprintf(sidecar, sizeof sidecar, "/tmp/sim-%d.pg", (int)getpid());
    int failures = 0;
    for (int b = 0; b < nbooks; b++) {
        ibv_t *book = open_book(books[b]);
        if (!book) { failures++; continue; }
        if (s.font_dir) book_setup(&s, book), lh = layout_hash(&s.cfg);
        const char *name = basename_of(books[b]);
        bool ok = true;
#define FAIL(...) do { fprintf(stderr, "%s: ", name); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ok = false; } while (0)
        pageidx_t idx;
        pageidx_init(&idx, lh);
        if (s.cfg.fit) memset(s.cfg.fit->broken, 0, sizeof s.cfg.fit->broken);
        double t0 = now_ms();
        ibv_err_t err = pageidx_step(&idx, &s.cfg, book, 1 << 30, &page);
        double layout_ms = now_ms() - t0;
        if (err) FAIL("%s", ibv_strerror(err));

        int overhang = 0, widows = 0, orphans = 0;
        quality_t q = {0};
        uint32_t hash = 0, min_lines = 999, prev_off = 0;
        for (uint32_t i = 0; ok && i < idx.count; i++) {
            layout_pos_t pos = idx.starts[i];
            if ((err = layout_page(&s.cfg, book, pos, &page)) != IBV_OK) { FAIL("%s", ibv_strerror(err)); break; }
            gfx_clear(&g, GFX_WHITE);
            gfx_take_dirty(&g, &(gfx_rect_t){0});
            if (layout_draw(&s.cfg, book, &page, &g, s.margin, s.margin)) { FAIL("draw error"); break; }
            gfx_rect_t d;
            if (gfx_take_dirty(&g, &d)) {
                // Ink past the text area. Side bearings and italic descenders
                // ('f', 'j' leaning left) stick out a few px by design; a line
                // that is too long shows up as much more.
                int o[3] = {s.margin - d.x, d.x + d.w - s.margin - s.cfg.width, d.y + d.h - s.margin - s.cfg.height};
                for (int k = 0; k < 3; k++)
                    if (o[k] > overhang) overhang = o[k];
            }
            draw_footer(&g, &s, book, &page, &idx);
            hash = crc32_update(hash, buf, sizeof buf);
            widows_orphans(book, &page, &widows, &orphans);
            quality_page(&q, &s, &page);
            if (page.line_count > 0 && (uint32_t)page.line_count < min_lines && !page.at_end) min_lines = (uint32_t)page.line_count;
            if (page.at_end != (i + 1 == idx.count)) FAIL("page %u: at_end is wrong", i + 1);
            if (page.at_end) break;
            uint32_t off;
            ibv_text_offset(book, page.end.para, page.end.byte, &off);
            if (layout_pos_cmp(page.end, pos) <= 0 || off < prev_off) FAIL("page %u doesn't advance (%u:%u)", i + 1, pos.para, pos.byte);
            if (layout_pos_cmp(page.end, idx.starts[i + 1]) != 0) FAIL("page %u: end isn't the next page's start", i + 1);
            prev_off = off;
        }

        // Back paging and locating, on up to 64 pages spread over the book.
        int prev_max = 0;
        double prev_sum = 0;
        int samples = 0;
        uint32_t step = idx.count > 64 ? idx.count / 64 : 1;
        for (uint32_t i = 1; ok && i < idx.count; i += step) {
            layout_pos_t got;
            int n;
            if ((err = layout_prev(&s.cfg, book, idx.starts[i], &page, &got, &n)) != IBV_OK) { FAIL("prev: %s", ibv_strerror(err)); break; }
            if (layout_pos_cmp(got, idx.starts[i - 1]) != 0) FAIL("prev of page %u is %u:%u, not page %u", i + 1, got.para, got.byte, i);
            if (n > prev_max) prev_max = n;
            prev_sum += n;
            samples++;
            // A position inside the page (one byte in, if the page is longer than that).
            layout_pos_t mid = idx.starts[i];
            mid.byte++;
            if (i + 1 < idx.count && layout_pos_cmp(mid, idx.starts[i + 1]) >= 0) mid = idx.starts[i];
            if ((err = layout_locate(&s.cfg, book, mid, &page, &got)) != IBV_OK) { FAIL("locate: %s", ibv_strerror(err)); break; }
            if (layout_pos_cmp(got, idx.starts[i]) != 0) FAIL("locate %u:%u gives %u:%u, not page %u", mid.para, mid.byte, got.para, got.byte, i + 1);
            if (pageidx_find(&idx, mid) != (int32_t)i) FAIL("pageidx_find %u:%u isn't page %u", mid.para, mid.byte, i + 1);
        }
        layout_pos_t first;
        if (layout_prev(&s.cfg, book, (layout_pos_t){0, 0}, &page, &first, NULL) != IBV_ERR_RANGE) FAIL("prev of page 1 isn't an error");

        // The sidecar: a round trip gives the same index, a flipped byte or another layout gives none.
        pageidx_t back;
        pageidx_init(&back, lh);
        if (!pageidx_save(&idx, book->book_id, sidecar)) FAIL("can't write %s", sidecar);
        else if (!pageidx_load(&back, book->book_id, lh, sidecar) || back.count != idx.count || !back.complete
                 || memcmp(back.starts, idx.starts, idx.count * sizeof *idx.starts))
            FAIL("sidecar round trip differs");
        else if (pageidx_load(&back, book->book_id, lh ^ 1, sidecar)) FAIL("sidecar loads for another layout");
        else {
            FILE *f = fopen(sidecar, "r+b");
            fseek(f, 30, SEEK_SET);
            int c = fgetc(f);
            fseek(f, 30, SEEK_SET);
            fputc(c ^ 0x10, f);
            fclose(f);
            if (pageidx_load(&back, book->book_id, lh, sidecar)) FAIL("damaged sidecar loads");
        }
        remove(sidecar);
        pageidx_free(&back);

        uint32_t broken[3] = {0};
        if (s.cfg.fit) memcpy(broken, s.cfg.fit->broken, sizeof broken);
        double r_mean = 0, r95 = 0, loose = 0;
        if (q.n) {
            qsort(q.r, q.n, sizeof *q.r, cmp_double);
            for (uint32_t i = 0; i < q.n; i++) r_mean += q.r[i], loose += q.r[i] > 0.5;
            r_mean /= q.n;
            r95 = q.r[q.n * 95 / 100];
            loose = 100 * loose / q.n;
        }
        printf("%-28.28s %6u %7u %8.1f %8.0f %5u %2dpx %4d %5d %6d %6.1f %5.1f %4u %5.2f %5.2f %6.1f %6u  %08x  %s%s\n",
               name, idx.count, book->paragraph_count, layout_ms, layout_ms * 1000 / (idx.count ? idx.count : 1),
               min_lines == 999 ? 0 : min_lines, overhang, widows, orphans, prev_max,
               samples ? prev_sum / samples : 0.0, q.text_lines ? 100.0 * q.hyphens / q.text_lines : 0.0,
               q.double_hyphens, r_mean, r95, loose, missing_glyphs(book, s.cfg.body[0]), hash, s.font_name,
               ok ? "" : "  FAILED");
        free(q.r);
        if (getenv("SIM_FIT")) fprintf(stderr, "%s: total fit pass 1 %u, pass 2 %u, greedy %u\n", name, broken[0], broken[1], broken[2]);
        if (overhang > s.cfg.body[0]->pixel_size / 4) FAIL("ink %d px outside the text area", overhang);
#undef FAIL
        failures += !ok;
        pageidx_free(&idx);
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
            if (!render_page(&g, &s, book, pos, &page, NULL)) return 1;
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
