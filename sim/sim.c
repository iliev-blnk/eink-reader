// Simulatorul de pe PC: rulează nucleul portabil (fonturile, cititorul IBV,
// paginarea) și scrie ca fișiere PNG ce ar arăta panoul de 240x416.
//
//   sim screens [-o DIR]                    ecranele de pornire și de somn
//   sim pages BOOK.ibv [-o DIR] [--from N] [-n COUNT] [--cover] [opțiuni de paginare]
//   sim stats BOOK.ibv... [opțiuni de paginare]  paginează cărți întregi: timpi,
//                                           verificări și un hash al fiecărei pagini
//                                           (testele cu imagini de referință)
//   sim breaks BOOK.ibv... [opțiuni de paginare]  rândurile fiecărei cărți, ca text, pentru
//                                           compararea ruperii cu TeX (tools/texref)
//   sim compare BOOK.ibv -o OUT.png [--page N] FONT... [opțiuni de paginare]
//                                           o pagină în mai multe fonturi, una lângă alta
//   sim ui CARD -o DIR --keys "..."         interfața întreagă, după un scenariu de
//                                           apăsări (sim_ui.c)
//
// Opțiunile de paginare:
//   -f FONT          fontul textului: FONT-r.ibf, plus FONT-b/-i/-bi.ibf dacă există
//                    (de exemplu tools/fonttool/out/fam/literata-18); implicit:
//                    font_ui16, cel încorporat
//   --margin PX      marginea din stânga, dreapta și de sus (implicit 12)
//   --indent PX      alineatul primului rând (implicit: mărimea fontului în pixeli)
//   --line-gap PX    --para-gap PX
//   --ragged         fără justificare
//   --no-hyphen      ignoră cratimele moi
//   --no-chapter-page   capitolele nu încep o pagină nouă
//   --no-keep-lines  permite rândurile văduve și orfane
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

int cmd_ui(int argc, char **argv);   // sim_ui.c

#define W 240
#define H 416
#define FOOTER 22               // px sub zona de text, pentru rândul de jos
#define MAX_FONTS 8

typedef struct {
    layout_cfg_t cfg;
    int margin;
    int indent;                 // -1: mărimea fontului textului
    const char *font_name;      // pentru mesaje
    const char *font_dir;       // --fonts: familia se alege după limba cărții
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

// Încarcă un .ibf într-un buffer care trăiește până la ieșire. NULL dacă
// fișierul nu există; iese din program dacă există, dar nu se deschide.
static const font_t *load_font(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);        // malloc aliniază cel puțin la 8
    font_t *font = malloc(sizeof *font);
    if (!d || !font || fread(d, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s: eroare de citire\n", path);
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
        fprintf(stderr, "%s-r.ibf: nu există\n", prefix);
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

// Cu --fonts, încarcă fontul pe care fontsel îl alege pentru cartea asta, ca dispozitivul.
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

// Citește opțiunile de paginare de la argv[*i]; întoarce false dacă nu e una.
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

// Desenează un text tăiat la max_w px, cu puncte de suspensie.
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
        do len--; while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);   // înapoi cu un caracter
        while (len > 0 && buf[len - 1] == ' ') len--;
        memcpy(buf + len, "\xE2\x80\xA6", 4);
        if (gfx_text_width(f, buf) <= max_w) break;
    }
    gfx_text(g, f, x, y, buf, GFX_BLACK);
}

// Subsolul: titlul capitolului și „pagina / pagini” după ce indexul e complet
// (`idx` poate fi NULL), un procent până atunci.
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

// Paginează și desenează pagina de la `pos`. Întoarce false la o eroare de citire.
static bool render_page(gfx_t *g, const setup_t *s, ibv_t *book, layout_pos_t pos, layout_page_t *page,
                        const pageidx_t *idx)
{
    ibv_err_t err = layout_page(&s->cfg, book, pos, page);
    if (!err) {
        gfx_clear(g, GFX_WHITE);
        err = layout_draw(&s->cfg, book, page, g, s->margin, s->margin);
    }
    if (err) {
        fprintf(stderr, "pagina de la %u:%u: %s\n", pos.para, pos.byte, ibv_strerror(err));
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

    // Ce arată o pornire normală, apoi cele mai lungi texte pe care le poate
    // produce firmware-ul, ca să se verifice că nimic nu iese din lățimea de 240 px.
    const bringup_info_t screens[] = {
        {
            .board = "devkit-proto",
            .chip = "ESP32-S3 v0.2, 2 nuclee",
            .memory = "16M flash, 8M PSRAM",
            .sd = "OK, 15,9 GB, 3 fișiere",
            .battery = "3,92 V",
            .wake = "alimentare · nr. 1",
            .last_button = "ÎNAINTE",
            .counter = 3,
            .last_timing = "ultima: parțială 412 ms",
        },
        {
            .board = "devkit-proto",
            .chip = "ESP32-S3 v0.2, 2 nuclee",
            .memory = "16M flash, fără PSRAM",
            .sd = "niciun card (timp depășit)",
            .battery = "lipsă (0,13 V)",
            .wake = "subtensiune · nr. 1234",
            .last_button = "ÎNAINTE lung",
            .counter = -1234,
            .last_timing = "ultima: parțială 1234 ms",
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
    printf("s-au scris %s/{bringup,bringup-long,sleep}.png\n", outdir);
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
        else { fprintf(stderr, "pages: argument greșit %s\n", argv[i]); return 2; }
    }
    if (!path) { fprintf(stderr, "pages: lipsește cartea\n"); return 2; }
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
    printf("%s: %d pagini scrise în %s (%s)\n", name, written, outdir, s.font_name);
    pageidx_free(&idx);
    ibv_close(book);
    return 0;
}

// Caracterele cărții pentru care fontul textului nu are glifă (desenate ca U+FFFD).
// Spațiile și caracterele de rupere invizibile tratate de paginare nu se numără.
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

// Numără rândul văduv al unei pagini (începe cu ultimul rând al unui paragraf,
// singur) și rândul orfan (se termină cu primul rând al unui paragraf, singur).
// Titlurile nu se numără.
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

// Paginează fiecare carte până la capăt și verifică ce promite paginarea:
// paginile se înlănțuie (end = începutul următoarei) și avansează mereu, nimic
// nu se desenează în afara zonei de text, pagina înapoi și găsirea paginii unei
// poziții (fără index) sunt de acord cu indexul, iar indexul trece neschimbat
// prin fișierul însoțitor. Hash-ul acoperă pixelii fiecărei pagini, cu tot cu subsolul.
static int cmd_stats(int argc, char **argv)
{
    setup_t s;
    default_setup(&s);
    const char *books[256];
    int nbooks = 0, indent = -1;
    for (int i = 1; i < argc; i++) {
        if (layout_option(&s, argc, argv, &i, &indent)) continue;
        if (argv[i][0] != '-' && nbooks < 256) books[nbooks++] = argv[i];
        else { fprintf(stderr, "stats: argument greșit %s\n", argv[i]); return 2; }
    }
    finish_setup(&s, indent);
    uint32_t lh = layout_hash(&s.cfg);
    printf("# font %s, zona de text %dx%d, hash de paginare %08x%s\n", s.font_dir ? "după limbă" : s.font_name, s.cfg.width,
           s.cfg.height, lh,
           s.cfg.keep_lines ? "" : ", rânduri văduve și orfane permise");
    printf("# ruperea rândurilor: %s\n", s.cfg.fit ? "încadrare totală" : "greedy");
    printf("# min: cele mai puține rânduri pe o pagină care nu e ultima; dep: px de cerneală în afara zonei de text;\n"
           "#   văd, orf: rânduri văduve și orfane; lipsă: caractere fără glifă în font\n");
    printf("# înap: pagini așezate ca să se întoarcă o pagină fără index (maximul și media pe cel mult 64 de pagini)\n");
    printf("# cr: %% din rânduri cu cratimă, 2cr: două la rând; r: cu cât e mai lat decât un spațiu simplu un spațiu\n"
           "#   justificat (0,5 = un spațiu și jumătate), media și percentila 95; larg: %% din rânduri cu r > 0,5\n");
    // Lățimile sunt în octeți: fiecare literă cu diacritic are 2, deci primește o coloană în plus.
    printf("%-28s %6s %7s %8s %8s %5s %4s %5s %5s %7s %6s %5s %4s %5s %5s %6s %7s  %-8s  %s\n", "carte", "pagini",
           "parag", "ms", "us/pag", "min", "dep", "văd", "orf", "înap", "medie", "cr", "2cr", "r", "r95", "larg", "lipsă",
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
            if (layout_draw(&s.cfg, book, &page, &g, s.margin, s.margin)) { FAIL("eroare la desenare"); break; }
            gfx_rect_t d;
            if (gfx_take_dirty(&g, &d)) {
                // Cerneală dincolo de zona de text. Spațiile laterale ale glifelor
                // și cozile italicelor ('f', 'j' aplecate la stânga) ies câțiva px
                // prin proiectare; un rând prea lung iese mult mai mult.
                int o[3] = {s.margin - d.x, d.x + d.w - s.margin - s.cfg.width, d.y + d.h - s.margin - s.cfg.height};
                for (int k = 0; k < 3; k++)
                    if (o[k] > overhang) overhang = o[k];
            }
            draw_footer(&g, &s, book, &page, &idx);
            hash = crc32_update(hash, buf, sizeof buf);
            widows_orphans(book, &page, &widows, &orphans);
            quality_page(&q, &s, &page);
            if (page.line_count > 0 && (uint32_t)page.line_count < min_lines && !page.at_end) min_lines = (uint32_t)page.line_count;
            if (page.at_end != (i + 1 == idx.count)) FAIL("pagina %u: at_end greșit", i + 1);
            if (page.at_end) break;
            uint32_t off;
            ibv_text_offset(book, page.end.para, page.end.byte, &off);
            if (layout_pos_cmp(page.end, pos) <= 0 || off < prev_off) FAIL("pagina %u nu avansează (%u:%u)", i + 1, pos.para, pos.byte);
            if (layout_pos_cmp(page.end, idx.starts[i + 1]) != 0) FAIL("pagina %u: sfârșitul nu e începutul paginii următoare", i + 1);
            prev_off = off;
        }

        // Pagina înapoi și găsirea paginii, pe cel mult 64 de pagini răspândite prin carte.
        int prev_max = 0;
        double prev_sum = 0;
        int samples = 0;
        uint32_t step = idx.count > 64 ? idx.count / 64 : 1;
        for (uint32_t i = 1; ok && i < idx.count; i += step) {
            layout_pos_t got;
            int n;
            if ((err = layout_prev(&s.cfg, book, idx.starts[i], &page, &got, &n)) != IBV_OK) { FAIL("înapoi: %s", ibv_strerror(err)); break; }
            if (layout_pos_cmp(got, idx.starts[i - 1]) != 0) FAIL("înapoi de la pagina %u dă %u:%u, nu pagina %u", i + 1, got.para, got.byte, i);
            if (n > prev_max) prev_max = n;
            prev_sum += n;
            samples++;
            // O poziție în interiorul paginii (un octet după început, dacă pagina e mai lungă de atât).
            layout_pos_t mid = idx.starts[i];
            mid.byte++;
            if (i + 1 < idx.count && layout_pos_cmp(mid, idx.starts[i + 1]) >= 0) mid = idx.starts[i];
            if ((err = layout_locate(&s.cfg, book, mid, &page, &got)) != IBV_OK) { FAIL("găsirea poziției: %s", ibv_strerror(err)); break; }
            if (layout_pos_cmp(got, idx.starts[i]) != 0) FAIL("poziția %u:%u dă %u:%u, nu pagina %u", mid.para, mid.byte, got.para, got.byte, i + 1);
            if (pageidx_find(&idx, mid) != (int32_t)i) FAIL("pageidx_find %u:%u nu e pagina %u", mid.para, mid.byte, i + 1);
        }
        layout_pos_t first;
        if (layout_prev(&s.cfg, book, (layout_pos_t){0, 0}, &page, &first, NULL) != IBV_ERR_RANGE) FAIL("înapoi de la pagina 1 nu dă eroare");

        // Fișierul însoțitor: dus-întors dă același index, un octet inversat sau altă paginare nu dau niciunul.
        pageidx_t back;
        pageidx_init(&back, lh);
        if (!pageidx_save(&idx, book->book_id, sidecar)) FAIL("nu se poate scrie %s", sidecar);
        else if (!pageidx_load(&back, book->book_id, lh, sidecar) || back.count != idx.count || !back.complete
                 || memcmp(back.starts, idx.starts, idx.count * sizeof *idx.starts))
            FAIL("fișierul însoțitor nu dă înapoi același index");
        else if (pageidx_load(&back, book->book_id, lh ^ 1, sidecar)) FAIL("fișierul însoțitor se încarcă pentru altă paginare");
        else {
            FILE *f = fopen(sidecar, "r+b");
            fseek(f, 30, SEEK_SET);
            int c = fgetc(f);
            fseek(f, 30, SEEK_SET);
            fputc(c ^ 0x10, f);
            fclose(f);
            if (pageidx_load(&back, book->book_id, lh, sidecar)) FAIL("fișierul însoțitor deteriorat se încarcă");
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
               ok ? "" : "  EȘUAT");
        free(q.r);
        if (getenv("SIM_FIT")) fprintf(stderr, "%s: încadrare totală la trecerea 1 %u, la trecerea 2 %u, greedy %u\n", name, broken[0], broken[1], broken[2]);
        if (overhang > s.cfg.body[0]->pixel_size / 4) FAIL("cerneală la %d px în afara zonei de text", overhang);
#undef FAIL
        failures += !ok;
        pageidx_free(&idx);
        ibv_close(book);
    }
    return failures ? 1 : 0;
}

// Copiază un ecran W x H într-o pânză mai lată, la coloana `x` (multiplu de 8).
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
        if (!strcmp(argv[i], "-f")) { fprintf(stderr, "compare: fonturile se dau fără -f\n"); return 2; }
        if (layout_option(&s, argc, argv, &i, &indent)) continue;
        if (i + 1 < argc && !strcmp(argv[i], "-o")) out = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--page")) page_no = atoi(argv[++i]);
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else if (argv[i][0] != '-' && nfonts < MAX_FONTS) fonts[nfonts++] = argv[i];
        else { fprintf(stderr, "compare: argument greșit %s\n", argv[i]); return 2; }
    }
    if (!path || !out || !nfonts) { fprintf(stderr, "compare: trebuie cartea, -o OUT.png și fonturile\n"); return 2; }
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
    if (!rc) printf("s-a scris %s\n", out);
    free(cbuf);
    ibv_close(book);
    return rc;
}

// Scrie fiecare rând al fiecărei cărți: înregistrarea, octeții [start, end),
// indicatorii, începutul peniței, justificarea și numărul de spații. E ce
// ajunge pe ecran, luat din paginare, nu din interiorul ruperii în rânduri.
// Antetul fiecărei cărți are și avansul spațiului (1/64 px), pentru E6.
static int cmd_breaks(int argc, char **argv)
{
    setup_t s;
    default_setup(&s);
    const char *books[256];
    int nbooks = 0, indent = -1;
    for (int i = 1; i < argc; i++) {
        if (layout_option(&s, argc, argv, &i, &indent)) continue;
        if (argv[i][0] != '-' && nbooks < 256) books[nbooks++] = argv[i];
        else { fprintf(stderr, "breaks: argument greșit %s\n", argv[i]); return 2; }
    }
    finish_setup(&s, indent);
    static layout_page_t page;
    int failures = 0;
    for (int b = 0; b < nbooks; b++) {
        ibv_t *book = open_book(books[b]);
        if (!book) { failures++; continue; }
        book_setup(&s, book);
        const font_t *body = s.cfg.body[0];
        printf("book %s font %s width %d indent %d hyphenate %d justify %d fit %d version %d space %u\n", books[b],
               s.font_name, s.cfg.width, s.cfg.indent, s.cfg.hyphenate, s.cfg.justify, s.cfg.fit != NULL, LAYOUT_VERSION,
               body->glyphs[font_index_or_fallback(body, ' ')].advance);
        if (s.cfg.fit) memset(s.cfg.fit->broken, 0, sizeof s.cfg.fit->broken);
        layout_pos_t pos = {0, 0};
        for (;;) {
            ibv_err_t err = layout_page(&s.cfg, book, pos, &page);
            if (err) { fprintf(stderr, "%s: %s\n", books[b], ibv_strerror(err)); failures++; break; }
            for (int i = 0; i < page.line_count; i++) {
                const layout_line_t *l = &page.lines[i];
                printf("%u %u %u %u %d %d %u\n", l->para, l->start, l->end, l->flags, l->x, l->extra, l->gaps);
            }
            if (page.at_end || layout_pos_cmp(page.end, pos) <= 0) break;
            pos = page.end;
        }
        if (s.cfg.fit)
            printf("passes %u %u %u\n", s.cfg.fit->broken[0], s.cfg.fit->broken[1], s.cfg.fit->broken[2]);
        ibv_close(book);
    }
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "screens")) return cmd_screens(argc - 1, argv + 1);
    if (!strcmp(argv[1], "pages")) return cmd_pages(argc - 1, argv + 1);
    if (!strcmp(argv[1], "stats")) return cmd_stats(argc - 1, argv + 1);
    if (!strcmp(argv[1], "breaks")) return cmd_breaks(argc - 1, argv + 1);
    if (!strcmp(argv[1], "compare")) return cmd_compare(argc - 1, argv + 1);
    if (!strcmp(argv[1], "ui")) return cmd_ui(argc - 1, argv + 1);
    fprintf(stderr, "folosire: sim screens|pages|stats|breaks|compare|ui ... (vezi sim.c)\n");
    return 2;
}
