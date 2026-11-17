// Verificare exhaustivă pentru utf8_next_n(): toate cele 2^32 de secvențe de 4
// octeți, cu sfârșitul la 1, 2, 3 și 4 octeți. Pentru fiecare:
//   - consumă între 1 și câți octeți are voie, deci nu citește după `end`;
//   - dacă nu întoarce U+FFFD, octeții consumați sunt exact codificarea UTF-8
//     cea mai scurtă a rezultatului, care e un scalar Unicode (nu surogat);
// iar fiecare scalar Unicode, codificat, se decodifică înapoi la el însuși.
// Împreună: funcția decodifică UTF-8 valid corect și orice altceva dă U+FFFD.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "font.h"

static int encode(uint32_t cp, uint8_t *o)
{
    if (cp < 0x80) { o[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) { o[0] = (uint8_t)(0xC0 | cp >> 6); o[1] = (uint8_t)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (uint8_t)(0xE0 | cp >> 12); o[1] = (uint8_t)(0x80 | (cp >> 6 & 0x3F)); o[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (uint8_t)(0xF0 | cp >> 18); o[1] = (uint8_t)(0x80 | (cp >> 12 & 0x3F));
    o[2] = (uint8_t)(0x80 | (cp >> 6 & 0x3F)); o[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

static uint64_t errors;

static void fail(const uint8_t *b, int len, const char *why)
{
    if (errors++ < 10) printf("GREȘIT %02x %02x %02x %02x (lungimea %d): %s\n", b[0], b[1], b[2], b[3], len, why);
}

int main(void)
{
    uint8_t b[4], e[4];
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        int n = encode(cp, b);
        const char *s = (const char *)b;
        if (utf8_next_n(&s, (const char *)b + n) != cp || s != (const char *)b + n) fail(b, n, "scalar valid decodificat greșit");
    }
    uint64_t checked = 0;
    for (int len = 1; len <= 4; len++) {
        uint64_t total = 1ull << (8 * len);
        for (uint64_t v = 0; v < total; v++) {
            for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * (len - 1 - i)));
            const char *s = (const char *)b;
            uint32_t cp = utf8_next_n(&s, (const char *)b + len);
            int used = (int)(s - (const char *)b);
            checked++;
            if (used < 1 || used > len) { fail(b, len, "consumă în afara intervalului"); continue; }
            if (cp == 0xFFFD) continue;
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { fail(b, len, "nu e scalar"); continue; }
            if (encode(cp, e) != used || memcmp(e, b, (size_t)used)) fail(b, len, "nu e codificarea cea mai scurtă");
        }
    }
    printf("%llu secvențe și 1.112.064 de scalari verificați, %llu greșeli\n", (unsigned long long)checked,
           (unsigned long long)errors);
    return errors != 0;
}
