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

// One record being laid out or drawn.
typedef struct {
    const layout_cfg_t *cfg;
    const ibv_record_t *rec;
    const font_t *face[4];       // by style; a heading uses its face for every style
    int kind, align;
    int32_t left, right;         // margins, 1/64 px
    int32_t first_indent, wrap_indent;
    int line_height, ascent;
} para_t;

static void setup(para_t *p, const layout_cfg_t *cfg, const ibv_record_t *rec)
{
    memset(p, 0, sizeof *p);
    p->cfg = cfg;
    p->rec = rec;
    p->kind = rec->kind <= IBV_QUOTE ? rec->kind : IBV_PARA;   // unknown kinds with text: PARA
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
    uint32_t end, next;          // content ends at `end`; the next line starts at `next`
    int32_t width;               // natural width, 1/64 px, with the hyphen if any
    uint16_t gaps;               // stretchable spaces in [start, end)
    bool hyphen;
    bool hard;                   // ends at U+2028 or the end of the record: not justified
} brk_t;

// Greedy first fit: the longest line from `start` that fits in `avail`.
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
            break;                                  // invisible: kerning carries across it
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
                if (!have) cand = (brk_t){at, at, w, gaps, false, false};   // cut the word
                *out = cand;
                return;
            }
            w += add;
            prev = (int32_t)gi;
            prev_face = f;
            any = true;
            // A dash inside a word ("north-west", "1914–1918") may end a line.
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

// The page is full before the line at `*byte` of record `para`. Moves lines to
// the next page so that it doesn't start with a paragraph's last line alone
// (a widow) or end with its first line alone (an orphan). The page never
// empties: a paragraph that starts at the top of the page stays as it is.
static void keep_lines(const para_t *p, layout_page_t *page, uint32_t para, uint32_t *byte)
{
    int first = page->line_count;
    while (first > 0 && page->lines[first - 1].para == para) first--;
    const int here = page->line_count - first;                  // lines of `para` on this page
    const bool started = here > 0 && page->lines[first].start == 0;
    if (here == 0) return;
    if (here == 1 && started) {                                  // orphan: move it over
        if (first > 0) page->line_count = first, *byte = 0;
        return;
    }
    brk_t k;
    break_line(p, *byte, line_avail(p, false), &k);
    if (k.next < p->rec->text_len) return;                       // two or more lines left: fine
    if (here == 2 && started) {                                  // moving one would orphan the other
        if (first > 0) page->line_count = first, *byte = 0;
        return;
    }
    if (here >= 2) *byte = page->lines[--page->line_count].start;   // widow: take one line with it
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
            if (rec.kind == IBV_BREAK && !top) {     // dropped at the top of a page
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
        int below = 0;
        const bool chapter = cfg->chapter_page && (rec.flags & IBV_PAGE) && fresh;
        if (chapter && !top) goto full;
        if (p.kind == IBV_HEADING && fresh) {
            int above = top ? (chapter ? 2 * body_lh : 0) : body_lh;
            below = body_lh / 2;
            // The heading moves to the next page unless it fits with two lines after it.
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
            break_line(&p, byte, avail, &k);
            bool done = k.next >= rec.text_len;
            layout_line_t *ln = &page->lines[page->line_count++];
            *ln = (layout_line_t){.para = para, .start = byte, .end = k.end,
                                  .baseline = (int16_t)(y + p.ascent),
                                  .flags = (uint8_t)((k.hyphen ? LINE_HYPHEN : 0) | (p.kind == IBV_HEADING ? LINE_HEADING : 0))};
            int32_t slack = avail - k.width;
            bool justify = p.align == ALIGN_JUSTIFY && k.gaps > 0 && slack > 0;
            if (justify && k.hard) {
                // The last line of a paragraph isn't justified, unless the
                // converter split the paragraph and it goes on in a CONT record.
                justify = false;
                if (done && k.end == rec.text_len && para + 1 < book->paragraph_count) {
                    ibv_record_t nxt;
                    if ((err = ibv_record(book, para + 1, &nxt)) != IBV_OK) return err;
                    justify = (nxt.flags & IBV_CONT) != 0;
                    if ((err = ibv_record(book, para, &rec)) != IBV_OK) return err;   // keep `rec` current
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
            wrapped = !k.hard;       // after U+2028 the next line starts at the margin
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
    // A heading at the bottom goes with what follows it, unless it's all the page has.
    while (byte == 0 && page->line_count > 0 && (page->lines[page->line_count - 1].flags & LINE_HEADING)) {
        uint32_t h = page->lines[page->line_count - 1].para;
        int n = page->line_count;
        while (n > 0 && page->lines[n - 1].para == h) n--;
        if (n == 0) break;
        page->line_count = n;
        para = h;
    }
    // Breaks at the top of a page are dropped, so the next page starts after
    // them: page starts are then the same however the book was reached.
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

// The latest position at or before `pos` (strictly before, if `strict`) where a
// page is sure to start: the book's start, or a chapter when chapter_page.
// Chapters are found through the TOC, which is in RAM; a chapter missing from
// the TOC only makes the search start earlier.
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
        // The TOC may point at any record; only a flagged one counts.
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
    int32_t v[10] = {LAYOUT_VERSION, cfg->width, cfg->height, cfg->indent, cfg->line_gap, cfg->para_gap,
                     cfg->justify, cfg->hyphenate, cfg->chapter_page, cfg->keep_lines};
    uint32_t h = crc32_update(0, v, sizeof v);
    for (int i = 0; i < 4; i++) h = hash_font(h, cfg->body[i]);
    return hash_font(h, cfg->heading);
}
