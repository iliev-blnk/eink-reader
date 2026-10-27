// Book font choice (LAY-7): the family follows the book's language, the size
// follows one reader setting.
//
// Libron has no Cyrillic, so a book in a Cyrillic-script language gets
// PT Serif and every other book gets Libron. The reader never picks a family.
// The size setting is a step, and each step names a size per family that
// gives about the same text per page (matched on x-height and on page counts
// over the corpus), so changing the book doesn't change the reading size.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FONTSEL_STEPS 5
#define FONTSEL_DEFAULT_STEP 2

typedef struct {
    const char *family;          // file name prefix: "libron", "ptserif"
    uint8_t size;                // px
} fontsel_t;

// True if `lang` (BCP 47, e.g. "ru", "sr-Cyrl", "en-GB") is written in
// Cyrillic. A script subtag wins over the language; NULL or "" is Latin.
bool fontsel_cyrillic(const char *lang);

// The font for a book in `lang` at size `step` (clamped to 0..STEPS-1).
fontsel_t fontsel_pick(const char *lang, int step);

// "<dir>/<family>-<size>-<style>.ibf", style "r", "b", "i" or "bi". Returns
// false if it doesn't fit in `n`.
bool fontsel_path(char *buf, size_t n, const char *dir, fontsel_t f, const char *style);
