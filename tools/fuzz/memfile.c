#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "memfile.h"

#undef fopen

static uint8_t *file_data;
static size_t file_len;
static bool file_set;

void fuzz_set_file(const uint8_t *data, size_t len)
{
    free(file_data);
    file_data = NULL;
    file_len = 0;
    file_set = data != NULL;
    if (!data) return;
    // O copie de exact `len` octeți: ASan prinde orice citire de după ea.
    file_data = malloc(len ? len : 1);
    if (len) memcpy(file_data, data, len);
    file_len = len;
}

FILE *fuzz_fopen(const char *path, const char *mode)
{
    if (strncmp(path, "/fuzz/", 6) != 0 || mode[0] != 'r' || !file_set) return NULL;
    if (file_len == 0) return fopen("/dev/null", "rb");   // fmemopen nu primește lungimea 0
    return fmemopen(file_data, file_len, "rb");
}

uint8_t *fuzz_read_real(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) free(d), d = NULL;
    fclose(f);
    *len = d ? (size_t)n : 0;
    return d;
}
