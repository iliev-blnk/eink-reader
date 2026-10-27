#include "crc32.h"

static uint32_t table[256];

static void make_table(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    if (table[1] == 0) make_table();
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
