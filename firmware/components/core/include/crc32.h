// CRC32 ca în zlib și PNG (reflectat, polinomul 0xEDB88320), folosit de
// formatele IBV și IBF.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Se începe cu crc = 0; rezultatul se dă înapoi ca să se continue peste alte date.
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
