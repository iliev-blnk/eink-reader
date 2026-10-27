// inflate_raw() on the device: tinfl from the ESP32-S3 ROM (no flash cost).
#include "inflate.h"

#include <stdlib.h>

#include "miniz.h"

bool inflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t raw_len)
{
    // The decompressor state is about 11 KB: too big for a task stack.
    tinfl_decompressor *d = malloc(sizeof *d);
    if (!d) return false;
    tinfl_init(d);
    size_t in = src_len, out = raw_len;
    tinfl_status st = tinfl_decompress(d, src, &in, dst, dst, &out,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(d);
    return st == TINFL_STATUS_DONE && out == raw_len && in == src_len;
}
