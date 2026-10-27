// Paginarea textului: înregistrări IBV -> rânduri -> pagini (LAY-1, LAY-2, LAY-3, LAY-4).
//
// O pagină se calculează de la o poziție (paragraf, octet) și umple zona de
// text cu rânduri până când următorul nu mai încape. Rândurile sunt intervale
// de octeți din înregistrarea lor plus locul unde se desenează, deci o pagină
// ocupă câteva sute de octeți, iar desenarea recitește textul din cache-ul de
// blocuri al cărții.
//
// Puncte de rupere: spațiile, cratimele moi U+00AD (se desenează '-'), U+200B,
// după '-', '–', '—' în interiorul unui cuvânt; U+2028 forțează ruperea.
// U+00A0 e un spațiu care se lățește, dar nu se rupe. Toate lățimile sunt în
// 1/64 px, cu kerning (font.h), deci justificarea nu acumulează erori de rotunjire.
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
// O înregistrare CONT (un paragraf prea lung pe care convertorul l-a împărțit
// la un spațiu) începe un rând nou fără alineat sau spațiu, iar rândul dinaintea
// ei e justificat, deci îmbinarea nu se vede decât dacă acel rând e scurt.
// Rândurile nu trec niciodată peste două înregistrări. Împărțirea e rară (30
// din 70.000 de înregistrări în corpus).
//
// Paginile sunt mereu cele obținute paginând de la începutul cărții: paginarea
// unei pagini depinde doar de locul unde începe, iar cu chapter_page un capitol
// (o înregistrare cu IBV_PAGE) începe mereu o pagină, deci paginarea de acolo
// dă aceleași pagini. Așa găsesc layout_locate și layout_prev o pagină fără să
// pagineze toată cartea dinaintea ei.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "ibv.h"

#define LAYOUT_VERSION 3         // face parte din layout_hash: se crește când se pot schimba rupturile de rând
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
    // Fonturile textului după stilul IBV: [0] regular, [1] bold, [2] italic, [3] bold italic.
    // Doar [0] e obligatoriu; în locul unui font lipsă se folosește regular.
    const font_t *body[4];
    const font_t *heading;       // NULL: bold-ul textului (sau regular)
    int width, height;           // zona de text, px
    int indent;                  // alineatul primului rând la PARA și marginile la QUOTE, px
    int line_gap;                // px adăugați la înălțimea fiecărui rând de text
    int para_gap;                // px între paragrafe
    bool justify;
    bool hyphenate;              // rupe la cratimele moi
    bool chapter_page;           // înregistrările cu IBV_PAGE (capitolele) încep o pagină nouă
    bool keep_lines;             // fără rânduri văduve sau orfane (ultimul sau primul rând al unui paragraf singur)
    layout_fit_t *fit;           // NULL: rupere greedy peste tot
} layout_cfg_t;

typedef struct {
    uint32_t para, byte;
} layout_pos_t;

enum {
    LINE_HYPHEN = 1,             // se desenează '-' la final (rupere la cratimă moale)
    LINE_ORNAMENT = 2,           // un BREAK desenat ca "* * *"
    LINE_HEADING = 4,            // desenat cu fontul titlurilor
};

typedef struct {
    uint32_t para;
    uint32_t start, end;         // octeții de text [start, end) ai înregistrării `para`
    int32_t x;                   // începutul peniței, în 1/64 px de la marginea stângă a zonei de text
    int32_t extra;               // justificarea: 1/64 px împărțiți pe `gaps` spații (< 0: îngustare)
    uint16_t gaps;
    int16_t baseline;            // px de la marginea de sus a zonei de text
    uint8_t flags;
} layout_line_t;

typedef struct {
    layout_pos_t start, end;     // end = începutul paginii următoare
    bool at_end;                 // cartea se termină pe pagina asta
    int line_count;
    layout_line_t lines[LAYOUT_MAX_LINES];
} layout_page_t;

// Paginează pagina care începe la `pos`.
ibv_err_t layout_page(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *page);

// Începutul paginii care conține `pos` (start <= pos < end), pentru deschiderea
// unei cărți la o poziție salvată după ce s-a schimbat paginarea. `scratch` se suprascrie.
ibv_err_t layout_locate(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *scratch,
                        layout_pos_t *out);

// Începutul paginii dinaintea celei care începe la `pos` (sau care conține textul
// de chiar dinainte de `pos`). Întoarce IBV_ERR_RANGE la începutul cărții. Fără
// index de pagini, paginează de la începutul capitolului (sau al cărții, dacă
// nu are capitole); `*pages`, dacă nu e NULL, primește câte pagini au fost.
ibv_err_t layout_prev(const layout_cfg_t *cfg, ibv_t *book, layout_pos_t pos, layout_page_t *scratch,
                      layout_pos_t *out, int *pages);

static inline int layout_pos_cmp(layout_pos_t a, layout_pos_t b)
{
    if (a.para != b.para) return a.para < b.para ? -1 : 1;
    return a.byte < b.byte ? -1 : a.byte > b.byte;
}

// Desenează o pagină paginată, cu colțul stânga-sus al zonei de text la (x0, y0).
ibv_err_t layout_draw(const layout_cfg_t *cfg, ibv_t *book, const layout_page_t *page, gfx_t *g,
                      int x0, int y0);

// Identifică paginarea pentru indexurile de pagini (specificația: IPGC layout_hash):
// fonturile, mărimile, zona, spațierea, greedy sau încadrare totală și LAYOUT_VERSION.
uint32_t layout_hash(const layout_cfg_t *cfg);
