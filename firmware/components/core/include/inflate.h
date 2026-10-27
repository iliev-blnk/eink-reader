// DEFLATE brut (fără antet zlib), cum sunt stocate blocurile și copertele IBV.
// Două implementări în spatele aceluiași apel: tinfl din ROM-ul ESP32-S3
// (core/inflate_rom.c) și zlib în simulatorul de pe PC (sim/inflate_zlib.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Decomprimă src în dst, care trebuie să primească exact raw_len octeți.
// Întoarce false la date stricate sau la o lungime care nu se potrivește.
bool inflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t raw_len);
