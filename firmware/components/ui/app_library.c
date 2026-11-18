// Biblioteca: cărțile `.ibv` din rădăcina cardului și din `/books`, cu titlul și
// autorul din META, cele deschise recent primele (în ordinea folosirii), apoi
// celelalte după titlu. Cardul se citește o dată pe pornire; fiecare carte
// costă antetul, directorul și META (ibv_open_meta), nu tot cuprinsul.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ui_priv.h"

#define LIB_MAX 200

static bool is_book(const char *name)
{
    size_t n = strlen(name);
    return name[0] != '.' && n > 4 && !strcasecmp(name + n - 4, ".ibv");
}

static void add_book(ui_t *ui, const char *rel)
{
    if (ui->lib_count == LIB_MAX) return;
    lib_book_t *b = &ui->lib[ui->lib_count];
    memset(b, 0, sizeof *b);
    ui_copy(b->rel, sizeof b->rel, rel);
    char path[UI_PATH];
    snprintf(path, sizeof path, "%s/%s", ui->p.root, rel);
    static ibv_t meta;
    if (ibv_open_meta(&meta, path) == IBV_OK) {
        const char *t = meta.meta[IBV_TITLE], *a = meta.meta[IBV_AUTHOR];
        ui_copy(b->title, sizeof b->title, t && *t ? t : rel);
        ui_copy(b->author, sizeof b->author, a ? a : "");
        memcpy(b->id, meta.book_id, 8);
        ibv_close(&meta);
    } else {
        const char *slash = strrchr(rel, '/');
        ui_copy(b->title, sizeof b->title, slash ? slash + 1 : rel);
        b->bad = true;
    }
    ui->lib_count++;
}

static void scan_dir(ui_t *ui, const char *sub)
{
    char path[UI_PATH], rel[RECENT_PATH];
    snprintf(path, sizeof path, "%s%s%s", ui->p.root, sub[0] ? "/" : "", sub);
    DIR *d = opendir(path);
    if (!d) return;
    size_t sl = strlen(sub);
    for (struct dirent *e; (e = readdir(d));) {
        size_t nl = strlen(e->d_name);
        if (!is_book(e->d_name) || sl + 1 + nl >= RECENT_PATH) continue;   // o cale prea lungă pentru `recent`
        size_t o = 0;
        if (sl) memcpy(rel, sub, sl), rel[sl] = '/', o = sl + 1;
        memcpy(rel + o, e->d_name, nl + 1);
        add_book(ui, rel);
    }
    closedir(d);
}

static ui_t *s_sort_ui;

// Recente primele, după ordinea din listă; apoi după titlu, fără a ține cont de
// majuscule (octet cu octet: literele cu diacritice vin după z).
static int cmp_books(const void *pa, const void *pb)
{
    const lib_book_t *a = pa, *b = pb;
    int ra = a->bad ? -1 : recent_find(&s_sort_ui->recent, a->id);
    int rb = b->bad ? -1 : recent_find(&s_sort_ui->recent, b->id);
    if (ra >= 0 || rb >= 0) {
        if (ra < 0) return 1;
        if (rb < 0) return -1;
        return ra - rb;
    }
    int c = strcasecmp(a->title, b->title);
    return c ? c : strcmp(a->rel, b->rel);
}

void library_scan(ui_t *ui)
{
    uint32_t t0 = ui_now(ui);
    if (!ui->lib) ui->lib = malloc(LIB_MAX * sizeof *ui->lib);
    ui->lib_count = 0;
    if (!ui->lib) return;
    scan_dir(ui, "");
    scan_dir(ui, "books");
    s_sort_ui = ui;
    qsort(ui->lib, (size_t)ui->lib_count, sizeof *ui->lib, cmp_books);
    ui->lib_scanned = true;
    ui_perf(ui, "library_scan", (uint32_t)ui->lib_count, ui_now(ui) - t0);
}

int library_find(ui_t *ui, const uint8_t id[8])
{
    for (int i = 0; i < ui->lib_count; i++)
        if (!ui->lib[i].bad && !memcmp(ui->lib[i].id, id, 8)) return i;
    return -1;
}

static void lib_item(ui_t *ui, int i, char *label, char *value, char *sub)
{
    const lib_book_t *b = &ui->lib[i];
    snprintf(label, UI_ITEM, "%s", b->title);
    if (b->bad) snprintf(sub, UI_ITEM, "fișier deteriorat");
    else snprintf(sub, UI_ITEM, "%s", b->author);
    if (!b->bad && ui->recent.count && !memcmp(ui->recent.book[0].id, b->id, 8)) strcpy(value, "•");
}

// Cardul, întrebat din nou (FW-4): un card care lipsea la pornire sau a fost
// scos între timp se montează aici, fără repornire. Un card care tocmai a
// revenit poate fi altul, deci se recitește și lista recentă.
static bool card_ok(ui_t *ui)
{
    bool was_missing = ui->card_missing;
    ui->card_missing = ui->p.card_ready && !ui->p.card_ready(ui->p.ctx);
    if (ui->card_missing) {
        ui->lib_scanned = false;
        ui->lib_count = 0;
    } else if (was_missing) {
        recent_load(ui->p.root, &ui->recent);
    }
    ui_perf(ui, "card", !ui->card_missing, was_missing);
    return !ui->card_missing;
}

static void lib_list(ui_t *ui)
{
    ui_list_t *l = ui_my_list(ui);
    ui_list_reset(l, "Biblioteca", ui->lib_count, 0, true, lib_item);
    if (ui->card_missing) l->empty = "Niciun card", l->empty_sub = "Pune cardul și intră din nou.";
    else if (!ui->lib_count) l->empty = "Nicio carte", l->empty_sub = "Copiază fișiere .ibv prin USB.";
}

static void lib_enter(ui_t *ui)
{
    // Sortarea ține cont de ce s-a deschis între timp, deci se refac ordinea și titlurile.
    if (card_ok(ui)) library_scan(ui);
    lib_list(ui);
}

static void lib_resume(ui_t *ui)
{
    s_sort_ui = ui;
    qsort(ui->lib, (size_t)ui->lib_count, sizeof *ui->lib, cmp_books);
    lib_list(ui);       // cartea tocmai citită e acum prima
}

static void lib_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    ui_list_t *l = ui_my_list(ui);
    if (ui_list_key(l, k, p)) {
        ui_redraw(ui, false);
    } else if (k == UI_KEY_PREV && p == UI_LONG) {
        ui_pop(ui);
    } else if (k == UI_KEY_MENU && l->count && !ui->lib[l->sel].bad) {
        if (reader_open(ui, ui->lib[l->sel].rel)) {
            ui_push(ui, &app_reader);
        } else if (!card_ok(ui)) {
            lib_list(ui);       // cardul a fost scos: nu e cartea de vină
            ui_redraw(ui, false);
        } else if (reader_open(ui, ui->lib[l->sel].rel)) {
            ui_push(ui, &app_reader);   // cardul tocmai a fost montat din nou
        } else {
            ui->lib[l->sel].bad = true;
            ui_redraw(ui, false);
        }
    }
}

static void lib_draw(ui_t *ui, gfx_t *g) { ui_list_draw(ui, g, ui_my_list(ui)); }

const app_t app_library = {
    .name = "bibliotecă", .enter = lib_enter, .resume = lib_resume, .key = lib_key, .draw = lib_draw,
};
