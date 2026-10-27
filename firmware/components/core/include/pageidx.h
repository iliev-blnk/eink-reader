// Indexul paginilor (LAY-4): începutul fiecărei pagini a unei cărți într-o
// paginare, pentru „pagina x din y”, pagina înapoi în O(log n) și saltul la un
// număr de pagină.
//
// Se construiește de la începutul cărții, câteva pagini odată (cititorul apelează
// pageidx_step între întoarcerile de pagină, sau o face un task de fundal), și se
// salvează ca fișierul însoțitor `<id>-<layout>.pg` (spec/book-format.md,
// secțiunea 6), deci o carte se paginează o singură dată pentru fiecare paginare.
// 8 octeți pe pagină: Pride and Prejudice cu Literata 16 are 2.057 de pagini, 16 KB.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ibv.h"
#include "layout.h"

typedef struct {
    layout_pos_t *starts;        // starts[i] = începutul paginii i (de la 0)
    uint32_t count, cap;
    bool complete;               // ultima pagină e sfârșitul cărții
    uint32_t layout_hash;
    layout_pos_t next;           // unde începe pagina următoare, dacă nu e complet
} pageidx_t;

void pageidx_init(pageidx_t *x, uint32_t layout_hash);
void pageidx_free(pageidx_t *x);

// Paginează cel mult încă `max_pages` pagini. `scratch` se suprascrie.
ibv_err_t pageidx_step(pageidx_t *x, const layout_cfg_t *cfg, ibv_t *book, int max_pages, layout_page_t *scratch);

// Numărul paginii care conține `pos` (start <= pos < end), sau -1 dacă indexul
// încă nu ajunge până acolo.
int32_t pageidx_find(const pageidx_t *x, layout_pos_t pos);

// Fișierul însoțitor. Salvarea scrie `<path>.tmp` și îl redenumește în `path`.
// Încărcarea eșuează (întoarce false, lasă `x` gol) la un fișier lipsă, un CRC
// greșit sau altă carte ori paginare: atunci indexul se reface.
bool pageidx_save(const pageidx_t *x, const uint8_t book_id[8], const char *path);
bool pageidx_load(pageidx_t *x, const uint8_t book_id[8], uint32_t layout_hash, const char *path);
