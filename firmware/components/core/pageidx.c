#include "pageidx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"

#define HEADER 24                // "IPGC", book_id, layout_hash, page_count, complete, 3 rezervați

void pageidx_init(pageidx_t *x, uint32_t layout_hash)
{
    memset(x, 0, sizeof *x);
    x->layout_hash = layout_hash;
}

void pageidx_free(pageidx_t *x)
{
    free(x->starts);
    pageidx_init(x, x->layout_hash);
}

static bool push(pageidx_t *x, layout_pos_t p)
{
    if (x->count == x->cap) {
        uint32_t cap = x->cap ? x->cap * 2 : 256;
        layout_pos_t *s = realloc(x->starts, cap * sizeof *s);
        if (!s) return false;
        x->starts = s;
        x->cap = cap;
    }
    x->starts[x->count++] = p;
    return true;
}

ibv_err_t pageidx_step(pageidx_t *x, const layout_cfg_t *cfg, ibv_t *book, int max_pages, layout_page_t *scratch)
{
    for (int n = 0; n < max_pages && !x->complete; n++) {
        ibv_err_t err = layout_page(cfg, book, x->next, scratch);
        if (err) return err;
        if (!push(x, x->next)) return IBV_ERR_NOMEM;
        x->next = scratch->end;
        x->complete = scratch->at_end;
    }
    return IBV_OK;
}

int32_t pageidx_find(const pageidx_t *x, layout_pos_t pos)
{
    if (x->count == 0 || (!x->complete && layout_pos_cmp(pos, x->next) >= 0)) return -1;
    uint32_t lo = 0, hi = x->count;           // ultimul început <= pos
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (layout_pos_cmp(x->starts[mid], pos) <= 0) lo = mid;
        else hi = mid;
    }
    return (int32_t)lo;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool pageidx_save(const pageidx_t *x, const uint8_t book_id[8], const char *path)
{
    char tmp[256];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    uint8_t h[HEADER] = {'I', 'P', 'G', 'C'};
    memcpy(h + 4, book_id, 8);
    put32(h + 12, x->layout_hash);
    put32(h + 16, x->count);
    h[20] = x->complete;
    uint32_t crc = crc32_update(0, h, sizeof h);
    bool ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    for (uint32_t i = 0; ok && i < x->count; i++) {
        uint8_t e[8];
        put32(e, x->starts[i].para);
        put32(e + 4, x->starts[i].byte);
        crc = crc32_update(crc, e, sizeof e);
        ok = fwrite(e, 1, sizeof e, f) == sizeof e;
    }
    uint8_t c[4];
    put32(c, crc);
    ok = ok && fwrite(c, 1, 4, f) == 4;
    ok = (fclose(f) == 0) && ok;
    // rename() peste un fișier existent nu e atomic pe FATFS: eșuează, deci
    // fișierul vechi se șterge întâi. O cădere între cei doi pași pierde indexul,
    // care se reface, dar nu lasă niciodată unul greșit.
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    return ok;
}

bool pageidx_load(pageidx_t *x, const uint8_t book_id[8], uint32_t layout_hash, const char *path)
{
    pageidx_free(x);
    x->layout_hash = layout_hash;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[HEADER];
    bool ok = fread(h, 1, sizeof h, f) == sizeof h && !memcmp(h, "IPGC", 4) && !memcmp(h + 4, book_id, 8)
              && get32(h + 12) == layout_hash && h[20] <= 1;
    uint32_t n = ok ? get32(h + 16) : 0;
    uint32_t crc = crc32_update(0, h, sizeof h);
    if (ok && n > 0) {
        x->starts = malloc(n * sizeof *x->starts);
        ok = x->starts != NULL;
        x->cap = ok ? n : 0;
    }
    for (uint32_t i = 0; ok && i < n; i++) {
        uint8_t e[8];
        ok = fread(e, 1, sizeof e, f) == sizeof e;
        crc = crc32_update(crc, e, sizeof e);
        x->starts[i] = (layout_pos_t){get32(e), get32(e + 4)};
        // Începuturile trebuie să crească; primul e începutul cărții.
        if (ok && i == 0) ok = x->starts[0].para == 0 && x->starts[0].byte == 0;
        if (ok && i > 0) ok = layout_pos_cmp(x->starts[i - 1], x->starts[i]) < 0;
    }
    uint8_t c[4];
    ok = ok && fread(c, 1, 4, f) == 4 && get32(c) == crc && fgetc(f) == EOF;
    fclose(f);
    if (!ok || n == 0) {
        pageidx_free(x);
        return false;
    }
    x->count = n;
    x->complete = h[20];
    // Un index incomplet continuă de la ultima lui pagină: pagina aceea se
    // paginează din nou (pageidx_step) ca să se afle unde începe următoarea.
    if (!x->complete) x->next = x->starts[--x->count];
    return true;
}
