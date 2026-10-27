// Cititorul de cărți IBV (spec/book-format.md). C portabil peste stdio: pe
// dispozitiv fișierul e pe cardul SD (FATFS prin VFS), în simulator e un
// fișier obișnuit.
//
// Deschiderea citește antetul, directorul și secțiunile mici (META, TOC,
// BLKS) și le verifică CRC-urile. Blocurile de text se citesc și se decomprimă
// la cerere, se verifică după CRC-ul lor și se țin într-un cache de două, deci
// o pagină care trece peste granița unui bloc nu decomprimă nimic de două ori.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define IBV_MAX_SECTIONS 16
#define IBV_MAX_BLOCK_RAW 32768
#define IBV_CACHE_SLOTS 2

// Tipurile și indicatorii înregistrărilor (specificația, 3.6).
enum { IBV_PARA = 0, IBV_HEADING = 1, IBV_VERSE = 2, IBV_BREAK = 3, IBV_QUOTE = 4 };
#define IBV_ALIGN_MASK 0x03    // 0 implicit pentru tip, 1 stânga, 2 centru, 3 dreapta
#define IBV_NO_INDENT 0x04
#define IBV_ORNAMENT 0x04      // BREAK: se desenează "* * *"
#define IBV_CONT 0x08          // continuă înregistrarea precedentă
#define IBV_LEVEL_SHIFT 4      // HEADING: biții 4-5 = nivelul - 1
#define IBV_PAGE 0x40          // începe o pagină nouă (un capitol; pus de convertor)
#define IBV_STYLE_BOLD 1
#define IBV_STYLE_ITALIC 2

// Cheile META.
enum { IBV_TITLE = 1, IBV_AUTHOR, IBV_LANG, IBV_SOURCE, IBV_CONVERTER, IBV_CONVERTED, IBV_YEAR, IBV_DESC,
       IBV_META_KEYS };

typedef enum {
    IBV_OK = 0,
    IBV_ERR_IO,        // eroare de citire sau fișier prea scurt
    IBV_ERR_MAGIC,
    IBV_ERR_VERSION,
    IBV_ERR_CRC,
    IBV_ERR_FORMAT,    // o secțiune sau un bloc e greșit format
    IBV_ERR_NOMEM,
    IBV_ERR_RANGE,     // indicele paragrafului e în afara limitelor
} ibv_err_t;

typedef struct {
    uint32_t offset, stored_len;
    uint16_t raw_len;
    uint8_t method;     // 0 stocat, 1 DEFLATE brut
    uint32_t first_para, text_start, crc;
} ibv_block_t;

// O înregistrare, cu pointeri în interiorul unui bloc din cache: valabilă până
// când următorul apel ibv_ care încarcă un bloc (ibv_record, ibv_text_offset)
// scoate acel bloc din cache. Textul e UTF-8, fără NUL la final.
typedef struct {
    uint8_t kind, flags;
    const char *text;
    uint32_t text_len;
    const uint8_t *runs;     // intervalele de stil codificate (specificația, 3.6); se citesc cu ibv_styles_*
    uint32_t run_count;
} ibv_record_t;

typedef struct {
    uint32_t para;
    uint8_t level;
    const char *title;       // fără NUL la final
    uint16_t title_len;
} ibv_toc_t;

typedef struct {
    int32_t block;           // -1 = gol
    uint32_t used;           // ultima folosire, pentru scoaterea din cache
    uint8_t *raw;
    uint16_t *rec_off;       // deplasamentul fiecărei înregistrări în raw
    uint32_t rec_count;
} ibv_slot_t;

typedef struct {
    FILE *f;
    uint32_t file_size;
    uint8_t version_minor;
    uint8_t book_id[8];
    uint32_t paragraph_count, text_bytes;
    struct { char tag[5]; uint32_t offset, length, crc; } sections[IBV_MAX_SECTIONS];
    int section_count;
    const char *meta[IBV_META_KEYS];   // terminate cu NUL, sau NULL
    char *meta_buf;
    uint8_t *toc_buf;
    uint32_t *toc_off;
    uint16_t toc_count;
    ibv_block_t *blocks;
    uint32_t block_count;
    ibv_slot_t cache[IBV_CACHE_SLOTS];
    uint32_t clock;
    uint32_t blocks_loaded;
} ibv_t;

ibv_err_t ibv_open(ibv_t *b, const char *path);
// Doar antetul, directorul și META (specificația, secțiunea 5: biblioteca), plus
// coperta la cerere. ibv_record și cuprinsul nu merg pe o carte deschisă așa.
ibv_err_t ibv_open_meta(ibv_t *b, const char *path);
void ibv_close(ibv_t *b);
const char *ibv_strerror(ibv_err_t err);

// Înregistrarea `para` (0 .. paragraph_count-1).
ibv_err_t ibv_record(ibv_t *b, uint32_t para, ibv_record_t *out);
// Poziția lui (para, byte) în textul cărții, în octeți de la început: pentru
// procentul de progres.
ibv_err_t ibv_text_offset(ibv_t *b, uint32_t para, uint32_t byte, uint32_t *out);

bool ibv_toc(const ibv_t *b, uint32_t i, ibv_toc_t *out);
// Indicele ultimei intrări din cuprins la sau înainte de `para`, sau -1.
int32_t ibv_toc_find(const ibv_t *b, uint32_t para);

// Bitmapul copertei (bitul 1 = alb, rândurile completate la octeți). Întoarce
// false dacă nu are. *bits e alocat cu malloc; îl eliberează apelantul.
bool ibv_cover(ibv_t *b, uint16_t *w, uint16_t *h, uint8_t **bits);

// Intervalele de stil, citite în ordine. Un cursor răspunde la „ce stil are
// octetul n” pentru n crescător.
typedef struct {
    const uint8_t *p, *end;
    uint32_t left;           // intervale încă necitite
    uint32_t start, stop;    // intervalul curent [start, stop)
    uint8_t style;
} ibv_styles_t;

void ibv_styles_init(ibv_styles_t *s, const ibv_record_t *r);
uint8_t ibv_style_at(ibv_styles_t *s, uint32_t byte);   // `byte` nu are voie să scadă
