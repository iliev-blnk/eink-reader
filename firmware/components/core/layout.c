#include "layout.h"

#include <string.h>

#include "crc32.h"

#define SHY 0x00AD
#define NBSP 0x00A0
#define ZWSP 0x200B
#define LSEP 0x2028

enum { ALIGN_JUSTIFY, ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

static const font_t *body_face(const layout_cfg_t *c, int style)
{
    style &= 3;
    if (c->body[style]) return c->body[style];
    if (style == 3) {
        if (c->body[1]) return c->body[1];
        if (c->body[2]) return c->body[2];
    }
    return c->body[0];
}

static const font_t *heading_face(const layout_cfg_t *c)
{
    return c->heading ? c->heading : body_face(c, IBV_STYLE_BOLD);
}

static int32_t advance_of(const font_t *f, uint32_t cp)
{
    return f->glyphs[font_index_or_fallback(f, cp)].advance;
}

// O înregistrare care se paginează sau se desenează.
typedef struct {
    const layout_cfg_t *cfg;
    const ibv_record_t *rec;
    const font_t *face[4];       // după stil; un titlu își folosește fontul pentru fiecare stil
    int kind, align;
    int32_t left, right;         // marginile, 1/64 px
    int32_t first_indent, wrap_indent;
    int line_height, ascent;
    layout_fit_t *fit;           // rupturile prin încadrare totală ale înregistrării sau NULL: greedy
} para_t;

static void setup(para_t *p, const layout_cfg_t *cfg, const ibv_record_t *rec)
{
    memset(p, 0, sizeof *p);
    p->cfg = cfg;
    p->rec = rec;
    p->kind = rec->kind <= IBV_QUOTE ? rec->kind : IBV_PARA;   // tipurile necunoscute cu text: PARA
    for (int s = 0; s < 4; s++) p->face[s] = p->kind == IBV_HEADING ? heading_face(cfg) : body_face(cfg, s);
    p->line_height = p->face[0]->line_height + cfg->line_gap;
    p->ascent = p->face[0]->ascent;
    int32_t ind = cfg->indent * FONT_UNIT;
    int a = rec->flags & IBV_ALIGN_MASK;
    switch (p->kind) {
    case IBV_HEADING:
        p->align = a ? a : ALIGN_CENTER;
        break;
    case IBV_VERSE:
        p->align = a ? a : ALIGN_LEFT;
        p->wrap_indent = 2 * ind;
        break;
    case IBV_QUOTE:
        p->left = p->right = ind;
        /* fall through */
    default:
        p->align = a ? a : (cfg->justify ? ALIGN_JUSTIFY : ALIGN_LEFT);
        if (p->kind == IBV_PARA && !(rec->flags & (IBV_NO_INDENT | IBV_CONT))
            && (p->align == ALIGN_JUSTIFY || p->align == ALIGN_LEFT))
            p->first_indent = ind;
    }
}

typedef struct {
    uint32_t end, next;          // conținutul se termină la `end`; rândul următor începe la `next`
    int32_t width;               // lățimea naturală, 1/64 px, cu cratima dacă are
    uint16_t gaps;               // spațiile care se pot lăți în [start, end)
    bool hyphen;
    bool hard;                   // se termină la U+2028 sau la sfârșitul înregistrării: nejustificat
} brk_t;

// Greedy (first fit): cel mai lung rând de la `start` care încape în `avail`.
static void break_line(const para_t *p, uint32_t start, int32_t avail, brk_t *out)
{
    const char *text = p->rec->text, *end = text + p->rec->text_len, *s = text + start;
    ibv_styles_t st;
    ibv_styles_init(&st, p->rec);
    int32_t w = 0;
    uint16_t gaps = 0;
    int32_t prev = -1;
    const font_t *prev_face = NULL;
    brk_t cand = {0};
    bool have = false, any = false, in_word = false;

    while (s < end) {
        uint32_t at = (uint32_t)(s - text);
        const char *ns = s;
        uint32_t cp = utf8_next_n(&ns, end);
        uint32_t nx = (uint32_t)(ns - text);
        const font_t *f = p->face[ibv_style_at(&st, at) & 3];
        switch (cp) {
        case ' ':
            cand = (brk_t){at, nx, w, gaps, false, false};
            have = true;
            /* fall through */
        case NBSP:
            w += advance_of(f, ' ');
            gaps++;
            prev = -1;
            in_word = false;
            break;
        case LSEP:
            *out = (brk_t){at, nx, w, gaps, false, true};
            return;
        case SHY:
            if (p->cfg->hyphenate) {
                uint32_t h = font_index_or_fallback(f, '-');
                int32_t hw = w + f->glyphs[h].advance + (prev >= 0 && f == prev_face ? font_kern(f, (uint32_t)prev, h) : 0);
                if (hw <= avail) {
                    cand = (brk_t){at, nx, hw, gaps, true, false};
                    have = true;
                }
            }
            break;                                  // invizibil: kerningul trece peste el
        case ZWSP:
            cand = (brk_t){at, nx, w, gaps, false, false};
            have = true;
            prev = -1;
            break;
        default: {
            uint32_t gi = font_index_or_fallback(f, cp);
            int32_t add = f->glyphs[gi].advance;
            if (prev >= 0 && f == prev_face) add += font_kern(f, (uint32_t)prev, gi);
            if (any && w + add > avail) {
                if (!have) cand = (brk_t){at, at, w, gaps, false, false};   // taie cuvântul
                *out = cand;
                return;
            }
            w += add;
            prev = (int32_t)gi;
            prev_face = f;
            any = true;
            // O liniuță în interiorul unui cuvânt („nord-vest”, „1914–1918”) poate încheia un rând.
            if ((cp == '-' || cp == 0x2013 || cp == 0x2014) && in_word && ns < end && *ns != ' ') {
                cand = (brk_t){nx, nx, w, gaps, false, false};
                have = true;
            }
            in_word = true;
        }
        }
        s = ns;
    }
    *out = (brk_t){p->rec->text_len, p->rec->text_len, w, gaps, false, true};
}

static int32_t line_left(const para_t *p, bool first)
{
    return p->left + (first ? p->first_indent : p->wrap_indent);
}

static int32_t line_avail(const para_t *p, bool first)
{
    return p->cfg->width * FONT_UNIT - line_left(p, first) - p->right;
}

// Încadrarea totală (Knuth și Plass, „Breaking paragraphs into lines”, 1981),
// în forma din TeX, redusă la ce îi trebuie unui cititor: fiecare spațiu e un
// „lipici” care se poate lăți cu de două ori lățimea lui sau îngusta cu o
// treime. Proasta așezare (badness) a unui rând e 100 r^3 pentru raportul r cu
// care se lățește sau se îngustează, iar penalizarea lui (demerits) e
// (LINE + badness)^2 plus pătratul penalizării punctului de rupere și
// adaosurile de mai jos. Câștigă rupturile cu suma cea mai mică pe toată
// înregistrarea. O cratimă moale sau o liniuță în cuvânt costă HYPHEN; două
// rânduri cu cratimă la rând, o cratimă pe penultimul rând și un rând larg
// lângă unul strâns (clasele de potrivire) costă în plus. Ultimul rând și
// rândul care se termină la U+2028 rămân la lățimea lor naturală, deci
// trebuie doar să încapă.
//
// Căutarea e o singură trecere peste punctele de rupere, cu cel mai bun drum
// până la fiecare (algoritmul clasic păstrează câte unul pe clasă de
// potrivire; aici ajunge unul și ocupă de 4 ori mai puțin). Un rând începe la
// începutul înregistrării sau după o rupere și se măsoară din sume prefix:
// punctul de rupere ține lățimea până unde se termină rândul care se rupe
// acolo și lățimea de unde începe următorul, cu kerningul tăiat de rupere.
// Prima trecere ia doar rânduri cu badness până la TOLERANCE; dacă nu iese
// nicio împărțire, a doua ia orice rând care încape (badness nelimitat, ca să
// deosebească un rând larg de unul și mai larg), iar dacă un cuvânt singur nu
// încape, înregistrarea se rupe lacom (greedy).
//
// Constantele sunt alese pe corpus cu `sim stats` (experimentul E6, la 216 px):
// față de greedy, mai puține cratime, de 2–10 ori mai puține cratime
// consecutive și spații cu ~30 % mai puțin lățite.
//
// Rupturile unei înregistrări depind doar de textul ei și de cele două lățimi
// de rând (primul rând, celelalte), deci o pagină care începe în mijlocul unui
// paragraf primește aceleași rupturi ca atunci când paragraful a început pe
// pagina dinainte.
enum { BP_START, BP_SPACE, BP_SHY, BP_DASH, BP_ZWSP, BP_HARD };

// Se pot suprascrie cu -D pentru experimente.
#ifndef STRETCH_NUM
#define STRETCH_NUM 2            // un spațiu se lățește cu 2 lățimi ale lui...
#endif
#ifndef STRETCH_DEN
#define STRETCH_DEN 1
#endif
#define SHRINK_NUM 1             // ...și se îngustează cu 1/3
#define SHRINK_DEN 3
#ifndef TOLERANCE
#define TOLERANCE 400            // prima trecere: r până la 1,59
#endif
#define BAD_MAX 10000000         // fără plafonul de 10000 din TeX, ca a doua trecere să
                                 // deosebească un rând larg de unul și mai larg
#define LINE 10
#ifndef HYPHEN
#define HYPHEN 100
#endif
#ifndef DOUBLE_HYPHEN
#define DOUBLE_HYPHEN 100000
#endif
#define FINAL_HYPHEN 5000
#define ADJ_FITNESS 10000

void layout_fit_reset(layout_fit_t *fit)
{
    fit->valid = false;
}

// Adună punctele de rupere ale înregistrării, așa cum le vede break_line.
// False dacă sunt mai multe de LAYOUT_FIT_MAX.
static bool fit_scan(layout_fit_t *ft, const para_t *p)
{
    const char *text = p->rec->text, *end = text + p->rec->text_len, *s = text;
    ibv_styles_t st;
    ibv_styles_init(&st, p->rec);
    layout_fit_bp_t *bp = ft->bp;
    int n = 0;
    bp[n++] = (layout_fit_bp_t){.kind = BP_START};
    int32_t w = 0, sp = 0;
    uint16_t gaps = 0;
    int32_t prev = -1;
    const font_t *prev_face = NULL;
    bool in_word = false;
    int pend = -1;               // rupere în cuvânt: kerningul glifei următoare nu e pe rândul următor

    while (s < end) {
        if (n >= LAYOUT_FIT_MAX - 1) return false;                // un loc rămâne pentru sfârșit
        uint16_t at = (uint16_t)(s - text);
        const char *ns = s;
        uint32_t cp = utf8_next_n(&ns, end);
        uint16_t nx = (uint16_t)(ns - text);
        const font_t *f = p->face[ibv_style_at(&st, at) & 3];
        switch (cp) {
        case ' ': {
            int16_t a = (int16_t)advance_of(f, ' ');
            bp[n++] = (layout_fit_bp_t){at, nx, gaps, BP_SPACE, w, sp, a, a};
        }
            /* fall through */
        case NBSP: {
            int32_t a = advance_of(f, ' ');
            w += a;
            sp += a;
            gaps++;
            prev = -1;
            in_word = false;
            pend = -1;
            break;
        }
        case LSEP:
            bp[n++] = (layout_fit_bp_t){at, nx, gaps, BP_HARD, w, sp, 0, 0};
            prev = -1;
            in_word = false;
            pend = -1;
            break;
        case SHY:
            if (p->cfg->hyphenate) {
                uint32_t h = font_index_or_fallback(f, '-');
                int32_t hw = w + f->glyphs[h].advance + (prev >= 0 && f == prev_face ? font_kern(f, (uint32_t)prev, h) : 0);
                bp[n++] = (layout_fit_bp_t){at, nx, gaps, BP_SHY, hw, sp, (int16_t)(w - hw), 0};
                pend = n - 1;
            }
            break;
        case ZWSP:
            bp[n++] = (layout_fit_bp_t){at, nx, gaps, BP_ZWSP, w, sp, 0, 0};
            prev = -1;
            pend = -1;
            break;
        default: {
            uint32_t gi = font_index_or_fallback(f, cp);
            int32_t k = prev >= 0 && f == prev_face ? font_kern(f, (uint32_t)prev, gi) : 0;
            if (pend >= 0) bp[pend].dw = (int16_t)(bp[pend].dw + k);
            pend = -1;
            w += f->glyphs[gi].advance + k;
            prev = (int32_t)gi;
            prev_face = f;
            if ((cp == '-' || cp == 0x2013 || cp == 0x2014) && in_word && ns < end && *ns != ' ') {
                bp[n++] = (layout_fit_bp_t){nx, nx, gaps, BP_DASH, w, sp, 0, 0};
                pend = n - 1;
            }
            in_word = true;
        }
        }
        s = ns;
    }
    uint16_t len = (uint16_t)p->rec->text_len;
    // Un U+2028 chiar la sfârșit încheie deja ultimul rând.
    if (!(bp[n - 1].kind == BP_HARD && bp[n - 1].next == len))
        bp[n++] = (layout_fit_bp_t){len, len, gaps, BP_HARD, w, sp, 0, 0};
    ft->count = (uint16_t)n;
    return true;
}

static int32_t badness(int32_t over, int32_t room)
{
    if (over <= 0) return 0;
    if (room <= 0) return BAD_MAX;           // rând scurt fără spații: doar în a doua trecere
    int64_t b = 100 * (int64_t)over * over * over / ((int64_t)room * room * room);
    return b > BAD_MAX ? BAD_MAX : (int32_t)b;
}

static bool hyphenish(int kind)
{
    return kind == BP_SHY || kind == BP_DASH;
}

// Cele mai bune rupturi cu badness-ul fiecărui rând până la `tol`; false dacă nu există.
//
// E algoritmul din TeX (tex.web, §813–§890), verificat paragraf cu paragraf
// față de LuaTeX cu tools/texref. Penalizarea unui rând depinde și de clasa de
// potrivire a rândului dinainte (ADJ_FITNESS), deci la fiecare punct de rupere
// se ține cea mai bună împărțire pentru fiecare clasă a rândului care se
// termină acolo, nu una singură. O clasă cu mai mult de ADJ_FITNESS peste cea
// mai bună nu poate câștiga și nu se ține. Două lucruri rămân altfel decât în
// TeX: badness-ul e calculat exact și fără plafon (vezi BAD_MAX), iar
// îngustarea unui rând e o treime din suma spațiilor lui, rotunjită o dată.
static bool fit_solve(layout_fit_t *ft, int tol)
{
    _Static_assert(LAYOUT_FIT_MAX <= 1 << 12, "prev ține punctul de rupere pe 12 biți");
    const layout_fit_bp_t *bp = ft->bp;
    const int n = ft->count;
    // Începutul paragrafului ține loc de rând potrivit (clasa 2).
    ft->total[0] = 0;
    for (int c = 0; c < 4; c++) ft->over[0][c] = c == 2 ? 0 : LAYOUT_FIT_NONE;
    for (int j = 1; j < n; j++) {
        const layout_fit_bp_t *b = &bp[j];
        const bool fill = b->kind == BP_HARD;
        uint64_t best[4] = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
        uint16_t from[4] = {0, 0, 0, 0};
        for (int i = j - 1; i >= 0; i--) {
            const layout_fit_bp_t *a = &bp[i];
            int32_t width = b->w - (a->w + a->dw), space = b->sp - (a->sp + a->dsp);
            int32_t avail = ft->avail[i > 0];
            int32_t shrink = space * SHRINK_NUM / SHRINK_DEN;
            if (width - shrink > avail) break;                    // și mai lung de mai departe în urmă
            // Un rând gol e admis doar între două ruperi forțate (două U+2028 la rând).
            if ((b->end > a->next || (fill && b->end == a->next)) && ft->total[i] != UINT64_MAX) {
                int32_t bad;
                int fit;
                if (width > avail) {
                    bad = badness(width - avail, shrink);
                    fit = bad > 12 ? 3 : 2;
                } else if (fill) {                                // ultimul rând se umple cu spațiu gol
                    bad = 0;
                    fit = 2;
                } else {
                    bad = badness(avail - width, space * STRETCH_NUM / STRETCH_DEN);
                    fit = bad > 99 ? 0 : bad > 12 ? 1 : 2;
                }
                if (bad <= tol) {
                    uint64_t d = ft->total[i] + (uint64_t)(LINE + bad) * (uint64_t)(LINE + bad);
                    if (hyphenish(b->kind)) d += HYPHEN * HYPHEN;
                    if (i > 0 && hyphenish(a->kind)) {
                        if (hyphenish(b->kind)) d += DOUBLE_HYPHEN;
                        if (j == n - 1) d += FINAL_HYPHEN;
                    }
                    // La egalitate rămâne ruptura cea mai târzie și, la aceeași ruptură, clasa cea mai mare.
                    for (int c = 3; c >= 0; c--) {
                        if (ft->over[i][c] == LAYOUT_FIT_NONE) continue;
                        uint64_t t = d + ft->over[i][c] + (fit > c + 1 || c > fit + 1 ? ADJ_FITNESS : 0);
                        if (t < best[fit]) {
                            best[fit] = t;
                            from[fit] = (uint16_t)(i | c << 12);
                        }
                    }
                }
            }
            if (a->kind == BP_HARD) break;                        // niciun rând nu trece peste U+2028
        }
        uint64_t least = best[0];
        for (int c = 1; c < 4; c++)
            if (best[c] < least) least = best[c];
        ft->total[j] = least;
        for (int c = 0; c < 4; c++) {
            bool keep = best[c] != UINT64_MAX && best[c] - least <= ADJ_FITNESS;
            ft->over[j][c] = keep ? (uint16_t)(best[c] - least) : LAYOUT_FIT_NONE;
            ft->prev[j][c] = from[c];
        }
        if (fill && least == UINT64_MAX) return false;             // nimic nu trece de o rupere forțată
    }
    // Împărțirea cu penalizarea cea mai mică; la egalitate, clasa cea mai mică a ultimului rând.
    int last = 0;
    while (ft->over[n - 1][last] != 0) last++;
    int lines = 0;
    for (int j = n - 1, c = last; j > 0;) {
        uint16_t f = ft->prev[j][c];
        j = f & 0xFFF, c = f >> 12;
        lines++;
    }
    ft->lines = (uint16_t)lines;
    for (int j = n - 1, c = last; j > 0;) {
        ft->line_end[--lines] = (uint16_t)j;
        uint16_t f = ft->prev[j][c];
        j = f & 0xFFF, c = f >> 12;
    }
    return true;
}

// Pune în p->fit rupturile prin încadrare totală ale înregistrării și le
// calculează dacă zona de lucru nu le are deja. Lasă NULL pentru greedy.
static void fit_prepare(para_t *p, const ibv_t *book, uint32_t para)
{
    layout_fit_t *ft = p->cfg->fit;
    if (!ft || p->align != ALIGN_JUSTIFY || (p->kind != IBV_PARA && p->kind != IBV_QUOTE)) return;
    int32_t avail[2] = {line_avail(p, true), line_avail(p, false)};
    if (!(ft->valid && ft->para == para && !memcmp(ft->book_id, book->book_id, sizeof ft->book_id)
          && !memcmp(ft->face, p->face, sizeof ft->face) && !memcmp(ft->avail, avail, sizeof avail)
          && ft->hyphenate == p->cfg->hyphenate)) {
        ft->valid = true;
        ft->para = para;
        memcpy(ft->book_id, book->book_id, sizeof ft->book_id);
        memcpy(ft->face, p->face, sizeof ft->face);
        memcpy(ft->avail, avail, sizeof avail);
        ft->hyphenate = p->cfg->hyphenate;
        int pass = 2;
        if (p->rec->text_len > 0 && p->rec->text_len <= UINT16_MAX && fit_scan(ft, p))
            pass = fit_solve(ft, TOLERANCE) ? 0 : fit_solve(ft, BAD_MAX) ? 1 : 2;
        ft->fitted = pass < 2;
        ft->broken[pass]++;
    }
    if (ft->fitted) p->fit = ft;
}

// Rândul din încadrarea totală care începe la `start`; false dacă acolo nu începe niciun rând.
static bool fit_line(const layout_fit_t *ft, uint32_t start, brk_t *out)
{
    int lo = 0, hi = ft->lines - 1;
    while (lo <= hi) {
        int k = (lo + hi) / 2;
        int i = k ? ft->line_end[k - 1] : 0;
        uint32_t at = ft->bp[i].next;
        if (at < start) lo = k + 1;
        else if (at > start) hi = k - 1;
        else {
            const layout_fit_bp_t *a = &ft->bp[i], *b = &ft->bp[ft->line_end[k]];
            *out = (brk_t){b->end, b->next, b->w - (a->w + a->dw), (uint16_t)(b->gaps - a->gaps - (a->kind == BP_SPACE)),
                           b->kind == BP_SHY, b->kind == BP_HARD};
            return true;
        }
    }
    return false;
}

// Rândul de la `start`: din încadrarea totală dacă înregistrarea o are, altfel greedy.
static void next_line(const para_t *p, uint32_t start, int32_t avail, brk_t *out)
{
    if (!p->fit || !fit_line(p->fit, start, out)) break_line(p, start, avail, out);
}

// Pagina e plină înainte de rândul de la `*byte` din înregistrarea `para`. Mută
// rânduri pe pagina următoare ca ea să nu înceapă cu ultimul rând al unui
// paragraf singur (rând văduv) sau să nu se termine cu primul lui rând singur
// (rând orfan). Pagina nu se golește niciodată: un paragraf care începe sus pe
// pagină rămâne cum e.
static void keep_lines(const para_t *p, layout_page_t *page, uint32_t para, uint32_t *byte)
{
    int first = page->line_count;
    while (first > 0 && page->lines[first - 1].para == para) first--;
    const int here = page->line_count - first;                  // rândurile lui `para` de pe pagina asta
    const bool started = here > 0 && page->lines[first].start == 0;
    if (here == 0) return;
    if (here == 1 && started) {                                  // rând orfan: se mută
        if (first > 0) page->line_count = first, *byte = 0;
        return;
    }
    brk_t k;
    next_line(p, *byte, line_avail(p, false), &k);
    if (k.next < p->rec->text_len) return;                       // mai rămân două rânduri sau mai multe: e bine
    if (here == 2 && started) {                                  // mutarea unuia l-ar lăsa orfan pe celălalt
        if (first > 0) page->line_count = first, *byte = 0;
        return;
    }
    if (here >= 2) *byte = page->lines[--page->line_count].start;   // rând văduv: ia un rând cu el
}

ibv_err_t layout_page(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *page)
{
    memset(page, 0, sizeof(*page) - sizeof(page->lines));
    page->start = pos;
    const font_t *body = body_face(cfg, 0);
    const int body_lh = body->line_height + cfg->line_gap;
    int y = 0;
    uint32_t para = pos.para, byte = pos.byte;
    ibv_err_t err;

    while (para < book->paragraph_count) {
        ibv_record_t rec;
        if ((err = ibv_record(book, para, &rec)) != IBV_OK) return err;
        const bool fresh = byte == 0, top = page->line_count == 0;

        if (rec.kind == IBV_BREAK || (rec.kind > IBV_QUOTE && rec.text_len == 0)) {
            if (rec.kind == IBV_BREAK && !top) {     // scos sus pe pagină
                if (y + body_lh > cfg->height || page->line_count == LAYOUT_MAX_LINES) goto full;
                if (rec.flags & IBV_ORNAMENT) {
                    int32_t w = font_text_width(body, "* * *");
                    page->lines[page->line_count++] = (layout_line_t){
                        .para = para, .x = (cfg->width * FONT_UNIT - w) / 2,
                        .baseline = (int16_t)(y + body->ascent), .flags = LINE_ORNAMENT};
                }
                y += body_lh;
            }
            para++;
            byte = 0;
            continue;
        }

        para_t p;
        setup(&p, cfg, &rec);
        fit_prepare(&p, book, para);
        int below = 0;
        const bool chapter = cfg->chapter_page && (rec.flags & IBV_PAGE) && fresh;
        if (chapter && !top) goto full;
        if (p.kind == IBV_HEADING && fresh) {
            int above = top ? (chapter ? 2 * body_lh : 0) : body_lh;
            below = body_lh / 2;
            // Titlul se mută pe pagina următoare dacă nu încape cu două rânduri după el.
            int n = 0;
            for (uint32_t b = 0; b < rec.text_len; n++) {
                brk_t k;
                break_line(&p, b, line_avail(&p, true), &k);
                b = k.next;
            }
            if (!top && y + above + n * p.line_height + below + 2 * body_lh > cfg->height) goto full;
            y += above;
        } else if (fresh && !top && !(rec.flags & IBV_CONT) && p.kind != IBV_VERSE) {
            y += cfg->para_gap;
        }

        bool first = fresh && !(rec.flags & IBV_CONT);
        bool wrapped = !fresh;
        for (;;) {
            if ((y + p.line_height > cfg->height && page->line_count > 0) || page->line_count == LAYOUT_MAX_LINES) {
                if (cfg->keep_lines) keep_lines(&p, page, para, &byte);
                goto full;
            }
            int32_t left = line_left(&p, first && !wrapped), avail = line_avail(&p, first && !wrapped);
            if (p.kind == IBV_VERSE && !wrapped) left = p.left, avail = cfg->width * FONT_UNIT - p.left - p.right;
            brk_t k;
            next_line(&p, byte, avail, &k);
            bool done = k.next >= rec.text_len;
            layout_line_t *ln = &page->lines[page->line_count++];
            *ln = (layout_line_t){.para = para, .start = byte, .end = k.end,
                                  .baseline = (int16_t)(y + p.ascent),
                                  .flags = (uint8_t)((k.hyphen ? LINE_HYPHEN : 0) | (p.kind == IBV_HEADING ? LINE_HEADING : 0))};
            int32_t slack = avail - k.width;
            bool justify = p.align == ALIGN_JUSTIFY && k.gaps > 0 && slack != 0;
            if (justify && k.hard && slack > 0) {
                // Ultimul rând al unui paragraf nu e lățit până la margine, decât dacă
                // convertorul a împărțit paragraful și el continuă într-o înregistrare
                // CONT. De îngustat se îngustează ca oricare altul (slack < 0).
                justify = false;
                if (done && k.end == rec.text_len && para + 1 < book->paragraph_count) {
                    ibv_record_t nxt;
                    if ((err = ibv_record(book, para + 1, &nxt)) != IBV_OK) return err;
                    justify = (nxt.flags & IBV_CONT) != 0;
                    if ((err = ibv_record(book, para, &rec)) != IBV_OK) return err;   // `rec` rămâne la zi
                }
            }
            ln->x = left;
            if (justify) ln->extra = slack, ln->gaps = k.gaps;
            else if (p.align == ALIGN_CENTER) ln->x = left + slack / 2;
            else if (p.align == ALIGN_RIGHT) ln->x = left + slack;
            ln->gaps = justify ? k.gaps : 0;
            y += p.line_height;
            byte = k.next;
            first = false;
            wrapped = !k.hard;       // după U+2028 rândul următor începe la margine
            if (done) break;
        }
        y += below;
        para++;
        byte = 0;
    }
    page->at_end = true;
    page->end = (layout_pos_t){book->paragraph_count, 0};
    return IBV_OK;
full:
    // Un titlu de jos pleacă împreună cu ce urmează după el, decât dacă e tot ce are pagina.
    while (byte == 0 && page->line_count > 0 && (page->lines[page->line_count - 1].flags & LINE_HEADING)) {
        uint32_t h = page->lines[page->line_count - 1].para;
        int n = page->line_count;
        while (n > 0 && page->lines[n - 1].para == h) n--;
        if (n == 0) break;
        page->line_count = n;
        para = h;
    }
    // Separatoarele de sus de pe pagină se scot, deci pagina următoare începe
    // după ele: începuturile paginilor sunt atunci aceleași oricum s-a ajuns în carte.
    while (byte == 0 && para < book->paragraph_count) {
        ibv_record_t rec;
        if ((err = ibv_record(book, para, &rec)) != IBV_OK) return err;
        if (!(rec.kind == IBV_BREAK || (rec.kind > IBV_QUOTE && rec.text_len == 0))) break;
        para++;
    }
    page->at_end = para >= book->paragraph_count;
    page->end = (layout_pos_t){para, page->at_end ? 0 : byte};
    return IBV_OK;
}

// Ultima poziție la sau înainte de `pos` (strict înainte, dacă `strict`) unde
// sigur începe o pagină: începutul cărții sau un capitol, cu chapter_page.
// Capitolele se găsesc prin cuprins, care e în RAM; un capitol care lipsește
// din cuprins doar face căutarea să înceapă mai devreme.
static ibv_err_t sync_point(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, bool strict, layout_pos_t *out)
{
    *out = (layout_pos_t){0, 0};
    if (!cfg->chapter_page) return IBV_OK;
    for (int32_t i = ibv_toc_find(book, pos.para); i >= 0; i--) {
        ibv_toc_t t;
        if (!ibv_toc(book, (uint32_t)i, &t)) break;
        layout_pos_t at = {t.para, 0};
        int c = layout_pos_cmp(at, pos);
        if (c > 0 || (strict && c == 0)) continue;
        ibv_record_t rec;
        ibv_err_t err = ibv_record(book, t.para, &rec);
        if (err) return err;
        // Cuprinsul poate arăta spre orice înregistrare; contează doar una cu indicatorul.
        if (rec.flags & IBV_PAGE) {
            *out = at;
            return IBV_OK;
        }
    }
    return IBV_OK;
}

ibv_err_t layout_locate(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *scratch,
                        layout_pos_t *out)
{
    layout_pos_t at;
    ibv_err_t err = sync_point(cfg, book, pos, false, &at);
    for (; !err; at = scratch->end) {
        if ((err = layout_page(cfg, book, at, scratch)) != IBV_OK) break;
        if (scratch->at_end || layout_pos_cmp(pos, scratch->end) < 0) {
            *out = at;
            break;
        }
    }
    return err;
}

ibv_err_t layout_prev(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *scratch,
                      layout_pos_t *out, int *pages)
{
    if (pages) *pages = 0;
    if (pos.para == 0 && pos.byte == 0) return IBV_ERR_RANGE;
    layout_pos_t at;
    ibv_err_t err = sync_point(cfg, book, pos, true, &at);
    for (; !err; at = scratch->end) {
        if ((err = layout_page(cfg, book, at, scratch)) != IBV_OK) break;
        if (pages) ++*pages;
        if (scratch->at_end || layout_pos_cmp(pos, scratch->end) <= 0) {
            *out = at;
            break;
        }
    }
    return err;
}

static void draw_line(gfx_t *g, const para_t *p, const layout_line_t *ln, int x0, int y0)
{
    const char *text = p->rec->text, *s = text + ln->start, *e = text + ln->end;
    ibv_styles_t st;
    ibv_styles_init(&st, p->rec);
    int32_t pen = x0 * FONT_UNIT + ln->x;
    int y = y0 + ln->baseline;
    int32_t prev = -1;
    const font_t *prev_face = p->face[0], *f = p->face[0];
    uint32_t k = 0;
    while (s < e) {
        uint32_t at = (uint32_t)(s - text);
        uint32_t cp = utf8_next_n(&s, e);
        f = p->face[ibv_style_at(&st, at) & 3];
        if (cp == ' ' || cp == NBSP) {
            pen += advance_of(f, ' ');
            if (ln->gaps) {
                pen += (int32_t)((int64_t)ln->extra * (k + 1) / ln->gaps - (int64_t)ln->extra * k / ln->gaps);
                k++;
            }
            prev = -1;
            continue;
        }
        if (cp == SHY) continue;
        if (cp == ZWSP || cp == LSEP) { prev = -1; continue; }
        uint32_t gi = font_index_or_fallback(f, cp);
        if (prev >= 0 && f == prev_face) pen += font_kern(f, (uint32_t)prev, gi);
        gfx_glyph(g, f, gi, (pen + FONT_UNIT / 2) / FONT_UNIT, y, GFX_BLACK);
        pen += f->glyphs[gi].advance;
        prev = (int32_t)gi;
        prev_face = f;
    }
    if (ln->flags & LINE_HYPHEN) {
        uint32_t gi = font_index_or_fallback(f, '-');
        if (prev >= 0 && f == prev_face) pen += font_kern(f, (uint32_t)prev, gi);
        gfx_glyph(g, f, gi, (pen + FONT_UNIT / 2) / FONT_UNIT, y, GFX_BLACK);
    }
}

ibv_err_t layout_draw(const layout_cfg_t *cfg, ibv_t *book, const layout_page_t *page, gfx_t *g, int x0, int y0)
{
    for (int i = 0; i < page->line_count; i++) {
        const layout_line_t *ln = &page->lines[i];
        if (ln->flags & LINE_ORNAMENT) {
            const font_t *f = body_face(cfg, 0);
            gfx_text(g, f, x0 + ln->x / FONT_UNIT, y0 + ln->baseline, "* * *", GFX_BLACK);
            continue;
        }
        ibv_record_t rec;
        ibv_err_t err = ibv_record(book, ln->para, &rec);
        if (err) return err;
        para_t p;
        setup(&p, cfg, &rec);
        draw_line(g, &p, ln, x0, y0);
    }
    return IBV_OK;
}

static uint32_t hash_font(uint32_t h, const font_t *f)
{
    if (!f) return crc32_update(h, "-", 1);
    h = crc32_update(h, f->name, strlen(f->name));
    uint16_t m[5] = {f->style, f->pixel_size, f->line_height, f->ascent, f->descent};
    h = crc32_update(h, m, sizeof m);
    h = crc32_update(h, f->glyphs, sizeof(font_glyph_t) * f->glyph_count);
    return crc32_update(h, f->kerns, sizeof(font_kern_t) * f->kern_count);
}

uint32_t layout_hash(const layout_cfg_t *cfg)
{
    int32_t v[11] = {LAYOUT_VERSION, cfg->width, cfg->height, cfg->indent, cfg->line_gap, cfg->para_gap,
                     cfg->justify, cfg->hyphenate, cfg->chapter_page, cfg->keep_lines, cfg->fit != NULL};
    uint32_t h = crc32_update(0, v, sizeof v);
    for (int i = 0; i < 4; i++) h = hash_font(h, cfg->body[i]);
    return hash_font(h, cfg->heading);
}
