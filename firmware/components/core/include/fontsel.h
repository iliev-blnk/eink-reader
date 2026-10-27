// Alegerea fontului cărții (LAY-7): familia urmează limba cărții, mărimea
// urmează o singură setare a cititorului.
//
// Libron nu are chirilice, deci o carte într-o limbă scrisă cu chirilice
// primește PT Serif, iar orice altă carte primește Libron. Cititorul nu alege
// niciodată familia. Setarea mărimii e o treaptă, iar fiecare treaptă dă câte o
// mărime pe familie cu cam același text pe pagină (potrivite după înălțimea x
// și după numărul de pagini pe corpus), deci schimbarea cărții nu schimbă
// mărimea de lectură.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FONTSEL_STEPS 5
#define FONTSEL_DEFAULT_STEP 2

typedef struct {
    const char *family;          // prefixul numelui de fișier: "libron", "ptserif"
    uint8_t size;                // px
} fontsel_t;

// True dacă `lang` (BCP 47, de exemplu "ru", "sr-Cyrl", "en-GB") se scrie cu
// chirilice. O subetichetă de scriere are întâietate față de limbă; NULL sau "" e latin.
bool fontsel_cyrillic(const char *lang);

// Fontul pentru o carte în `lang` la treapta `step` (limitată la 0..STEPS-1).
fontsel_t fontsel_pick(const char *lang, int step);

// "<dir>/<family>-<size>-<style>.ibf", stilul "r", "b", "i" sau "bi". Întoarce
// false dacă nu încape în `n`.
bool fontsel_path(char *buf, size_t n, const char *dir, fontsel_t f, const char *style);
