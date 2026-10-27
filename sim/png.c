#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t b[4];
    put32(b, len);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);    // crc32(_, NULL, _) would reset it to 0
    put32(b, (uint32_t)crc);
    fwrite(b, 1, 4, f);
}

int png_write(const gfx_t *g, const char *path)
{
    size_t row = (size_t)g->stride + 1, raw_len = row * (size_t)g->height;
    uint8_t *raw = malloc(raw_len);
    uLongf z_len = compressBound((uLong)raw_len);
    uint8_t *z = malloc(z_len);
    if (!raw || !z) { free(raw); free(z); return 1; }
    for (int y = 0; y < g->height; y++) {
        raw[y * row] = 0;    // filter: none
        memcpy(raw + y * row + 1, g->buf + (size_t)y * (size_t)g->stride, (size_t)g->stride);
    }
    int st = compress2(z, &z_len, raw, (uLong)raw_len, 9);
    free(raw);
    FILE *f = st == Z_OK ? fopen(path, "wb") : NULL;
    if (!f) { if (st == Z_OK) perror(path); free(z); return 1; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)g->width);
    put32(ihdr + 4, (uint32_t)g->height);
    ihdr[8] = 1, ihdr[9] = 0, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;   // 1 bit gray, no interlace
    fwrite(sig, 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)z_len);
    chunk(f, "IEND", NULL, 0);
    free(z);
    return fclose(f) != 0;
}
