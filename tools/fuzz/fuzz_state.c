// Fuzzing pentru fișierele mici pe care dispozitivul le scrie singur pe card
// și le citește înapoi: indexul de pagini (.pg), poziția (.pos), semnele de
// carte (.bm), lista cărților recente (recent) și blobul setărilor. Pe card
// pot ajunge stricate (o cădere a curentului) sau puse de altcineva.
// Primul octet alege fișierul, restul e conținutul lui.
#include <string.h>

#include "pageidx.h"
#include "settings.h"
#include "sidecar.h"
#include "memfile.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return 0;
    uint8_t which = data[0] % 5;
    data++, size--;
    fuzz_set_file(data, size);
    // Identitatea cărții se ia din fișier, ca verificările de după ea să poată trece.
    uint8_t id[8] = {0};
    if (size >= 12) memcpy(id, data + 4, 8);
    switch (which) {
    case 0: {
        pageidx_t x;
        pageidx_init(&x, 0);
        uint32_t hash = size >= 16 ? (uint32_t)(data[12] | data[13] << 8 | data[14] << 16 | (uint32_t)data[15] << 24) : 0;
        if (pageidx_load(&x, id, hash, "/fuzz/carte.pg")) {
            for (uint32_t i = 0; i < x.count; i++) pageidx_find(&x, x.starts[i]);
            pageidx_find(&x, (layout_pos_t){UINT32_MAX, UINT32_MAX});
            pageidx_find(&x, x.next);
        }
        pageidx_free(&x);
        break;
    }
    case 1: {
        layout_pos_t p;
        pos_load("/fuzz", id, &p);
        break;
    }
    case 2: {
        bookmarks_t bm;
        bm_load("/fuzz", id, &bm);
        bm_find(&bm, (layout_pos_t){0, 0}, (layout_pos_t){UINT32_MAX, UINT32_MAX});
        bm_add(&bm, (layout_pos_t){1, 1});
        bm_remove(&bm, 0);
        break;
    }
    case 3: {
        recent_t r;
        recent_load("/fuzz", &r);
        for (int i = 0; i < r.count; i++) {
            volatile size_t n = strlen(r.book[i].path);
            (void)n;
        }
        recent_touch(&r, id, r.count ? r.book[r.count - 1].path : "x.ibv");
        break;
    }
    case 4: {
        uint8_t blob[SETTINGS_BLOB] = {0};
        memcpy(blob, data, size < SETTINGS_BLOB ? size : SETTINGS_BLOB);
        settings_t s;
        if (settings_unpack(&s, blob))
            for (int k = 0; k < SET_COUNT; k++) settings_value(&s, k);
        break;
    }
    }
    fuzz_set_file(NULL, 0);
    return 0;
}
