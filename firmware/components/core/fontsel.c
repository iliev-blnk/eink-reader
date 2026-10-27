#include "fontsel.h"

#include <stdio.h>
#include <string.h>

// Page counts for Pride and Prejudice per step, Libron / PT Serif:
// 1541 / 1601, 1845 / 1803, 2081 / 2042, 2370 / 2302, 2844 / 2753.
static const uint8_t LIBRON[FONTSEL_STEPS] = {15, 16, 17, 18, 20};
static const uint8_t PTSERIF[FONTSEL_STEPS] = {16, 17, 18, 19, 21};

// Languages written in Cyrillic by default. Serbian is written in both;
// "sr" alone is Cyrillic, "sr-Latn" is Latin.
static const char *const CYRILLIC[] = {"ba", "be", "bg", "ce", "cv", "kk", "ky", "mk", "mn",
                                       "os", "ru", "sah", "sr", "tg", "tt", "uk"};

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive compare of a subtag [s, s+n) with a lowercase word.
static bool tag_is(const char *s, size_t n, const char *word)
{
    if (strlen(word) != n) return false;
    for (size_t i = 0; i < n; i++)
        if (lower(s[i]) != word[i]) return false;
    return true;
}

bool fontsel_cyrillic(const char *lang)
{
    if (!lang || !*lang) return false;
    size_t n = strcspn(lang, "-_");
    // A script subtag ("sr-Latn", "ro-Cyrl") is four letters, anywhere after
    // the language.
    for (const char *s = lang + n; *s;) {
        s++;
        size_t k = strcspn(s, "-_");
        if (tag_is(s, k, "cyrl")) return true;
        if (tag_is(s, k, "latn")) return false;
        s += k;
    }
    for (size_t i = 0; i < sizeof CYRILLIC / sizeof *CYRILLIC; i++)
        if (tag_is(lang, n, CYRILLIC[i])) return true;
    return false;
}

fontsel_t fontsel_pick(const char *lang, int step)
{
    if (step < 0) step = 0;
    if (step >= FONTSEL_STEPS) step = FONTSEL_STEPS - 1;
    if (fontsel_cyrillic(lang)) return (fontsel_t){"ptserif", PTSERIF[step]};
    return (fontsel_t){"libron", LIBRON[step]};
}

bool fontsel_path(char *buf, size_t n, const char *dir, fontsel_t f, const char *style)
{
    int len = snprintf(buf, n, "%s/%s-%u-%s.ibf", dir, f.family, (unsigned)f.size, style);
    return len >= 0 && (size_t)len < n;
}
