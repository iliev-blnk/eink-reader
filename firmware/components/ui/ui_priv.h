// Ce împart managerul și aplicațiile; nu face parte din API-ul pentru platforme.
#pragma once

#include "ibv.h"
#include "layout.h"
#include "pageidx.h"
#include "settings.h"
#include "sidecar.h"
#include "ui.h"

#define UI_W 240
#define UI_H 416
#define UI_STACK 6
#define UI_FOOTER 22            // px sub zona de text a lectorului, pentru subsol
#define UI_PATH 256

// O aplicație. Toate funcțiile sunt opționale în afară de draw.
typedef struct app {
    const char *name;
    void (*enter)(ui_t *ui);    // pusă în stivă
    void (*resume)(ui_t *ui);   // din nou deasupra, după ce s-a scos cea de peste ea
    void (*leave)(ui_t *ui);    // scoasă din stivă (sau totul se oprește, la somn)
    void (*key)(ui_t *ui, ui_key_t k, ui_press_t p);
    void (*draw)(ui_t *ui, gfx_t *g);
    bool (*idle)(ui_t *ui);     // lucru de fundal; true = mai e
} app_t;

// O listă cu selecție, folosită de lansator, bibliotecă, meniuri și setări.
// Fiecare element are o etichetă, opțional o valoare aliniată la dreapta și un
// al doilea rând, mai mic.
typedef struct {
    const char *title;
    int count, sel, top;
    bool two_line;
    void (*item)(ui_t *ui, int i, char *label, char *value, char *sub);   // fiecare buffer are 96 de octeți
} ui_list_t;

#define UI_ITEM 96

typedef struct {
    char rel[RECENT_PATH];      // calea față de rădăcina cardului
    char title[64], author[48];
    uint8_t id[8];
    bool bad;                   // nu se deschide: arătată „fișier deteriorat”
} lib_book_t;

struct ui {
    ui_platform_t p;
    gfx_t *g;
    uint8_t *shown;             // ce e acum pe panou, pentru reîmprospătarea parțială
    bool have_shown, want_full, dirty;
    int partials;               // reîmprospătări parțiale de la ultima completă
    const app_t *stack[UI_STACK];
    int depth;
    uint32_t last_key_ms;
    uint32_t last_battery_ms;
    int battery_mv;
    settings_t set;
    bool settings_dirty;
    recent_t recent;
    // Biblioteca, citită o dată pe pornire.
    lib_book_t *lib;
    int lib_count;
    bool lib_scanned;
    ui_list_t list[UI_STACK];   // starea listei fiecărui nivel din stivă
};

// Managerul.
void ui_push(ui_t *ui, const app_t *a);
void ui_pop(ui_t *ui);
// Scoate până când `a` e deasupra; false (și nimic scos) dacă nu e în stivă.
bool ui_pop_to(ui_t *ui, const app_t *a);
const app_t *ui_top(const ui_t *ui);
// Cere o redesenare; `full` cere și o reîmprospătare completă.
void ui_redraw(ui_t *ui, bool full);
ui_list_t *ui_my_list(ui_t *ui);    // lista nivelului din vârful stivei
void ui_perf(ui_t *ui, const char *event, uint32_t a, uint32_t b);
uint32_t ui_now(ui_t *ui);
void ui_save_recent(ui_t *ui);

// Desenarea comună.
void ui_header(ui_t *ui, gfx_t *g, const char *title);
void ui_text_fit(gfx_t *g, const font_t *f, int x, int y, const char *s, size_t len, int max_w);
// Copiază cât încape din `src`, tăiat la un caracter UTF-8 întreg.
void ui_copy(char *dst, size_t n, const char *src);
void ui_text_center(gfx_t *g, const font_t *f, int y, const char *s);
void ui_list_draw(ui_t *ui, gfx_t *g, ui_list_t *l);
// Mută selecția (ÎNAPOI/ÎNAINTE, ÎNAINTE lung = pagina următoare a listei).
// Întoarce true dacă a folosit tasta. ÎNAPOI lung (înapoi) și MENIU (alegerea)
// rămân aplicației.
bool ui_list_key(ui_list_t *l, ui_key_t k, ui_press_t p);
void ui_list_reset(ui_list_t *l, const char *title, int count, int sel, bool two_line,
                   void (*item)(ui_t *, int, char *, char *, char *));

// Biblioteca.
void library_scan(ui_t *ui);
// Cartea din bibliotecă cu acest id, sau -1.
int library_find(ui_t *ui, const uint8_t id[8]);

// Lectorul.
bool reader_open(ui_t *ui, const char *rel);   // false: cartea nu se deschide (eroarea e afișată)
void reader_close(ui_t *ui);
bool reader_is_open(void);
const ibv_t *reader_book(void);
void reader_save(ui_t *ui);                    // poziția în .pos și indexul, dacă s-a schimbat
// Rândurile lectorului pentru ecranul de diagnostic.
int reader_diag(char (*lines)[UI_DIAG_LINE], int max);

extern const app_t app_home, app_library, app_reader, app_reader_menu, app_toc, app_bookmarks, app_settings,
    app_diag;
