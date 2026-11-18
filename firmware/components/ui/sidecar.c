#include "sidecar.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "crc32.h"

#define POS_SIZE 20         // "IPOS", book_id, u32 para, u32 byte (+ CRC = 24)
#define BM_HEAD 14          // "IBMK", book_id, u16 count
#define RECENT_HEAD 8       // "IREC", u8 versiune, u8 reading, u8 count, 1 rezervat
#define LAYOUTS_HEAD 8      // "ILAY", u8 versiune, u8 count, 2 rezervați
#define PG_NAME 32          // "<16 hex>-<8 hex>.pg.tmp", fără NUL
#define PRUNE_BATCH 16      // nume adunate la o trecere prin dosar

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool sidecar_path(char *buf, size_t n, const char *root, const uint8_t id[8], const char *suffix)
{
    if (snprintf(buf, n, "%s/" SIDECAR_DIR, root) >= (int)n) return false;
    mkdir(buf, 0777);   // există deja de obicei; eroarea se vede la deschidere
    size_t len = strlen(buf);
    if (!id) return snprintf(buf + len, n - len, "/%s", suffix) < (int)(n - len);
    return snprintf(buf + len, n - len, "/%02x%02x%02x%02x%02x%02x%02x%02x%s", id[0], id[1], id[2], id[3], id[4],
                    id[5], id[6], id[7], suffix) < (int)(n - len);
}

bool sidecar_write(const char *path, const void *data, size_t len)
{
    char tmp[256];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    uint8_t c[4];
    put32(c, crc32_update(0, data, len));
    bool ok = fwrite(data, 1, len, f) == len && fwrite(c, 1, 4, f) == 4;
    ok = (fclose(f) == 0) && ok;
    // FATFS nu redenumește peste un fișier existent (vezi pageidx_save).
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    return ok;
}

int sidecar_read(const char *path, void *data, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t *d = data;
    size_t n = fread(d, 1, cap, f);
    bool whole = fgetc(f) == EOF;
    fclose(f);
    if (!whole || n < 4) return -1;
    n -= 4;
    return crc32_ok(crc32_update(0, d, n), get32(d + n)) ? (int)n : -1;
}

bool pos_load(const char *root, const uint8_t id[8], layout_pos_t *pos)
{
    char path[160];
    uint8_t d[POS_SIZE + 4];
    if (!sidecar_path(path, sizeof path, root, id, ".pos")) return false;
    if (sidecar_read(path, d, sizeof d) != POS_SIZE || memcmp(d, "IPOS", 4) || memcmp(d + 4, id, 8)) return false;
    *pos = (layout_pos_t){get32(d + 12), get32(d + 16)};
    return true;
}

bool pos_save(const char *root, const uint8_t id[8], layout_pos_t pos)
{
    char path[160];
    uint8_t d[POS_SIZE] = {'I', 'P', 'O', 'S'};
    if (!sidecar_path(path, sizeof path, root, id, ".pos")) return false;
    memcpy(d + 4, id, 8);
    put32(d + 12, pos.para);
    put32(d + 16, pos.byte);
    return sidecar_write(path, d, sizeof d);
}

void bm_load(const char *root, const uint8_t id[8], bookmarks_t *bm)
{
    static uint8_t d[BM_HEAD + BOOKMARKS_MAX * 8 + 4];
    char path[160];
    bm->count = 0;
    if (!sidecar_path(path, sizeof path, root, id, ".bm")) return;
    int n = sidecar_read(path, d, sizeof d);
    if (n < BM_HEAD || memcmp(d, "IBMK", 4) || memcmp(d + 4, id, 8)) return;
    uint16_t count = (uint16_t)(d[12] | d[13] << 8);
    if (count > BOOKMARKS_MAX || n != BM_HEAD + count * 8) return;
    for (int i = 0; i < count; i++) bm->at[i] = (layout_pos_t){get32(d + BM_HEAD + i * 8), get32(d + BM_HEAD + i * 8 + 4)};
    bm->count = count;
}

bool bm_save(const char *root, const uint8_t id[8], const bookmarks_t *bm)
{
    static uint8_t d[BM_HEAD + BOOKMARKS_MAX * 8];
    char path[160];
    if (!sidecar_path(path, sizeof path, root, id, ".bm")) return false;
    memcpy(d, "IBMK", 4);
    memcpy(d + 4, id, 8);
    d[12] = (uint8_t)bm->count, d[13] = (uint8_t)(bm->count >> 8);
    for (int i = 0; i < bm->count; i++) {
        put32(d + BM_HEAD + i * 8, bm->at[i].para);
        put32(d + BM_HEAD + i * 8 + 4, bm->at[i].byte);
    }
    return sidecar_write(path, d, BM_HEAD + bm->count * 8u);
}

int bm_find(const bookmarks_t *bm, layout_pos_t from, layout_pos_t to)
{
    for (int i = 0; i < bm->count; i++)
        if (layout_pos_cmp(bm->at[i], from) >= 0 && layout_pos_cmp(bm->at[i], to) < 0) return i;
    return -1;
}

bool bm_add(bookmarks_t *bm, layout_pos_t at)
{
    if (bm->count >= BOOKMARKS_MAX) return false;
    int i = bm->count;
    while (i > 0 && layout_pos_cmp(bm->at[i - 1], at) > 0) {
        bm->at[i] = bm->at[i - 1];
        i--;
    }
    bm->at[i] = at;
    bm->count++;
    return true;
}

void bm_remove(bookmarks_t *bm, int i)
{
    if (i < 0 || i >= bm->count) return;
    memmove(&bm->at[i], &bm->at[i + 1], (bm->count - i - 1u) * sizeof bm->at[0]);
    bm->count--;
}

// /.ibv/recent: antetul, apoi count × {book_id, u8 lungimea căii, calea}, apoi CRC32.
void recent_load(const char *root, recent_t *r)
{
    static uint8_t d[RECENT_HEAD + RECENT_MAX * (9 + RECENT_PATH) + 4];
    char path[160];
    memset(r, 0, sizeof *r);
    if (!sidecar_path(path, sizeof path, root, NULL, "recent")) return;
    int n = sidecar_read(path, d, sizeof d);
    if (n < RECENT_HEAD || memcmp(d, "IREC", 4) || d[4] != 1 || d[6] > RECENT_MAX) return;
    int pos = RECENT_HEAD;
    for (int i = 0; i < d[6]; i++) {
        if (pos + 9 > n) return;
        uint8_t len = d[pos + 8];
        if (len == 0 || len >= RECENT_PATH || pos + 9 + len > n) return;
        memcpy(r->book[i].id, d + pos, 8);
        memcpy(r->book[i].path, d + pos + 9, len);
        r->book[i].path[len] = 0;
        pos += 9 + len;
    }
    if (pos != n) return;
    r->count = d[6];
    r->reading = d[5] && r->count > 0;
}

bool recent_save(const char *root, const recent_t *r)
{
    static uint8_t d[RECENT_HEAD + RECENT_MAX * (9 + RECENT_PATH)];
    char path[160];
    if (!sidecar_path(path, sizeof path, root, NULL, "recent")) return false;
    memcpy(d, "IREC", 4);
    d[4] = 1, d[5] = r->reading, d[6] = r->count, d[7] = 0;
    size_t pos = RECENT_HEAD;
    for (int i = 0; i < r->count; i++) {
        size_t len = strlen(r->book[i].path);
        memcpy(d + pos, r->book[i].id, 8);
        d[pos + 8] = (uint8_t)len;
        memcpy(d + pos + 9, r->book[i].path, len);
        pos += 9 + len;
    }
    return sidecar_write(path, d, pos);
}

int recent_find(const recent_t *r, const uint8_t id[8])
{
    for (int i = 0; i < r->count; i++)
        if (!memcmp(r->book[i].id, id, 8)) return i;
    return -1;
}

void recent_touch(recent_t *r, const uint8_t id[8], const char *path)
{
    char copy[RECENT_PATH];
    snprintf(copy, sizeof copy, "%s", path);    // `path` poate fi chiar una din intrări
    path = copy;
    int i = recent_find(r, id);
    if (i < 0) i = r->count < RECENT_MAX ? r->count++ : RECENT_MAX - 1;   // plină: iese cea mai veche
    memmove(&r->book[1], &r->book[0], (size_t)i * sizeof r->book[0]);
    memcpy(r->book[0].id, id, 8);
    snprintf(r->book[0].path, RECENT_PATH, "%s", path);
}

// Hash-ul paginării din numele unui index, `<16 hex>-<8 hex>.pg` (sau `.pg.tmp`,
// rămas de la o scriere întreruptă). False pentru orice alt fișier.
static bool pg_name_hash(const char *name, uint32_t *hash)
{
    size_t len = strlen(name);
    if ((len != 28 || strcmp(name + 25, ".pg")) && (len != 32 || strcmp(name + 25, ".pg.tmp"))) return false;
    for (int i = 0; i < 25; i++)
        if (i == 16 ? name[i] != '-' : !strchr("0123456789abcdefABCDEF", name[i])) return false;
    char hex[9];
    memcpy(hex, name + 17, 8);
    hex[8] = 0;
    *hash = (uint32_t)strtoul(hex, NULL, 16);
    return true;
}

// Șterge indexurile paginărilor care nu sunt în `keep`. Numele se adună câte
// PRUNE_BATCH, cu dosarul închis înainte de ștergere: ștergerea în timpul
// parcurgerii nu e sigură pe toate sistemele de fișiere.
static int pg_prune(const char *root, const uint32_t *keep, int count)
{
    static char names[PRUNE_BATCH][PG_NAME + 1];
    char dir[160], path[200];
    int removed = 0;
    if (snprintf(dir, sizeof dir, "%s/" SIDECAR_DIR, root) >= (int)sizeof dir) return 0;
    for (;;) {
        DIR *d = opendir(dir);
        if (!d) return removed;
        int n = 0;
        for (struct dirent *e; n < PRUNE_BATCH && (e = readdir(d));) {
            uint32_t h;
            if (!pg_name_hash(e->d_name, &h)) continue;
            bool kept = false;
            for (int i = 0; i < count; i++) kept |= keep[i] == h;
            if (!kept) memcpy(names[n++], e->d_name, strlen(e->d_name) + 1);   // cel mult PG_NAME, verificat
        }
        closedir(d);
        int now = 0;
        for (int i = 0; i < n; i++) {
            if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path) continue;
            if (remove(path) == 0) now++;
        }
        removed += now;
        // Un lot neplin a fost tot dosarul; un lot din care nu s-a șters nimic s-ar repeta la nesfârșit.
        if (n < PRUNE_BATCH || now == 0) return removed;
    }
}

// /.ibv/layouts: antetul, apoi count × u32 hash-ul paginării, cel mai recent primul, apoi CRC32.
int layouts_touch(const char *root, uint32_t hash)
{
    uint8_t d[LAYOUTS_HEAD + LAYOUTS_MAX * 4 + 4];
    uint32_t list[LAYOUTS_MAX + 1];
    char path[160];
    int count = 0;
    if (!sidecar_path(path, sizeof path, root, NULL, "layouts")) return -1;
    int n = sidecar_read(path, d, sizeof d);
    if (n >= LAYOUTS_HEAD && !memcmp(d, "ILAY", 4) && d[4] == 1 && d[5] <= LAYOUTS_MAX && n == LAYOUTS_HEAD + d[5] * 4)
        for (; count < d[5]; count++) list[count] = get32(d + LAYOUTS_HEAD + count * 4);
    int i = 0;
    while (i < count && list[i] != hash) i++;
    if (i == 0 && count > 0) return 0;      // deja prima: nicio scriere pe card
    bool known = i < count;
    if (!known) i = count++;
    memmove(&list[1], &list[0], (size_t)i * sizeof list[0]);
    list[0] = hash;
    if (count > LAYOUTS_MAX) count = LAYOUTS_MAX;
    memcpy(d, "ILAY", 4);
    d[4] = 1, d[5] = (uint8_t)count, d[6] = d[7] = 0;
    for (int k = 0; k < count; k++) put32(d + LAYOUTS_HEAD + k * 4, list[k]);
    if (!sidecar_write(path, d, LAYOUTS_HEAD + (size_t)count * 4)) return -1;
    // O listă pierdută sau stricată începe de la zero: rămâne doar indexul paginării curente.
    return known ? 0 : pg_prune(root, list, count);
}
