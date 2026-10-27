// Managerul de aplicații (FW-6) și aplicațiile cititorului (FW-10). C portabil,
// ca nucleul: pe dispozitiv îl conduce main.c, pe PC simulatorul (`sim ui`),
// care apasă butoane după un scenariu și scrie fiecare reîmprospătare ca PNG.
//
// O aplicație e un ecran: lansatorul, biblioteca, lectorul, meniul lectorului,
// cuprinsul, semnele de carte, setările, diagnosticul. Aplicațiile stau într-o
// stivă: deschiderea uneia o pune deasupra, „înapoi” o scoate. Fiecare are
// intrare, tastă, desenare, ieșire și, opțional, lucru de fundal.
//
// Desenarea e mereu completă, în framebuffer, iar managerul compară imaginea cu
// cea afișată ultima dată: zona care diferă se reîmprospătează parțial, deci
// aplicațiile nu urmăresc dreptunghiuri modificate. Din N reîmprospătări una e
// completă (setarea „reîmprospătare completă la N pagini”), ca să șteargă
// imaginile fantomă; deschiderea unei cărți și ecranul de somn sunt mereu complete.
//
// Totul rulează într-un singur task, taskul de interfață. Lucrul de fundal
// (indexul paginilor) se face între apăsări, câte puțin: ui_idle() întoarce
// după câteva pagini, iar platforma îl apelează din nou doar dacă nu a venit
// nicio apăsare. Așa cartea, cache-ul ei de blocuri și zona de lucru a
// paginării au un singur proprietar și nu trebuie nici încuietori, nici o a doua
// copie de ~140 KB pentru un task separat (vezi docs/README.md, 2026-10-27).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

typedef enum { UI_KEY_PREV, UI_KEY_NEXT, UI_KEY_MENU } ui_key_t;
typedef enum { UI_SHORT, UI_LONG } ui_press_t;
typedef enum { UI_REFRESH_PARTIAL, UI_REFRESH_FULL } ui_refresh_t;
typedef enum { UI_BOOT_COLD, UI_BOOT_WAKE } ui_boot_t;

#define UI_DIAG_LINE 48

// Ce îi dă platforma interfeței. Toate funcțiile primesc `ctx`.
typedef struct {
    const char *root;    // rădăcina cardului, fără '/' la final: "/sd" pe dispozitiv
    void *ctx;
    uint32_t (*now_ms)(void *ctx);
    // Arată framebufferul: complet (cu clipire) sau doar zona `r`. Se întoarce
    // după ce panoul a terminat.
    void (*show)(void *ctx, const gfx_t *g, ui_refresh_t kind, gfx_rect_t r);
    int (*battery_mv)(void *ctx);                        // -1 dacă nu se știe
    bool (*load_settings)(void *ctx, void *buf, size_t len);
    void (*save_settings)(void *ctx, const void *buf, size_t len);
    // Somnul adânc, după ce ecranul de somn e afișat. Pe dispozitiv nu se
    // întoarce (trezirea e o pornire nouă, cu UI_BOOT_WAKE).
    void (*sleep)(void *ctx);
    // Rândurile platformei pentru ecranul de diagnostic; întoarce câte a scris.
    int (*diag)(void *ctx, char (*lines)[UI_DIAG_LINE], int max);
    // Un marcaj de timp pentru jurnalul de măsurători (FW-13); poate fi NULL.
    void (*perf)(void *ctx, const char *event, uint32_t a, uint32_t b);
} ui_platform_t;

typedef struct ui ui_t;

// Starea interfeței e statică (o singură instanță); întoarce instanța.
ui_t *ui_init(const ui_platform_t *p, gfx_t *g);
// Primul ecran: la o pornire la rece ecranul de pornire, apoi lansatorul; la o
// trezire, cartea citită înainte de somn, la aceeași pagină, dacă există.
void ui_start(ui_t *ui, ui_boot_t boot);
void ui_key(ui_t *ui, ui_key_t key, ui_press_t press);
// Lucrul de fundal și temporizatoarele (somnul după inactivitate, bateria).
// Întoarce true dacă mai e lucru de fundal: platforma îl apelează din nou
// imediat ce nu așteaptă nicio apăsare.
bool ui_idle(ui_t *ui);
// Cât poate aștepta platforma o apăsare până la următorul ui_idle, în ms.
uint32_t ui_wait_ms(ui_t *ui);
// Salvează progresul și setările, arată ecranul de somn și cheamă platforma.
void ui_sleep(ui_t *ui);

// Procentul unei baterii LiPo după tensiunea ei în repaus (FW-8). Curba e una
// tipică; se calibrează după descărcarea măsurată în E5.
int ui_battery_pct(int mv);
