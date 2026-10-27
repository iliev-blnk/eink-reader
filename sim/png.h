// Minimal PNG writer for 1-bit framebuffers (zlib for DEFLATE and CRC).
#pragma once

#include "gfx.h"

// Grayscale, 1 bit per pixel: the panel's format (bit 1 = white, MSB first)
// is PNG's, so rows are written as they are.
int png_write(const gfx_t *g, const char *path);
