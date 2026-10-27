// Setările cititorului (FW-9). Pe dispozitiv se țin în NVS, în simulator într-un
// fișier; aici sunt doar valorile, implicitele și forma lor ca octeți.
//
// Fiecare setare e un indice într-o listă scurtă de variante, ca setările să
// se schimbe cu un singur buton (MENIU trece la varianta următoare).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SET_SIZE,          // treapta mărimii textului (fontsel.h)
    SET_SPACING,       // spațierea rândurilor
    SET_MARGIN,
    SET_JUSTIFY,
    SET_HYPHENATE,
    SET_FULL_EVERY,    // o reîmprospătare completă la N pagini
    SET_SLEEP,         // minute de inactivitate până la somn
    SET_COUNT
};

typedef struct {
    uint8_t v[SET_COUNT];   // indicele variantei alese
} settings_t;

#define SETTINGS_BLOB 12    // octeți: versiunea, valorile, CRC32

void settings_default(settings_t *s);
// Numele setării, numărul de variante și numele variantei `i`.
const char *settings_name(int key);
int settings_options(int key);
const char *settings_option_name(int key, int i);
// Valoarea variantei alese, în unitatea setării (px, pagini, minute, 0/1).
int settings_value(const settings_t *s, int key);
// Trece la varianta următoare (după ultima vine prima).
void settings_cycle(settings_t *s, int key);
// True dacă setarea schimbă unde se rup paginile (deci și indexul paginilor).
bool settings_affects_layout(int key);

void settings_pack(const settings_t *s, uint8_t out[SETTINGS_BLOB]);
// False (și implicitele în *s) la o versiune sau un CRC greșit.
bool settings_unpack(settings_t *s, const uint8_t in[SETTINGS_BLOB]);
