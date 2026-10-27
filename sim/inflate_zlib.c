// inflate_raw() în simulator: zlib cu fereastră negativă (DEFLATE brut).
#include "inflate.h"

#include <zlib.h>

bool inflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t raw_len)
{
    z_stream z = {0};
    if (inflateInit2(&z, -15) != Z_OK) return false;
    z.next_in = (Bytef *)src;
    z.avail_in = (uInt)src_len;
    z.next_out = dst;
    z.avail_out = (uInt)raw_len;
    int st = inflate(&z, Z_FINISH);
    bool ok = st == Z_STREAM_END && z.total_out == raw_len && z.avail_in == 0;
    inflateEnd(&z);
    return ok;
}
