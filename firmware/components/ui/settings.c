#include "settings.h"

#include <string.h>

#include "crc32.h"
#include "fontsel.h"

#define VERSION 1

typedef struct {
    const char *name;
    uint8_t count, def;
    bool layout;
    int values[5];
    const char *labels[5];
} setting_def_t;

static const setting_def_t defs[SET_COUNT] = {
    [SET_SIZE] = {"Mărimea textului", FONTSEL_STEPS, FONTSEL_DEFAULT_STEP, true,
                  {0, 1, 2, 3, 4}, {"1", "2", "3", "4", "5"}},
    [SET_SPACING] = {"Spațierea", 3, 1, true, {0, 2, 4}, {"strânsă", "normală", "largă"}},
    [SET_MARGIN] = {"Marginile", 3, 1, true, {8, 12, 18}, {"înguste", "normale", "late"}},
    [SET_JUSTIFY] = {"Aliniere", 2, 1, true, {0, 1}, {"la stânga", "stânga-dreapta"}},
    [SET_HYPHENATE] = {"Despărțire", 2, 1, true, {0, 1}, {"oprită", "pornită"}},
    [SET_FULL_EVERY] = {"Curățare completă", 4, 2, false, {1, 5, 10, 20},
                        {"la fiecare pagină", "la 5 pagini", "la 10 pagini", "la 20 de pagini"}},
    [SET_SLEEP] = {"Somn după", 4, 1, false, {2, 5, 10, 30}, {"2 min", "5 min", "10 min", "30 min"}},
};

void settings_default(settings_t *s)
{
    for (int k = 0; k < SET_COUNT; k++) s->v[k] = defs[k].def;
}

const char *settings_name(int key) { return defs[key].name; }
int settings_options(int key) { return defs[key].count; }
const char *settings_option_name(int key, int i) { return defs[key].labels[i]; }
bool settings_affects_layout(int key) { return defs[key].layout; }

int settings_value(const settings_t *s, int key)
{
    uint8_t i = s->v[key] < defs[key].count ? s->v[key] : defs[key].def;
    return defs[key].values[i];
}

void settings_cycle(settings_t *s, int key)
{
    s->v[key] = (uint8_t)((s->v[key] + 1) % defs[key].count);
}

void settings_pack(const settings_t *s, uint8_t out[SETTINGS_BLOB])
{
    memset(out, 0, SETTINGS_BLOB);
    out[0] = VERSION;
    memcpy(out + 1, s->v, SET_COUNT);
    uint32_t c = crc32_update(0, out, SETTINGS_BLOB - 4);
    for (int i = 0; i < 4; i++) out[SETTINGS_BLOB - 4 + i] = (uint8_t)(c >> (8 * i));
}

bool settings_unpack(settings_t *s, const uint8_t in[SETTINGS_BLOB])
{
    settings_default(s);
    uint32_t c = 0;
    for (int i = 0; i < 4; i++) c |= (uint32_t)in[SETTINGS_BLOB - 4 + i] << (8 * i);
    if (in[0] != VERSION || crc32_update(0, in, SETTINGS_BLOB - 4) != c) return false;
    for (int k = 0; k < SET_COUNT; k++)
        if (in[1 + k] < defs[k].count) s->v[k] = in[1 + k];
    return true;
}
