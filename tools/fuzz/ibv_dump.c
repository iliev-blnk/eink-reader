// Ce vede cititorul din firmware într-o carte IBV, scris pe rânduri de text ca
// să poată fi comparat cu validatorul de referință (conformanta.py).
//
//     ibv_dump <carte.ibv>
//
// Deschide cartea cu ibv_open (CRC-urile se verifică: construcția asta nu e
// cea de fuzzing), apoi scrie:
//
//     H <paragraph_count> <text_bytes> <book_id hex>
//     M <cheie> <valoare hex>              pentru cheile META 1..8 prezente
//     T <para> <nivel> <titlu hex>         pentru fiecare intrare din cuprins
//     C <lățime> <înălțime> <crc32 hex>    dacă ibv_cover dă o copertă
//     R <tip> <indicatori> <deplasament> <text hex> <stiluri>
//     E <para> <eroare>                    la prima înregistrare care nu se poate citi
//
// <deplasament> e ibv_text_offset(para, 0). <stiluri> sunt intervalele în care
// ibv_style_at dă același stil nenul, „început-sfârșit:stil” despărțite prin
// virgulă, sau „-” dacă tot textul e regular. Dacă deschiderea eșuează, scrie
// doar „X <eroare>”. Codul de ieșire e 0 în ambele cazuri; altceva înseamnă că
// cititorul a căzut (ASan, UBSan).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "ibv.h"

static void hex(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    if (!n) putchar('-');
    for (uint32_t i = 0; i < n; i++) printf("%02x", b[i]);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "folosire: ibv_dump <carte.ibv>\n");
        return 2;
    }
    ibv_t b;
    ibv_err_t err = ibv_open(&b, argv[1]);
    if (err != IBV_OK) {
        printf("X %d\n", err);
        return 0;
    }
    printf("H %u %u ", b.paragraph_count, b.text_bytes);
    hex(b.book_id, 8);
    putchar('\n');
    for (int k = 1; k < IBV_META_KEYS; k++) {
        if (!b.meta[k]) continue;
        printf("M %d ", k);
        hex(b.meta[k], (uint32_t)strlen(b.meta[k]));
        putchar('\n');
    }
    ibv_toc_t t;
    for (uint32_t i = 0; ibv_toc(&b, i, &t); i++) {
        printf("T %u %u ", t.para, t.level);
        hex(t.title, t.title_len);
        putchar('\n');
    }
    uint16_t w, h;
    uint8_t *bits;
    if (ibv_cover(&b, &w, &h, &bits)) {
        printf("C %u %u %08x\n", w, h, crc32_update(0, bits, (size_t)(w + 7) / 8 * h));
        free(bits);
    }
    for (uint32_t p = 0; p < b.paragraph_count; p++) {
        ibv_record_t r;
        uint32_t off;
        if ((err = ibv_record(&b, p, &r)) != IBV_OK || (err = ibv_text_offset(&b, p, 0, &off)) != IBV_OK) {
            printf("E %u %d\n", p, err);
            break;   // paragraph_count poate fi oricât de mare într-o carte stricată
        }
        ibv_record(&b, p, &r);   // ibv_text_offset poate scoate blocul din cache
        printf("R %u %u %u ", r.kind, r.flags, off);
        hex(r.text, r.text_len);
        putchar(' ');
        ibv_styles_t s;
        ibv_styles_init(&s, &r);
        uint32_t start = 0;
        uint8_t cur = 0;
        int any = 0;
        for (uint32_t i = 0; i <= r.text_len; i++) {
            uint8_t st = i < r.text_len ? ibv_style_at(&s, i) : 0;
            if (st == cur) continue;
            if (cur) printf("%s%u-%u:%u", any++ ? "," : "", start, i, cur);
            cur = st;
            start = i;
        }
        if (!any) putchar('-');
        putchar('\n');
    }
    ibv_close(&b);
    return 0;
}
