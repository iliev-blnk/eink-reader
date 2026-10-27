// Raw DEFLATE (no zlib header), as IBV blocks and covers are stored.
// Two implementations behind one call: the ESP32-S3 ROM's tinfl
// (core/inflate_rom.c) and zlib in the PC simulator (sim/inflate_zlib.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Inflates src into dst, which must receive exactly raw_len bytes.
// Returns false on corrupt input or a length mismatch.
bool inflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t raw_len);
