// CRC32 ca în zlib și PNG (reflectat, polinomul 0xEDB88320), folosit de
// formatele IBV și IBF.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Se începe cu crc = 0; rezultatul se dă înapoi ca să se continue peste alte date.
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

// Compară CRC-ul calculat cu cel citit dintr-un fișier. În construcția pentru
// fuzzing (tools/fuzz) trece mereu, ca fuzzerul să ajungă la ce e după
// verificare: un CRC nu apără de un fișier făcut anume, doar de unul stricat.
static inline bool crc32_ok(uint32_t computed, uint32_t stored)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    (void)computed, (void)stored;
    return true;
#else
    return computed == stored;
#endif
}
