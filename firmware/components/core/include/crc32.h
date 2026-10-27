// CRC32 as in zlib and PNG (reflected, polynomial 0xEDB88320), used by the
// IBV and IBF formats.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Start with crc = 0; pass the result back in to continue over more data.
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
