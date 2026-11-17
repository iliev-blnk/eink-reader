// Inclus forțat (-include) în sursele firmware-ului când se compilează pentru
// fuzzing: fopen() pe o cale care începe cu "/fuzz/" deschide intrarea curentă
// a fuzzerului din memorie (fmemopen), orice altă cale nu se deschide. Codul
// testat rămâne neschimbat.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

FILE *fuzz_fopen(const char *path, const char *mode);
#define fopen fuzz_fopen

// Fișierul de sub "/fuzz/": datele și lungimea lor. NULL: nu există.
void fuzz_set_file(const uint8_t *data, size_t len);

// Citește un fișier adevărat (pentru datele fixe ale unui harness), sau NULL.
uint8_t *fuzz_read_real(const char *path, size_t *len);
