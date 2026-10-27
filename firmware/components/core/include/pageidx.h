// Page index (LAY-4): the start of every page of a book in one layout, for
// "page x of y", going back a page in O(log n) and jumping to a page number.
//
// It's built from the start of the book a few pages at a time (the reader
// calls pageidx_step between page turns, or a background task does), and it is
// saved as the `<id>-<layout>.pg` sidecar (spec/book-format.md, section 6), so
// a book is paginated once per layout. 8 bytes per page: Pride and Prejudice
// at Literata 16 is 2,057 pages, 16 KB.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ibv.h"
#include "layout.h"

typedef struct {
    layout_pos_t *starts;        // starts[i] = start of page i (0-based)
    uint32_t count, cap;
    bool complete;               // the last page is the end of the book
    uint32_t layout_hash;
    layout_pos_t next;           // where the next page starts, if not complete
} pageidx_t;

void pageidx_init(pageidx_t *x, uint32_t layout_hash);
void pageidx_free(pageidx_t *x);

// Paginates up to `max_pages` more pages. `scratch` is overwritten.
ibv_err_t pageidx_step(pageidx_t *x, const layout_cfg_t *cfg, ibv_t *book, int max_pages, layout_page_t *scratch);

// Number of the page holding `pos` (start <= pos < end), or -1 if the index
// doesn't reach that far yet.
int32_t pageidx_find(const pageidx_t *x, layout_pos_t pos);

// Sidecar file. Save writes `<path>.tmp` and renames it over `path`. Load
// fails (returns false, leaves `x` empty) on a missing file, a bad CRC, or a
// different book or layout: the index is then rebuilt.
bool pageidx_save(const pageidx_t *x, const uint8_t book_id[8], const char *path);
bool pageidx_load(pageidx_t *x, const uint8_t book_id[8], uint32_t layout_hash, const char *path);
