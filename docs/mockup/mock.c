// Ecranele de lectură și de bibliotecă pentru macheta carcasei, desenate cu codul
// gfx + font al firmware-ului. Paragrafele se rup ca întreg (cea mai mică
// proastă așezare totală peste toate punctele de rupere, justificat), cu
// cratimele moi puse de hyph.py. E o previzualizare a direcției de PAGINARE; nu
// e motorul final.
//
//   mock <outdir> <bookdir>   (formatul cărții: "# titlu", "P text", "V vers", "S")
//
// Fonturile din fonts/ sunt în formatul font_t de dinainte de IBF, deci paginile de
// lectură și biblioteca se desenează cu nucleul din commit-ul machetei (d9c9e0b):
//   git archive d9c9e0b firmware/components/core firmware/main | tar -x -C /tmp/old
//   O=/tmp/old/firmware
//   cc -O2 -o mock -I$O/components/core/include -I$O/main docs/mockup/mock.c $O/main/screen_bringup.c \
//      $O/components/core/*.c $O/components/core/fonts/font_ui16.c docs/mockup/fonts/*.c -lm
// Ecranele de verificare și de somn se iau dintr-o compilare cu nucleul de acum, ca să
// fie exact ce desenează firmware-ul (paginile ei de lectură ies goale, se aruncă):
//   I=firmware/components/core
//   cc -O2 -o mock-now -I$I/include -Ifirmware/main docs/mockup/mock.c firmware/main/screen_bringup.c \
//      $I/gfx.c $I/font.c $I/crc32.c $I/fonts/font_ui16.c docs/mockup/fonts/*.c -lm
// Fiecare scrie .pbm în <outdir>; se convertesc în PNG de 1 bit în docs/mockup/screens/.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "gfx.h"
#include "screen_bringup.h"

extern const font_t font_serif16, font_serif18, font_serif18b, font_ui13;

#define W 240
#define H 416
#define ML 14
#define TW (W - 2 * ML)
#define LH 24          // pasul rândurilor de text (serif de 18 px)
#define TOP 20         // prima linie de bază e la TOP + ascent
#define BOTTOM 382     // nicio linie de bază a textului sub asta
#define FOOT 405       // linia de bază a subsolului
#define INDENT 14
#define SHOW_PAGES 8

static uint8_t s_buf[GFX_BUF_SIZE(W, H)];
static gfx_t s_g;
static const font_t *s_body = &font_serif18;  // pe carte, ca o setare a mărimii fontului
static int s_lh = LH;

static int write_pbm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fprintf(f, "P4\n%d %d\n", W, H);
    for (int i = 0; i < s_g.stride * H; i++) fputc(~s_g.buf[i] & 0xFF, f);
    fclose(f);
    return 0;
}

// ---- paginarea --------------------------------------------------------------

typedef struct { char **items; int n; } book_t;

static book_t load(const char *path)
{
    book_t b = {0};
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, f)) > 0) {
        if (line[len - 1] == '\n') line[--len] = 0;
        if (!len) continue;
        b.items = realloc(b.items, sizeof(char *) * (b.n + 1));
        b.items[b.n++] = strdup(line);
    }
    free(line);
    fclose(f);
    return b;
}

// Paginarea rulează de două ori: o dată ca să numere paginile, încă o dată ca să deseneze o pagină anume.
typedef struct {
    int page, y;        // pagina curentă și linia de bază
    int draw_page;      // pagina de desenat, sau -1
    int fresh;          // încă nimic pus pe pagina asta
} pen_t;

static void new_page(pen_t *p)
{
    p->page++;
    p->y = TOP + s_body->ascent;
    p->fresh = 1;
}

static void need_line(pen_t *p)
{
    if (!p->fresh) p->y += s_lh;
    if (p->y > BOTTOM) new_page(p);
    p->fresh = 0;
}

static int drawing(const pen_t *p) { return p->page == p->draw_page; }

static void put_text(pen_t *p, const font_t *f, int x, const char *s)
{
    if (drawing(p)) gfx_text(&s_g, f, x, p->y, s, GFX_BLACK);
}

static void heading(pen_t *p, const char *text)
{
    if (!p->fresh || p->page == 0) new_page(p);
    p->y = TOP + 64;  // capitolele încep puțin mai jos pe pagină
    int w = gfx_text_width(&font_serif18b, text);
    put_text(p, &font_serif18b, (W - w) / 2, text);
    p->y += LH;       // un rând gol sub titlu
    p->fresh = 0;
}

// Paragrafele se taie în fragmente: la spații, la spațiile neseparabile (U+00A0,
// se lățesc, dar nu se rup niciodată), la cratimele moi (U+00AD, puse de hyph.py)
// și după o cratimă obișnuită. Apoi rupturile de rând se aleg pentru tot paragraful
// odată, în stilul Knuth-Plass: se minimizează suma (10 + badness)^2 plus
// penalizările cratimelor, deci un rând strâns poate scuti următorul de goluri uriașe.
enum { AFTER_SPACE, AFTER_TIE, AFTER_SOFT, AFTER_HARD, AFTER_END };

typedef struct {
    const char *s;
    int len, width, after;
} frag_t;

#define MAX_FRAGS 8192
static frag_t s_frag[MAX_FRAGS];
static double s_cost[MAX_FRAGS];
static int s_from[MAX_FRAGS], s_breaks[MAX_FRAGS];

static int width_of(const char *s, int len)
{
    char tmp[256];
    if (len >= (int)sizeof tmp) len = sizeof tmp - 1;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    return gfx_text_width(s_body, tmp);
}

static int split(const char *t)
{
    int n = 0;
    const char *start = t;
    while (n < MAX_FRAGS - 1) {
        int after = -1, skip = 0;
        const char *end = t;
        if (*t == 0) after = AFTER_END;
        else if (*t == ' ') after = AFTER_SPACE, skip = 1;
        else if (!memcmp(t, "\xc2\xa0", 2)) after = AFTER_TIE, skip = 2;
        else if (!memcmp(t, "\xc2\xad", 2)) after = AFTER_SOFT, skip = 2;
        else if (*t == '-' && t - start >= 3 && t[1] && t[1] != ' ' && t[2] && t[2] != ' ' && t[3] && t[3] != ' ')
            after = AFTER_HARD, skip = 1, end = t + 1;
        if (after < 0) { t++; continue; }
        if (end > start || after == AFTER_END) {
            s_frag[n] = (frag_t){start, (int)(end - start), width_of(start, end - start), after};
            n++;
        } else if (n && after == AFTER_SPACE) {
            s_frag[n - 1].after = AFTER_SPACE;  // spațiile repetate devin unul
        }
        if (after == AFTER_END) break;
        t += skip;
        while (after == AFTER_SPACE && *t == ' ') t++;
        start = t;
    }
    return n;
}

static int gap_after(int k) { return s_frag[k].after == AFTER_SPACE || s_frag[k].after == AFTER_TIE; }

// Lățimea naturală și numărul de goluri ale fragmentelor a..b ca un singur rând.
static int line_width(int a, int b, int *gaps)
{
    const int space = font_glyph(s_body, ' ')->advance;
    int w = 0, g = 0;
    for (int k = a; k <= b; k++) {
        w += s_frag[k].width;
        if (k < b && gap_after(k)) w += space, g++;
    }
    if (s_frag[b].after == AFTER_SOFT) w += font_glyph(s_body, '-')->advance;
    *gaps = g;
    return w;
}

static void paragraph(pen_t *p, const char *text, int indent)
{
    const int space = font_glyph(s_body, ' ')->advance;
    const double stretch = space, shrink = 1.0;  // pe gol, px
    int n = split(text);
    if (n == 0) return;

    // s_cost[b]: cel mai bun total pentru textul până la o rupere după fragmentul b.
    for (int b = 0; b < n; b++) {
        s_cost[b] = 1e300;
        int after = s_frag[b].after;
        if (after == AFTER_TIE) continue;
        for (int a = b; a >= 0; a--) {
            if (a > 0 && s_frag[a - 1].after == AFTER_TIE) continue;  // aici nu poate începe un rând
            int gaps, w = line_width(a, b, &gaps);
            int avail = TW - (a == 0 ? indent : 0);
            if (w - gaps * shrink > avail && a < b) break;
            // badness = 100 r^3; un cuvânt singur pe un rând justificat contează ca mai
            // rău decât orice rând larg, deci se întâmplă doar când nu încape altceva.
            double bad;
            if (w > avail) bad = gaps ? 100 * pow((w - avail) / (gaps * shrink), 3) : 1e7;
            else if (after == AFTER_END) bad = 0;  // ultimul rând e nejustificat
            else bad = gaps ? 100 * pow((avail - w) / (gaps * stretch), 3) : 1e7;
            if (bad > 1e7) bad = 1e7;
            double d = (10 + bad) * (10 + bad);
            if (after == AFTER_SOFT || after == AFTER_HARD) {
                d += 50 * 50;
                if (a > 0 && (s_frag[a - 1].after == AFTER_SOFT || s_frag[a - 1].after == AFTER_HARD)) d += 3000;
            }
            double total = (a == 0 ? 0 : s_cost[a - 1]) + d;
            if (total < s_cost[b]) s_cost[b] = total, s_from[b] = a;
        }
    }

    int nb = 0;
    for (int b = n - 1; b >= 0; b = s_from[b] - 1) s_breaks[nb++] = b;

    for (int li = nb - 1; li >= 0; li--) {
        int b = s_breaks[li], a = (li == nb - 1) ? 0 : s_breaks[li + 1] + 1;
        int gaps, w = line_width(a, b, &gaps);
        int x = ML + (a == 0 ? indent : 0);
        int avail = TW - (a == 0 ? indent : 0);
        int extra = avail - w;
        if (s_frag[b].after == AFTER_END && extra > 0) extra = 0;
        need_line(p);
        int g = 0;
        char tmp[256];
        for (int k = a; k <= b; k++) {
            int len = s_frag[k].len < 255 ? s_frag[k].len : 255;
            memcpy(tmp, s_frag[k].s, len);
            tmp[len] = 0;
            put_text(p, s_body, x, tmp);
            x += s_frag[k].width;
            if (k < b && gap_after(k)) {
                x += space + (gaps ? extra * (g + 1) / gaps - extra * g / gaps : 0);
                g++;
            }
        }
        if (s_frag[b].after == AFTER_SOFT) put_text(p, s_body, x, "-");
    }
}

// Versurile își păstrează rândurile. Unul prea lung pentru ecran se rupe la
// ultimul spațiu care încape, iar restul se așază la dreapta pe rândul următor.
static void verse(pen_t *p, const char *text)
{
    char line[512];
    snprintf(line, sizeof line, "%s", text);
    char *rest = line;
    int first = 1;
    while (*rest) {
        char *cut = NULL;
        if (gfx_text_width(s_body, rest) > TW) {
            for (char *q = rest; *q; q++) {
                if (*q != ' ') continue;
                *q = 0;
                int fits = gfx_text_width(s_body, rest) <= TW;
                *q = ' ';
                if (!fits) break;
                cut = q;
            }
        }
        if (cut) *cut = 0;
        need_line(p);
        put_text(p, s_body, first ? ML : W - ML - gfx_text_width(s_body, rest), rest);
        rest = cut ? cut + 1 : rest + strlen(rest);
        first = 0;
    }
}

static int layout(const book_t *b, int draw_page)
{
    pen_t p = {.page = 0, .y = 0, .draw_page = draw_page, .fresh = 1};
    int after_heading = 0;
    for (int i = 0; i < b->n; i++) {
        const char *it = b->items[i];
        const char *rest = it + 2;
        switch (it[0]) {
        case '#':
            heading(&p, rest);
            after_heading = 1;
            break;
        case 'P':
            if (p.page == 0) new_page(&p);
            paragraph(&p, rest, after_heading ? 0 : INDENT);
            after_heading = 0;
            break;
        case 'V':
            if (p.page == 0) new_page(&p);
            verse(&p, rest);
            break;
        case 'S':
            // Un separator de strofă care ar cădea sus pe pagină se scoate.
            if (!p.fresh && p.y + s_lh <= BOTTOM) p.y += s_lh / 2;
            break;
        }
    }
    return p.page;
}

static void footer(const char *title, int page, int pages)
{
    char num[32];
    snprintf(num, sizeof num, "%d / %d", page, pages);
    gfx_text(&s_g, &font_ui13, ML, FOOT, title, GFX_BLACK);
    gfx_text(&s_g, &font_ui13, W - ML - gfx_text_width(&font_ui13, num), FOOT, num, GFX_BLACK);
}

// ---- biblioteca -------------------------------------------------------------

typedef struct { const char *file, *title, *author; int small; } entry_t;

static const entry_t ENTRIES[] = {
    {"amintiri", "Amintiri din copilărie", "Ion Creangă", 0},
    {"luceafarul", "Luceafărul", "Mihai Eminescu", 1},  // versuri: cu o mărime mai mic
    {"moara", "Moara cu noroc", "Ioan Slavici", 0},
    {"belye", "Белые ночи", "Фёдор Достоевский", 0},
};
#define N_ENTRIES (int)(sizeof ENTRIES / sizeof ENTRIES[0])

static void battery_glyph(int x, int y, int level_px)
{
    gfx_rect(&s_g, x, y, 22, 11, GFX_BLACK);
    gfx_fill_rect(&s_g, x + 22, y + 3, 2, 5, GFX_BLACK);
    gfx_fill_rect(&s_g, x + 2, y + 2, level_px, 7, GFX_BLACK);
}

static void library(int sel, const int *pages)
{
    gfx_clear(&s_g, GFX_WHITE);
    gfx_text(&s_g, &font_ui16, ML, 30, "Biblioteca", GFX_BLACK);
    battery_glyph(W - ML - 24, 19, 14);
    gfx_hline(&s_g, ML, 42, TW, GFX_BLACK);

    const int top = 50, step = 76;
    for (int i = 0; i < N_ENTRIES; i++) {
        int y = top + i * step;
        gfx_color_t ink = GFX_BLACK;
        if (i == sel) {
            gfx_fill_rect(&s_g, ML - 6, y, TW + 12, step - 6, GFX_BLACK);
            ink = GFX_WHITE;
        } else if (i + 1 != sel && i + 1 < N_ENTRIES) {
            gfx_hline(&s_g, ML, y + step - 3, TW, GFX_BLACK);
        }
        gfx_text(&s_g, &font_serif18b, ML, y + 29, ENTRIES[i].title, ink);
        char meta[96];
        snprintf(meta, sizeof meta, "%s · %d pagini", ENTRIES[i].author, pages[i]);
        gfx_text(&s_g, &font_ui13, ML, y + 53, meta, ink);
    }
    // "◀ ▶ alege    MENIU deschide", săgețile desenate ca triunghiuri
    const char *t1 = "alege", *t2 = "MENIU  deschide";
    const int r = 4, cy = FOOT - 5;
    int x = (W - (2 * (r + 1) + 5 + 6 + gfx_text_width(&font_ui13, t1) + 20 + gfx_text_width(&font_ui13, t2))) / 2;
    for (int dy = -r; dy <= r; dy++) {
        int len = r + 1 - (dy < 0 ? -dy : dy);
        gfx_hline(&s_g, x + (r + 1 - len), cy + dy, len, GFX_BLACK);
        gfx_hline(&s_g, x + r + 1 + 5, cy + dy, len, GFX_BLACK);
    }
    x = gfx_text(&s_g, &font_ui13, x + 2 * (r + 1) + 5 + 6, FOOT, t1, GFX_BLACK) + 20;
    gfx_text(&s_g, &font_ui13, x, FOOT, t2, GFX_BLACK);
}

// ---- principal --------------------------------------------------------------

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "folosire: mock <outdir> <bookdir>\n"); return 1; }
    const char *out = argv[1], *dir = argc > 2 ? argv[2] : ".";
    gfx_init(&s_g, s_buf, W, H);
    char path[512];
    int pages[N_ENTRIES];

    for (int b = 0; b < N_ENTRIES; b++) {
        snprintf(path, sizeof path, "%s/%s.hy.txt", dir, ENTRIES[b].file);
        book_t book = load(path);
        s_body = ENTRIES[b].small ? &font_serif16 : &font_serif18;
        s_lh = ENTRIES[b].small ? 21 : LH;
        pages[b] = layout(&book, -1);
        printf("%-12s %4d pagini\n", ENTRIES[b].file, pages[b]);
        for (int pg = 1; pg <= SHOW_PAGES && pg <= pages[b]; pg++) {
            gfx_clear(&s_g, GFX_WHITE);
            layout(&book, pg);
            footer(ENTRIES[b].title, pg, pages[b]);
            snprintf(path, sizeof path, "%s/%s-%d.pbm", out, ENTRIES[b].file, pg);
            if (write_pbm(path)) return 1;
        }
    }
    for (int s = 0; s < N_ENTRIES; s++) {
        library(s, pages);
        snprintf(path, sizeof path, "%s/library-%d.pbm", out, s);
        if (write_pbm(path)) return 1;
    }

    // Exact ce desenează firmware-ul de azi: ecranul de pornire, același după o
    // trezire din somn și ecranul de somn. Numele plăcii și textele de pornire
    // sunt ale firmware-ului; cipul, memoria, SD-ul și bateria sunt valori de exemplu.
    bringup_info_t info = {
        .board = "devkit-proto",
        .chip = "ESP32-S3 v0.2, 2 nuclee",
        .memory = "16M flash, 8M PSRAM",
        .sd = "OK, 15,9 GB, 3 fișiere",
        .battery = "3,92 V",
        .wake = "alimentare · nr. 1",
        .last_button = "gata",
        .counter = 0,
        .last_timing = "ultima: încă niciuna",
    };
    screen_bringup_draw(&s_g, &info);
    snprintf(path, sizeof path, "%s/bringup.pbm", out);
    if (write_pbm(path)) return 1;
    // După o trezire, firmware-ul spune ce buton l-a trezit.
    static const char *const names[] = {"PREV", "NEXT", "MENU"};          // în numele fișierelor
    static const char *const labels[] = {"ÎNAPOI", "ÎNAINTE", "MENIU"};   // pe ecran, ca în buttons.c
    char wake[40];
    for (int boot = 2; boot <= 9; boot++) {
        for (int n = 0; n < 3; n++) {
            snprintf(wake, sizeof wake, "%s · nr. %d", labels[n], boot);
            info.wake = wake;
            screen_bringup_draw(&s_g, &info);
            snprintf(path, sizeof path, "%s/bringup-%s-%d.pbm", out, names[n], boot);
            if (write_pbm(path)) return 1;
        }
    }
    screen_sleep_draw(&s_g);
    snprintf(path, sizeof path, "%s/sleep.pbm", out);
    return write_pbm(path);
}
