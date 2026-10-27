// Reader for IBV books (spec/book-format.md). Portable C over stdio: on the
// device the file is on the SD card (FATFS through the VFS), in the simulator
// it's a normal file.
//
// Opening reads the header, the directory and the small sections (META, TOC,
// BLKS) and checks their CRCs. Text blocks are read and inflated on demand,
// checked against their CRC, and kept in a cache of two, so a page that
// crosses a block boundary doesn't inflate anything twice.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define IBV_MAX_SECTIONS 16
#define IBV_MAX_BLOCK_RAW 32768
#define IBV_CACHE_SLOTS 2

// Record kinds and flags (spec 5).
enum { IBV_PARA = 0, IBV_HEADING = 1, IBV_VERSE = 2, IBV_BREAK = 3, IBV_QUOTE = 4 };
#define IBV_ALIGN_MASK 0x03    // 0 kind default, 1 left, 2 center, 3 right
#define IBV_NO_INDENT 0x04
#define IBV_ORNAMENT 0x04      // BREAK: draw "* * *"
#define IBV_CONT 0x08          // continues the previous record
#define IBV_LEVEL_SHIFT 4      // HEADING: bits 4-5 = level - 1
#define IBV_STYLE_BOLD 1
#define IBV_STYLE_ITALIC 2

// META keys.
enum { IBV_TITLE = 1, IBV_AUTHOR, IBV_LANG, IBV_SOURCE, IBV_CONVERTER, IBV_CONVERTED, IBV_YEAR, IBV_DESC,
       IBV_META_KEYS };

typedef enum {
    IBV_OK = 0,
    IBV_ERR_IO,        // read error or file too short
    IBV_ERR_MAGIC,
    IBV_ERR_VERSION,
    IBV_ERR_CRC,
    IBV_ERR_FORMAT,    // a section or block is malformed
    IBV_ERR_NOMEM,
    IBV_ERR_RANGE,     // paragraph index out of range
} ibv_err_t;

typedef struct {
    uint32_t offset, stored_len;
    uint16_t raw_len;
    uint8_t method;     // 0 stored, 1 raw DEFLATE
    uint32_t first_para, text_start, crc;
} ibv_block_t;

// One record, pointing into a cached block: valid until the next ibv_ call
// that loads a block (ibv_record, ibv_text_offset) evicts that block. Text is
// UTF-8, not NUL-terminated.
typedef struct {
    uint8_t kind, flags;
    const char *text;
    uint32_t text_len;
    const uint8_t *runs;     // encoded style runs (spec 5.2); read with ibv_styles_*
    uint32_t run_count;
} ibv_record_t;

typedef struct {
    uint32_t para;
    uint8_t level;
    const char *title;       // not NUL-terminated
    uint16_t title_len;
} ibv_toc_t;

typedef struct {
    int32_t block;           // -1 = empty
    uint32_t used;           // last use, for eviction
    uint8_t *raw;
    uint16_t *rec_off;       // offset of each record in raw
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
    const char *meta[IBV_META_KEYS];   // NUL-terminated, or NULL
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
void ibv_close(ibv_t *b);
const char *ibv_strerror(ibv_err_t err);

// Record `para` (0 .. paragraph_count-1).
ibv_err_t ibv_record(ibv_t *b, uint32_t para, ibv_record_t *out);
// Position of (para, byte) in the book's text, in bytes from the start: for
// the progress percentage.
ibv_err_t ibv_text_offset(ibv_t *b, uint32_t para, uint32_t byte, uint32_t *out);

bool ibv_toc(const ibv_t *b, uint32_t i, ibv_toc_t *out);
// Index of the last TOC entry at or before `para`, or -1.
int32_t ibv_toc_find(const ibv_t *b, uint32_t para);

// Cover bitmap (bit 1 = white, rows padded to bytes). Returns false if the
// book has none. *bits is malloc'd; the caller frees it.
bool ibv_cover(ibv_t *b, uint16_t *w, uint16_t *h, uint8_t **bits);

// Style runs, read in order. A cursor answers "which style is byte n" for
// increasing n.
typedef struct {
    const uint8_t *p, *end;
    uint32_t left;           // runs not read yet
    uint32_t start, stop;    // current run [start, stop)
    uint8_t style;
} ibv_styles_t;

void ibv_styles_init(ibv_styles_t *s, const ibv_record_t *r);
uint8_t ibv_style_at(ibv_styles_t *s, uint32_t byte);   // `byte` must not decrease
