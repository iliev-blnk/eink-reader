// Text layout: IBV records -> lines -> pages (LAY-1, LAY-2, LAY-3, LAY-4).
//
// A page is computed from a position (paragraph, byte) and fills the text area
// with lines until the next one doesn't fit. Lines are byte ranges of their
// record plus where to draw them, so a page costs a few hundred bytes, and
// drawing re-reads the text from the book's block cache.
//
// Break opportunities: spaces, soft hyphens U+00AD (a '-' is drawn), U+200B,
// after '-', '–', '—' inside a word; U+2028 forces a break. U+00A0 is a space
// that stretches but doesn't break. All widths are in 1/64 px with kerning
// (font.h), so justification has no rounding drift.
//
// Paragrafele și citatele justificate se rup în rânduri tot odată (LAY-5,
// „încadrarea totală” Knuth–Plass): dintre toate felurile de a rupe
// înregistrarea, cel cu penalizarea cea mai mică, unde penalizarea unui rând
// crește cu cubul lățirii sau îngustării spațiilor lui, iar cratimele costă în
// plus. Îi trebuie o zonă de lucru (layout_fit_t, cfg.fit). Restul, și orice
// înregistrare când cfg.fit e NULL, se rupe greedy (first fit): cel mai lung
// rând care încape, apoi următorul. Greedy e și rezerva pentru o înregistrare
// cu peste LAYOUT_FIT_MAX puncte de rupere sau cu un cuvânt mai lat decât
// rândul (tăiat între caractere).
//
// A CONT record (an over-long paragraph the converter split at a space)
// starts a new line with no indent or gap, and the line before it is
// justified, so the join doesn't show unless that line is short. Lines never
// span two records. The split is rare (30 in 70,000 records in the corpus).
//
// Pages are always the ones you get by paginating from the start of the book:
// a page's layout depends only on where it starts, and with chapter_page a
// chapter (a record flagged IBV_PAGE) always starts a page, so paginating from
// there gives the same pages. That is how layout_locate and layout_prev find a page
// without laying out the whole book before it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "ibv.h"

#define LAYOUT_VERSION 3         // part of the layout hash: bump when line breaks can change
#define LAYOUT_MAX_LINES 64
#define LAYOUT_FIT_MAX 2048      // puncte de rupere pe înregistrare pentru încadrarea totală (maximul în corpus e ~2000)

// Zona de lucru pentru încadrarea totală, ~66 KB. Păstrează și rupturile
// ultimei înregistrări rupte, ca paginile unui paragraf lung să nu le refacă.
// Fiecare task care paginează are nevoie de a lui. Se pune pe zero (sau
// layout_fit_reset) înainte de prima folosire și după ce alte fonturi se
// încarcă la aceleași adrese.
typedef struct {
    uint16_t end, next;          // un rând rupt aici se termină la `end`; următorul începe la `next`
    uint16_t gaps;               // spații înainte de `end`
    uint8_t kind;
    int32_t w, sp;               // lățimea și lățimea spațiilor înainte de `end` (w cu cratima)
    int16_t dw, dsp;             // același lucru la `next`, minus w și sp
} layout_fit_bp_t;

typedef struct {
    // Pentru ce înregistrare sunt rupturile.
    bool valid, fitted;          // fitted false: greedy pentru înregistrarea asta
    uint8_t book_id[8];
    uint32_t para;
    const font_t *face[4];
    int32_t avail[2];
    bool hyphenate;
    uint16_t count, lines;       // puncte de rupere, rânduri
    layout_fit_bp_t bp[LAYOUT_FIT_MAX];
    uint64_t total[LAYOUT_FIT_MAX];
    uint16_t prev[LAYOUT_FIT_MAX];
    uint16_t line_end[LAYOUT_FIT_MAX];
    uint8_t fitness[LAYOUT_FIT_MAX];
    uint32_t broken[3];          // înregistrări rupte în prima trecere, a doua, greedy (pentru E6)
} layout_fit_t;

void layout_fit_reset(layout_fit_t *fit);

typedef struct {
    // Body faces by IBV style: [0] regular, [1] bold, [2] italic, [3] bold italic.
    // Only [0] is required; a missing face falls back to regular.
    const font_t *body[4];
    const font_t *heading;       // NULL: body bold (or regular)
    int width, height;           // text area, px
    int indent;                  // PARA first-line indent and QUOTE margins, px
    int line_gap;                // px added to every body line's height
    int para_gap;                // px between paragraphs
    bool justify;
    bool hyphenate;              // break at soft hyphens
    bool chapter_page;           // records flagged IBV_PAGE (chapters) start a new page
    bool keep_lines;             // no widows or orphans (a paragraph's last or first line alone)
    layout_fit_t *fit;           // NULL: rupere greedy peste tot
} layout_cfg_t;

typedef struct {
    uint32_t para, byte;
} layout_pos_t;

enum {
    LINE_HYPHEN = 1,             // draw '-' at the end (soft hyphen break)
    LINE_ORNAMENT = 2,           // a BREAK drawn as "* * *"
    LINE_HEADING = 4,            // drawn with the heading face
};

typedef struct {
    uint32_t para;
    uint32_t start, end;         // text bytes [start, end) of record `para`
    int32_t x;                   // pen start, 1/64 px from the text area's left
    int32_t extra;               // justification: 1/64 px spread over `gaps` spaces (< 0: îngustare)
    uint16_t gaps;
    int16_t baseline;            // px from the text area's top
    uint8_t flags;
} layout_line_t;

typedef struct {
    layout_pos_t start, end;     // end = start of the next page
    bool at_end;                 // the book ends on this page
    int line_count;
    layout_line_t lines[LAYOUT_MAX_LINES];
} layout_page_t;

// Lays out the page that starts at `pos`.
ibv_err_t layout_page(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *page);

// Start of the page that holds `pos` (start <= pos < end), for opening a book
// at a saved position after the layout changed. `scratch` is overwritten.
ibv_err_t layout_locate(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *scratch,
                        layout_pos_t *out);

// Start of the page before the one that starts at `pos` (or holds the text just
// before `pos`). Returns IBV_ERR_RANGE at the start of the book. Without a page
// index this lays out pages from the chapter's start (or the book's, with no
// chapters); `*pages`, if not NULL, gets how many.
ibv_err_t layout_prev(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *scratch,
                      layout_pos_t *out, int *pages);

static inline int layout_pos_cmp(layout_pos_t a, layout_pos_t b)
{
    if (a.para != b.para) return a.para < b.para ? -1 : 1;
    return a.byte < b.byte ? -1 : a.byte > b.byte;
}

// Draws a laid-out page with the text area's top-left at (x0, y0).
ibv_err_t layout_draw(const layout_cfg_t *cfg, ibv_t *book, const layout_page_t *page, gfx_t *g,
                      int x0, int y0);

// Identifies the layout for page caches (spec: IPGC layout_hash): fonts,
// sizes, area, spacing, greedy or total fit, and LAYOUT_VERSION.
uint32_t layout_hash(const layout_cfg_t *cfg);
