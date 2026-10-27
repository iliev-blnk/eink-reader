// Fișierele însoțitoare ale cărților (FW-11, spec/book-format.md, secțiunea 6):
// poziția (`.pos`), semnele de carte (`.bm`) și lista cărților recente
// (`/.ibv/recent`), în `/.ibv/` pe card, numite după book_id în hex.
//
// Fiecare începe cu un cod magic și se termină cu un CRC32 al octeților
// dinainte; unul greșit e ignorat. Scrierea trece prin `<nume>.tmp`, ca o cădere
// de curent să piardă cel mult fișierul, nu să lase unul greșit.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "layout.h"

#define SIDECAR_DIR ".ibv"
#define BOOKMARKS_MAX 64
#define RECENT_MAX 16
#define RECENT_PATH 120     // calea cărții față de rădăcina cardului, cu NUL

// "<root>/.ibv/<id hex><suffix>", de exemplu suffix ".pos" sau "-1a2b3c4d.pg".
// Creează dosarul dacă lipsește. False dacă nu încape.
bool sidecar_path(char *buf, size_t n, const char *root, const uint8_t id[8], const char *suffix);
// Scrie `len` octeți plus CRC32-ul lor, prin `<path>.tmp`.
bool sidecar_write(const char *path, const void *data, size_t len);
// Citește tot fișierul (cel mult `cap` octeți, cu tot cu CRC) și verifică CRC-ul
// de la final. Întoarce lungimea fără CRC, sau -1 (și pentru un fișier mai lung).
int sidecar_read(const char *path, void *data, size_t cap);

bool pos_load(const char *root, const uint8_t id[8], layout_pos_t *pos);
bool pos_save(const char *root, const uint8_t id[8], layout_pos_t pos);

typedef struct {
    uint16_t count;
    layout_pos_t at[BOOKMARKS_MAX];     // în ordinea din carte
} bookmarks_t;

void bm_load(const char *root, const uint8_t id[8], bookmarks_t *bm);
bool bm_save(const char *root, const uint8_t id[8], const bookmarks_t *bm);
// Indicele unui semn în [from, to), sau -1.
int bm_find(const bookmarks_t *bm, layout_pos_t from, layout_pos_t to);
bool bm_add(bookmarks_t *bm, layout_pos_t at);
void bm_remove(bookmarks_t *bm, int i);

// Cărțile deschise recent, cea mai recentă prima. `reading` spune dacă la somn
// era deschisă prima: atunci trezirea o redeschide.
typedef struct {
    uint8_t id[8];
    char path[RECENT_PATH];
} recent_book_t;

typedef struct {
    bool reading;
    uint8_t count;
    recent_book_t book[RECENT_MAX];
} recent_t;

void recent_load(const char *root, recent_t *r);
bool recent_save(const char *root, const recent_t *r);
// Pune cartea prima (o mută dacă era deja în listă).
void recent_touch(recent_t *r, const uint8_t id[8], const char *path);
// Locul cărții în listă, sau -1.
int recent_find(const recent_t *r, const uint8_t id[8]);
