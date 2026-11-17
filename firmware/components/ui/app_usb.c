// „Conectare la PC” (FW-12): cardul devine disc USB, calculatorul copiază
// cărțile, apoi cititorul ia cardul înapoi și recitește biblioteca.
//
// Cât timp ecranul e deschis, cardul e al calculatorului (vezi
// ui_platform_t.usb_start): interfața nu atinge cardul, nu adoarme de la sine,
// iar MENIU lung nu mai înseamnă somn. Ecranul se închide singur când
// calculatorul scoate discul în siguranță sau cablul e scos (cu senzor VBUS);
// MENIU îl închide oricând, cu o confirmare dacă discul e încă montat pe
// calculator.
#include <stdio.h>
#include <string.h>

#include "ui_priv.h"

#define POLL_MS 250u

static struct {
    bool on;            // usb_start a reușit; cardul e al calculatorului
    bool confirm;       // MENIU apăsat o dată cu discul încă montat pe calculator
    ui_usb_t state;
    uint32_t next_poll;
} U;

static void usb_enter(ui_t *ui)
{
    memset(&U, 0, sizeof U);
    ui_perf(ui, "usb_start", 0, 0);
    U.on = ui->p.usb_start && ui->p.usb_start(ui->p.ctx);
    if (U.on) U.state = ui->p.usb_state(ui->p.ctx);
    U.next_poll = ui_now(ui) + POLL_MS;
}

static void usb_leave(ui_t *ui)
{
    if (!U.on) return;
    U.on = false;
    ui->p.usb_stop(ui->p.ctx);
    ui_perf(ui, "usb_stop", U.state, 0);
    // Calculatorul a putut schimba orice: cărțile, pozițiile, lista recentă.
    recent_load(ui->p.root, &ui->recent);
    ui->lib_scanned = false;
    ui->lib_count = 0;
}

static void usb_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    (void)p;
    if (k != UI_KEY_MENU) {
        if (U.confirm) {
            U.confirm = false;
            ui_redraw(ui, false);
        }
        return;
    }
    if (U.on && U.state == UI_USB_HOST && !U.confirm) {
        U.confirm = true;
        ui_redraw(ui, false);
        return;
    }
    ui_pop(ui);
}

bool usb_poll(ui_t *ui)
{
    if (!U.on) return false;
    uint32_t now = ui_now(ui);
    if ((int32_t)(now - U.next_poll) < 0) return false;
    U.next_poll = now + POLL_MS;
    ui_usb_t s = ui->p.usb_state(ui->p.ctx);
    if (s == U.state) return false;
    ui_perf(ui, "usb_state", s, U.state);
    U.state = s;
    U.confirm = false;
    if (s == UI_USB_RELEASED) {
        ui_pop(ui);         // scos în siguranță: înapoi la cărți, fără nicio apăsare
        return false;
    }
    ui_redraw(ui, false);
    return false;
}

uint32_t usb_wait_ms(ui_t *ui)
{
    if (!U.on) return UINT32_MAX;
    uint32_t now = ui_now(ui);
    return (int32_t)(U.next_poll - now) < 0 ? 0 : U.next_poll - now;
}

bool usb_active(void) { return U.on; }

// Rândurile pe lățimea ecranului, rupte între cuvinte. Întoarce y-ul de după.
static int para(gfx_t *g, const font_t *f, int y, const char *s)
{
    const int x = 12, w = UI_W - 2 * x;
    char line[UI_ITEM];
    while (*s) {
        size_t n = 0, fit = 0;
        for (;;) {
            size_t e = n;
            while (s[e] && s[e] != ' ') e++;
            if (e >= sizeof line) break;
            memcpy(line, s, e);
            line[e] = 0;
            if (fit && gfx_text_width(f, line) > w) break;
            fit = e;
            if (!s[e]) break;
            n = e + 1;
        }
        if (!fit) fit = strlen(s) < sizeof line - 1 ? strlen(s) : sizeof line - 1;
        memcpy(line, s, fit);
        line[fit] = 0;
        gfx_text(g, f, x, y, line, GFX_BLACK);
        y += f->line_height;
        s += fit;
        while (*s == ' ') s++;
    }
    return y;
}

static void usb_draw(ui_t *ui, gfx_t *g)
{
    const font_t *big = &font_ui16, *small = &font_ui12;
    ui_header(ui, g, "Conectare la PC");
    int y = HEADER_H + 24 + big->ascent;
    const char *title, *body, *hint;
    if (!U.on) {
        title = "Cardul nu pornește";
        body = "Verifică dacă e cardul în cititor. Fără card, calculatorul nu are ce vedea.";
        hint = "MENIU: înapoi";
    } else if (U.confirm) {
        title = "Calculatorul încă folosește cardul";
        body = "Scoate întâi discul în siguranță din calculator (Eject). Dacă ieși acum, fișierele "
               "copiate pe jumătate se pot strica.";
        hint = "MENIU: ieși oricum · ÎNAINTE: rămâi";
    } else {
        switch (U.state) {
        case UI_USB_WAITING:
            title = "Conectează cablul";
            body = "Leagă cititorul de calculator cu un cablu USB. Cardul apare acolo ca un stick.";
            hint = "MENIU: înapoi";
            break;
        case UI_USB_PAUSED:
            title = "Calculatorul nu răspunde";
            body = "Cablul e scos sau calculatorul doarme. Dacă ai terminat de copiat, poți ieși.";
            hint = "MENIU: înapoi la cărți";
            break;
        default:
            title = "Cardul e deschis pe calculator";
            body = "Copiază cărțile (.ibv) în dosarul books. La sfârșit scoate discul în siguranță "
                   "(Eject): cititorul revine singur la cărți.";
            hint = "MENIU: ieși";
            break;
        }
    }
    y = para(g, big, y, title);
    para(g, small, y + 10, body);
    gfx_hline(g, 0, UI_H - LIST_HINT_H, UI_W, GFX_BLACK);
    gfx_text(g, small, 8, UI_H - 6, hint, GFX_BLACK);
}

const app_t app_usb = {
    .name = "usb", .enter = usb_enter, .leave = usb_leave, .key = usb_key, .draw = usb_draw, .idle = usb_poll,
};
