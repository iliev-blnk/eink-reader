// Scriitor PNG minimal pentru framebufferele de 1 bit (zlib pentru DEFLATE și CRC).
#pragma once

#include "gfx.h"

// Tonuri de gri, 1 bit pe pixel: formatul panoului (bitul 1 = alb, MSB primul)
// e chiar al PNG-ului, deci rândurile se scriu cum sunt.
int png_write(const gfx_t *g, const char *path);
