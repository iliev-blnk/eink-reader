#include "ibv.h"

#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "inflate.h"

#define HEADER_SIZE 44
#define DIR_ENTRY 16
#define BLOCK_ENTRY 24

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const char *ibv_strerror(ibv_err_t err)
{
    switch (err) {
    case IBV_OK: return "ok";
    case IBV_ERR_IO: return "eroare de citire sau fișier prea scurt";
    case IBV_ERR_MAGIC: return "nu e o carte IBV";
    case IBV_ERR_VERSION: return "versiune nesuportată";
    case IBV_ERR_CRC: return "CRC greșit";
    case IBV_ERR_FORMAT: return "carte deteriorată";
    case IBV_ERR_NOMEM: return "memorie insuficientă";
    case IBV_ERR_RANGE: return "paragraf în afara cărții";
    }
    return "?";
}

static bool read_at(ibv_t *b, uint32_t off, void *dst, uint32_t len)
{
    if ((uint64_t)off + len > b->file_size) return false;
    return fseek(b->f, (long)off, SEEK_SET) == 0 && fread(dst, 1, len, b->f) == len;
}

static int find_section(const ibv_t *b, const char *tag)
{
    for (int i = 0; i < b->section_count; i++)
        if (memcmp(b->sections[i].tag, tag, 4) == 0) return i;
    return -1;
}

// Citește o secțiune întreagă și îi verifică CRC-ul. *out e alocat cu malloc (+1 octet, NUL).
static ibv_err_t load_section(ibv_t *b, int i, uint8_t **out)
{
    uint32_t len = b->sections[i].length;
    uint8_t *p = malloc(len + 1);
    if (!p) return IBV_ERR_NOMEM;
    if (!read_at(b, b->sections[i].offset, p, len)) { free(p); return IBV_ERR_IO; }
    if (!crc32_ok(crc32_update(0, p, len), b->sections[i].crc)) { free(p); return IBV_ERR_CRC; }
    p[len] = 0;
    *out = p;
    return IBV_OK;
}

static ibv_err_t parse_meta(ibv_t *b, uint8_t *m, uint32_t len)
{
    // Valorile se rescriu pe loc ca șiruri terminate cu NUL: fiecare valoare
    // se mută cu 3 octeți la stânga, peste propriii octeți de cheie și lungime.
    if (len < 2) return IBV_ERR_FORMAT;
    uint32_t n = rd16(m), pos = 2;
    char *out = (char *)m;
    uint32_t w = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (pos + 3 > len) return IBV_ERR_FORMAT;
        uint8_t key = m[pos];
        uint16_t vlen = rd16(m + pos + 1);
        pos += 3;
        if (pos + vlen > len) return IBV_ERR_FORMAT;
        memmove(out + w, m + pos, vlen);
        out[w + vlen] = 0;
        if (key > 0 && key < IBV_META_KEYS && !b->meta[key]) b->meta[key] = out + w;
        w += vlen + 1u;
        pos += vlen;
    }
    b->meta_buf = out;
    return IBV_OK;
}

static ibv_err_t parse_toc(ibv_t *b, uint8_t *t, uint32_t len)
{
    if (len < 2) return IBV_ERR_FORMAT;
    b->toc_count = rd16(t);
    b->toc_buf = t;
    b->toc_off = malloc(sizeof(uint32_t) * (b->toc_count ? b->toc_count : 1));
    if (!b->toc_off) return IBV_ERR_NOMEM;
    uint32_t pos = 2;
    for (uint32_t i = 0; i < b->toc_count; i++) {
        if (pos + 8 > len) return IBV_ERR_FORMAT;
        b->toc_off[i] = pos;
        pos += 8u + rd16(t + pos + 6);
        if (pos > len) return IBV_ERR_FORMAT;
    }
    return IBV_OK;
}

static ibv_err_t parse_blocks(ibv_t *b, const uint8_t *p, uint32_t len)
{
    if (len < 4) return IBV_ERR_FORMAT;
    uint32_t n = rd32(p);
    if (n == 0 || 4 + (uint64_t)n * BLOCK_ENTRY != len) return IBV_ERR_FORMAT;
    b->blocks = malloc(sizeof(ibv_block_t) * n);
    if (!b->blocks) return IBV_ERR_NOMEM;
    b->block_count = n;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = p + 4 + i * BLOCK_ENTRY;
        ibv_block_t *k = &b->blocks[i];
        k->offset = rd32(e);
        k->stored_len = rd32(e + 4);
        k->raw_len = rd16(e + 8);
        k->method = e[10];
        k->first_para = rd32(e + 12);
        k->text_start = rd32(e + 16);
        k->crc = rd32(e + 20);
        if (k->method > 1 || k->raw_len == 0 || k->raw_len > IBV_MAX_BLOCK_RAW
            || (uint64_t)k->offset + k->stored_len > b->file_size
            || (i == 0 ? k->first_para != 0 : k->first_para <= b->blocks[i - 1].first_para)
            || k->first_para >= b->paragraph_count)
            return IBV_ERR_FORMAT;
    }
    return IBV_OK;
}

static ibv_err_t open_book(ibv_t *b, const char *path, bool full)
{
    memset(b, 0, sizeof *b);
    for (int i = 0; i < IBV_CACHE_SLOTS; i++) b->cache[i].block = -1;
    b->f = fopen(path, "rb");
    if (!b->f) return IBV_ERR_IO;
    ibv_err_t err = IBV_ERR_IO;
    if (fseek(b->f, 0, SEEK_END) != 0) goto fail;
    long size = ftell(b->f);
    if (size < HEADER_SIZE || size > 0x7FFFFFFF) goto fail;
    b->file_size = (uint32_t)size;

    uint8_t h[HEADER_SIZE];
    if (!read_at(b, 0, h, HEADER_SIZE)) goto fail;
    static const uint8_t magic[8] = {0x89, 'I', 'B', 'V', '\r', '\n', 0x1A, '\n'};
    err = IBV_ERR_MAGIC;
    if (memcmp(h, magic, 8) != 0) goto fail;
    err = IBV_ERR_CRC;
    if (!crc32_ok(crc32_update(0, h, 40), rd32(h + 40))) goto fail;
    err = IBV_ERR_VERSION;
    uint16_t hsize = rd16(h + 10);
    if (h[8] != 0 || hsize < HEADER_SIZE) goto fail;
    b->version_minor = h[9];
    memcpy(b->book_id, h + 16, 8);
    b->paragraph_count = rd32(h + 24);
    b->text_bytes = rd32(h + 28);
    uint16_t nsec = rd16(h + 32);
    err = IBV_ERR_FORMAT;
    if (nsec == 0 || b->paragraph_count == 0) goto fail;

    // Directorul se citește pe bucăți: CRC-ul acoperă toate intrările, dar se țin
    // doar etichetele cunoscute, prima apariție a fiecăreia. O carte mai nouă
    // poate avea oricâte secțiuni pe care firmware-ul ăsta nu le știe.
    static const char known[IBV_MAX_SECTIONS][5] = {"META", "TOC ", "BLKS", "COVR", "TEXT"};
    uint8_t dir[16 * DIR_ENTRY];
    uint32_t crc = 0;
    for (uint32_t i = 0; i < nsec; i += 16) {
        uint32_t n = nsec - i < 16 ? nsec - i : 16;
        err = IBV_ERR_IO;
        if (!read_at(b, hsize + i * DIR_ENTRY, dir, n * DIR_ENTRY)) goto fail;
        crc = crc32_update(crc, dir, n * DIR_ENTRY);
        for (uint32_t j = 0; j < n; j++) {
            const uint8_t *e = dir + j * DIR_ENTRY;
            bool want = false;
            for (int k = 0; k < IBV_MAX_SECTIONS; k++) want |= memcmp(e, known[k], 4) == 0;
            if (!want || find_section(b, (const char *)e) >= 0) continue;
            int s = b->section_count++;
            memcpy(b->sections[s].tag, e, 4);
            b->sections[s].tag[4] = 0;
            b->sections[s].offset = rd32(e + 4);
            b->sections[s].length = rd32(e + 8);
            b->sections[s].crc = rd32(e + 12);
        }
    }
    err = IBV_ERR_CRC;
    if (!crc32_ok(crc, rd32(h + 36))) goto fail;
    err = IBV_ERR_FORMAT;
    for (int i = 0; i < b->section_count; i++)
        if ((uint64_t)b->sections[i].offset + b->sections[i].length > b->file_size) goto fail;

    int meta = find_section(b, "META"), toc = find_section(b, "TOC "), blks = find_section(b, "BLKS");
    if (meta < 0 || blks < 0 || find_section(b, "TEXT") < 0) goto fail;
    uint8_t *p;
    if ((err = load_section(b, meta, &p)) != IBV_OK) goto fail;
    if ((err = parse_meta(b, p, b->sections[meta].length)) != IBV_OK) { free(p); goto fail; }
    if (!full) return IBV_OK;
    if (toc >= 0) {
        if ((err = load_section(b, toc, &p)) != IBV_OK) goto fail;
        if ((err = parse_toc(b, p, b->sections[toc].length)) != IBV_OK) {
            if (!b->toc_buf) free(p);
            goto fail;
        }
    }
    if ((err = load_section(b, blks, &p)) != IBV_OK) goto fail;
    err = parse_blocks(b, p, b->sections[blks].length);
    free(p);
    if (err != IBV_OK) goto fail;
    return IBV_OK;
fail:
    ibv_close(b);
    return err;
}

ibv_err_t ibv_open(ibv_t *b, const char *path) { return open_book(b, path, true); }
ibv_err_t ibv_open_meta(ibv_t *b, const char *path) { return open_book(b, path, false); }

void ibv_close(ibv_t *b)
{
    if (b->f) fclose(b->f);
    free(b->meta_buf);
    free(b->toc_buf);
    free(b->toc_off);
    free(b->blocks);
    for (int i = 0; i < IBV_CACHE_SLOTS; i++) {
        free(b->cache[i].raw);
        free(b->cache[i].rec_off);
    }
    memset(b, 0, sizeof *b);
}

static bool uleb(const uint8_t **p, const uint8_t *end, uint32_t *out)
{
    uint32_t n = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (*p >= end) return false;
        uint8_t c = *(*p)++;
        if (shift == 28 && c > 0x0F) return false;    // valoarea ar trece de 2^32 - 1
        n |= (uint32_t)(c & 0x7F) << shift;
        if (!(c & 0x80)) { *out = n; return true; }
    }
    return false;
}

// Decodează înregistrarea de la *p; la reușită *p e după ea.
static bool decode(const uint8_t **p, const uint8_t *end, ibv_record_t *r)
{
    if (end - *p < 2) return false;
    r->kind = (*p)[0];
    r->flags = (*p)[1];
    *p += 2;
    if (!uleb(p, end, &r->text_len) || !uleb(p, end, &r->run_count)) return false;
    r->runs = *p;
    for (uint32_t i = 0; i < r->run_count; i++) {
        uint32_t s, n;
        if (!uleb(p, end, &s) || !uleb(p, end, &n) || *p >= end) return false;
        (*p)++;
    }
    if ((uint32_t)(end - *p) < r->text_len) return false;
    r->text = (const char *)*p;
    *p += r->text_len;
    return true;
}

// Apelantul verifică întâi b->block_count (0 pentru o carte deschisă cu ibv_open_meta).
static uint32_t block_of(const ibv_t *b, uint32_t para)
{
    uint32_t lo = 0, hi = b->block_count - 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1) / 2;
        if (b->blocks[mid].first_para <= para) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static ibv_err_t load_block(ibv_t *b, uint32_t bi, ibv_slot_t **out)
{
    b->clock++;
    ibv_slot_t *slot = NULL;
    for (int i = 0; i < IBV_CACHE_SLOTS; i++)
        if (b->cache[i].block == (int32_t)bi) { slot = &b->cache[i]; slot->used = b->clock; *out = slot; return IBV_OK; }
    slot = &b->cache[0];
    for (int i = 1; i < IBV_CACHE_SLOTS; i++)
        if (b->cache[i].used < slot->used) slot = &b->cache[i];

    const ibv_block_t *k = &b->blocks[bi];
    uint32_t nrec = (bi + 1 < b->block_count ? b->blocks[bi + 1].first_para : b->paragraph_count) - k->first_para;
    slot->block = -1;
    // O înregistrare are cel puțin 4 octeți (tip, indicatori, două uleb), deci un
    // bloc nu poate avea mai multe de raw_len / 4. Fără limita asta, un first_para
    // greșit cere aici megaocteți, iar pe 32 de biți 2 * nrec poate da peste cap.
    if (nrec > k->raw_len / 4u) return IBV_ERR_FORMAT;
    if (!slot->raw && !(slot->raw = malloc(IBV_MAX_BLOCK_RAW))) return IBV_ERR_NOMEM;
    uint16_t *ro = realloc(slot->rec_off, sizeof(uint16_t) * nrec);
    if (!ro) return IBV_ERR_NOMEM;
    slot->rec_off = ro;

    if (k->method == 0) {
        if (k->stored_len != k->raw_len) return IBV_ERR_FORMAT;
        if (!read_at(b, k->offset, slot->raw, k->raw_len)) return IBV_ERR_IO;
    } else {
        uint8_t *stored = malloc(k->stored_len);
        if (!stored) return IBV_ERR_NOMEM;
        bool ok = read_at(b, k->offset, stored, k->stored_len);
        if (ok) ok = inflate_raw(stored, k->stored_len, slot->raw, k->raw_len);
        free(stored);
        if (!ok) return IBV_ERR_FORMAT;
    }
    if (!crc32_ok(crc32_update(0, slot->raw, k->raw_len), k->crc)) return IBV_ERR_CRC;

    const uint8_t *p = slot->raw, *end = slot->raw + k->raw_len;
    for (uint32_t i = 0; i < nrec; i++) {
        ibv_record_t r;
        ro[i] = (uint16_t)(p - slot->raw);
        if (!decode(&p, end, &r)) return IBV_ERR_FORMAT;
    }
    if (p != end) return IBV_ERR_FORMAT;
    slot->rec_count = nrec;
    slot->block = (int32_t)bi;
    slot->used = b->clock;
    b->blocks_loaded++;
    *out = slot;
    return IBV_OK;
}

ibv_err_t ibv_record(ibv_t *b, uint32_t para, ibv_record_t *out)
{
    if (para >= b->paragraph_count) return IBV_ERR_RANGE;
    if (!b->block_count) return IBV_ERR_FORMAT;
    uint32_t bi = block_of(b, para);
    ibv_slot_t *s;
    ibv_err_t err = load_block(b, bi, &s);
    if (err) return err;
    const uint8_t *p = s->raw + s->rec_off[para - b->blocks[bi].first_para];
    return decode(&p, s->raw + b->blocks[bi].raw_len, out) ? IBV_OK : IBV_ERR_FORMAT;
}

ibv_err_t ibv_text_offset(ibv_t *b, uint32_t para, uint32_t byte, uint32_t *out)
{
    if (para >= b->paragraph_count) { *out = b->text_bytes; return IBV_OK; }
    if (!b->block_count) return IBV_ERR_FORMAT;
    uint32_t bi = block_of(b, para);
    uint32_t off = b->blocks[bi].text_start;
    for (uint32_t p = b->blocks[bi].first_para; p < para; p++) {
        ibv_record_t r;
        ibv_err_t err = ibv_record(b, p, &r);
        if (err) return err;
        off += r.text_len;
    }
    *out = off + byte;
    return IBV_OK;
}

bool ibv_toc(const ibv_t *b, uint32_t i, ibv_toc_t *out)
{
    if (i >= b->toc_count) return false;
    const uint8_t *e = b->toc_buf + b->toc_off[i];
    out->para = rd32(e);
    out->level = e[4];
    out->title_len = rd16(e + 6);
    out->title = (const char *)e + 8;
    return true;
}

int32_t ibv_toc_find(const ibv_t *b, uint32_t para)
{
    int32_t found = -1;
    for (uint32_t i = 0; i < b->toc_count; i++) {
        if (rd32(b->toc_buf + b->toc_off[i]) > para) break;
        found = (int32_t)i;
    }
    return found;
}

bool ibv_cover(ibv_t *b, uint16_t *w, uint16_t *h, uint8_t **bits)
{
    int i = find_section(b, "COVR");
    uint8_t *p;
    if (i < 0 || b->sections[i].length < 10 || load_section(b, i, &p) != IBV_OK) return false;
    *w = rd16(p);
    *h = rd16(p + 2);
    uint32_t raw_len = rd32(p + 6), stored = b->sections[i].length - 10;
    // cel mult ecranul în portret (specificația, 3.8): o copertă mai mare ar cere
    // sute de KB pe un cip care n-are PSRAM
    bool ok = *w <= 240 && *h <= 416 && raw_len == (uint32_t)(*w + 7) / 8 * *h && (*bits = malloc(raw_len ? raw_len : 1)) != NULL;
    if (ok) {
        if (p[4] == 0) ok = stored == raw_len && memcpy(*bits, p + 10, raw_len);
        else ok = p[4] == 1 && inflate_raw(p + 10, stored, *bits, raw_len);
        if (!ok) free(*bits);
    }
    free(p);
    return ok;
}

void ibv_styles_init(ibv_styles_t *s, const ibv_record_t *r)
{
    s->p = r->runs;
    s->end = (const uint8_t *)r->text;    // intervalele se termină unde începe textul
    s->left = r->run_count;
    s->start = s->stop = 0;
    s->style = 0;
}

uint8_t ibv_style_at(ibv_styles_t *s, uint32_t byte)
{
    while (byte >= s->stop) {
        uint32_t start, len;
        if (!s->left || !uleb(&s->p, s->end, &start) || !uleb(&s->p, s->end, &len) || s->p >= s->end) {
            s->left = 0;
            s->start = s->stop = UINT32_MAX;
            return 0;
        }
        s->style = *s->p++;
        s->left--;
        s->start = start;
        s->stop = start + len;
    }
    return byte >= s->start ? s->style : 0;
}
