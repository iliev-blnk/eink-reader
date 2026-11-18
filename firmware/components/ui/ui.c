// Managerul de aplicații: stiva de ecrane, politica de reîmprospătare,
// temporizatoarele (somnul, bateria) și desenarea comună (antetul, listele).
// Tot aici: lansatorul, diagnosticul, ecranele de pornire, de somn și de
// baterie descărcată.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_priv.h"

#define BATTERY_EVERY_MS 60000u
#define LOW_MV 3400             // sub atât (sub sarcină) se oprește, ca LiPo să nu se descarce adânc
#define NO_BATTERY_MV 2500      // sub atât nu e baterie (alimentat pe USB): nu se oprește
#define BATTERY_JUMP_MV 200     // o diferență mai mare (încărcător pus sau scos) nu se netezește
#define UI_VERSION "0.1"

static ui_t s_ui;
static uint8_t s_shown[GFX_BUF_SIZE(UI_W, UI_H)];

// ---- managerul ----

uint32_t ui_now(ui_t *ui) { return ui->p.now_ms(ui->p.ctx); }

void ui_perf(ui_t *ui, const char *event, uint32_t a, uint32_t b)
{
    if (ui->p.perf) ui->p.perf(ui->p.ctx, event, a, b);
}

const app_t *ui_top(const ui_t *ui) { return ui->depth ? ui->stack[ui->depth - 1] : NULL; }
ui_list_t *ui_my_list(ui_t *ui) { return &ui->list[ui->depth ? ui->depth - 1 : 0]; }

void ui_redraw(ui_t *ui, bool full)
{
    ui->dirty = true;
    ui->want_full |= full;
}

void ui_push(ui_t *ui, const app_t *a)
{
    if (ui->depth == UI_STACK) return;
    ui->stack[ui->depth++] = a;
    memset(ui_my_list(ui), 0, sizeof(ui_list_t));
    if (a->enter) a->enter(ui);
    ui_redraw(ui, false);
}

void ui_pop(ui_t *ui)
{
    if (ui->depth <= 1) return;   // lansatorul rămâne mereu la bază
    const app_t *a = ui->stack[--ui->depth];
    if (a->leave) a->leave(ui);
    const app_t *t = ui_top(ui);
    if (t->resume) t->resume(ui);
    ui_redraw(ui, false);
}

bool ui_pop_to(ui_t *ui, const app_t *a)
{
    int i = ui->depth - 1;
    while (i >= 0 && ui->stack[i] != a) i--;
    if (i < 0) return false;
    while (ui->depth > i + 1) {
        const app_t *t = ui->stack[--ui->depth];
        if (t->leave) t->leave(ui);
    }
    if (a->resume) a->resume(ui);
    ui_redraw(ui, false);
    return true;
}

static void leave_all(ui_t *ui)
{
    while (ui->depth > 0) {
        const app_t *t = ui->stack[--ui->depth];
        if (t->leave) t->leave(ui);
    }
}

// Zona în care framebufferul diferă de ce e pe panou. False dacă nu diferă nimic.
static bool diff_rect(const ui_t *ui, gfx_rect_t *r)
{
    const gfx_t *g = ui->g;
    int y0 = -1, y1 = -1, b0 = g->stride, b1 = -1;
    for (int y = 0; y < g->height; y++) {
        const uint8_t *a = g->buf + y * g->stride, *b = s_shown + y * g->stride;
        if (!memcmp(a, b, (size_t)g->stride)) continue;
        if (y0 < 0) y0 = y;
        y1 = y;
        int l = 0, rr = g->stride - 1;
        while (a[l] == b[l]) l++;
        while (a[rr] == b[rr]) rr--;
        if (l < b0) b0 = l;
        if (rr > b1) b1 = rr;
    }
    if (y0 < 0) return false;
    *r = (gfx_rect_t){b0 * 8, y0, (b1 - b0 + 1) * 8, y1 - y0 + 1};
    return true;
}

static void show(ui_t *ui, bool full)
{
    gfx_rect_t r = {0, 0, ui->g->width, ui->g->height};
    gfx_rect_t ignored;
    gfx_take_dirty(ui->g, &ignored);
    if (!full && ui->have_shown && !diff_rect(ui, &r)) return;
    int every = settings_value(&ui->set, SET_FULL_EVERY);
    if (!ui->have_shown || ui->partials + 1 >= every) full = true;
    ui->partials = full ? 0 : ui->partials + 1;
    ui->p.show(ui->p.ctx, ui->g, full ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL, r);
    memcpy(s_shown, ui->g->buf, sizeof s_shown);
    ui->have_shown = true;
}

static void flush(ui_t *ui)
{
    if (!ui->dirty || !ui->depth) return;
    gfx_clear(ui->g, GFX_WHITE);
    ui_top(ui)->draw(ui, ui->g);
    bool full = ui->want_full;
    ui->dirty = ui->want_full = false;
    show(ui, full);
}

void ui_save_recent(ui_t *ui) { recent_save(ui->p.root, &ui->recent); }

static void load_settings(ui_t *ui)
{
    uint8_t blob[SETTINGS_BLOB];
    if (!ui->p.load_settings || !ui->p.load_settings(ui->p.ctx, blob, sizeof blob)
        || !settings_unpack(&ui->set, blob))
        settings_default(&ui->set);
}

static void save_settings(ui_t *ui)
{
    if (!ui->settings_dirty || !ui->p.save_settings) return;
    uint8_t blob[SETTINGS_BLOB];
    settings_pack(&ui->set, blob);
    ui->p.save_settings(ui->p.ctx, blob, sizeof blob);
    ui->settings_dirty = false;
}

// O citire a bateriei (FW-8). Oprirea la baterie descărcată folosește citirea
// așa cum e; procentul din bara de sus vine dintr-o medie exponențială cu
// ponderea 1/4, ca să nu sară înainte și înapoi între două citiri (ADC-ul are
// zgomot de câțiva mV, iar curba LiPo e plată la mijloc: 10 % înseamnă 30–40 mV).
// După un salt mare media o ia de la capăt: altfel, cu o citire pe minut, ar
// rămâne câteva minute în urma încărcătorului pus sau scos.
static void battery_sample(ui_t *ui)
{
    int mv = ui->p.battery_mv ? ui->p.battery_mv(ui->p.ctx) : -1;
    ui->battery_mv = mv;
    if (mv <= NO_BATTERY_MV) ui->battery_avg16 = 0;
    else if (!ui->battery_avg16 || abs(mv * 16 - ui->battery_avg16) > BATTERY_JUMP_MV * 16) ui->battery_avg16 = mv * 16;
    else ui->battery_avg16 += (mv * 16 - ui->battery_avg16) / 4;
}

// Tensiunea afișată (netezită), sau -1 fără baterie.
static int battery_shown_mv(const ui_t *ui) { return ui->battery_avg16 ? (ui->battery_avg16 + 8) / 16 : -1; }

ui_t *ui_init(const ui_platform_t *p, gfx_t *g)
{
    ui_t *ui = &s_ui;
    free(ui->lib);
    memset(ui, 0, sizeof *ui);
    ui->p = *p;
    ui->g = g;
    load_settings(ui);
    recent_load(p->root, &ui->recent);
    battery_sample(ui);
    ui->last_key_ms = ui->last_battery_ms = ui_now(ui);
    return ui;
}

// ---- ecranele fără aplicație ----

static void draw_boot(ui_t *ui, gfx_t *g)
{
    const font_t *f = &font_ui16;
    gfx_clear(g, GFX_WHITE);
    int y = UI_H / 2 - f->line_height;
    gfx_fill_rect(g, UI_W / 2 - 30, y - 70, 60, 44, GFX_BLACK);   // o carte închisă, desenată simplu
    gfx_fill_rect(g, UI_W / 2 - 26, y - 66, 52, 36, GFX_WHITE);
    gfx_hline(g, UI_W / 2 - 18, y - 56, 36, GFX_BLACK);
    gfx_hline(g, UI_W / 2 - 18, y - 48, 36, GFX_BLACK);
    gfx_hline(g, UI_W / 2 - 18, y - 40, 24, GFX_BLACK);
    ui_text_center(g, f, y, "Cititor e-ink");
    ui_text_center(g, &font_ui12, y + f->line_height, "versiunea " UI_VERSION);
    (void)ui;
}

static bool draw_cover(gfx_t *g, ibv_t *book)
{
    uint16_t w, h;
    uint8_t *bits;
    if (!ibv_cover(book, &w, &h, &bits)) return false;
    int x0 = (UI_W - w) / 2, y0 = (UI_H - h) / 2, stride = (w + 7) / 8;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (!(bits[y * stride + x / 8] & (0x80 >> (x % 8)))) gfx_pixel(g, x0 + x, y0 + y, GFX_BLACK);
    free(bits);
    return true;
}

// Ecranul de somn: coperta cărții citite, sau titlul ei, sau doar un îndemn.
// Rămâne pe panou cât doarme dispozitivul, fără curent.
static void draw_sleep(ui_t *ui, gfx_t *g)
{
    const font_t *f = &font_ui16;
    gfx_clear(g, GFX_WHITE);
    static ibv_t meta;
    const ibv_t *b = reader_book();
    bool own = false;
    if (!b && ui->recent.count) {
        char path[UI_PATH];
        snprintf(path, sizeof path, "%s/%s", ui->p.root, ui->recent.book[0].path);
        if (ibv_open_meta(&meta, path) == IBV_OK) b = &meta, own = true;
    }
    if (b && draw_cover(g, (ibv_t *)b)) {
        if (own) ibv_close(&meta);
        return;
    }
    int y = UI_H / 2 - 2 * f->line_height;
    if (b && b->meta[IBV_TITLE]) {
        const char *t = b->meta[IBV_TITLE], *a = b->meta[IBV_AUTHOR];
        ui_text_fit(g, f, 16, y, t, strlen(t), UI_W - 32);
        if (a) ui_text_fit(g, &font_ui12, 16, y + f->line_height, a, strlen(a), UI_W - 32);
        gfx_hline(g, 16, y + 2 * f->line_height, 40, GFX_BLACK);
        y += 4 * f->line_height;
    }
    ui_text_center(g, f, y, "Doarme");
    ui_text_center(g, &font_ui12, y + f->line_height, "Apasă orice buton");
    if (own) ibv_close(&meta);
}

static void draw_low_battery(gfx_t *g, int mv)
{
    const font_t *f = &font_ui16;
    char buf[48];
    gfx_clear(g, GFX_WHITE);
    int x = UI_W / 2 - 30, y = UI_H / 2 - 70;
    gfx_rect(g, x, y, 56, 28, GFX_BLACK);           // o baterie goală
    gfx_rect(g, x + 1, y + 1, 54, 26, GFX_BLACK);
    gfx_fill_rect(g, x + 56, y + 8, 5, 12, GFX_BLACK);
    gfx_fill_rect(g, x + 4, y + 4, 6, 20, GFX_BLACK);
    ui_text_center(g, f, UI_H / 2, "Baterie descărcată");
    snprintf(buf, sizeof buf, "%d,%02d V · încarcă dispozitivul", mv / 1000, mv % 1000 / 10);
    ui_text_center(g, &font_ui12, UI_H / 2 + f->line_height, buf);
}

static void sleep_now(ui_t *ui, void (*screen)(ui_t *, gfx_t *))
{
    // Întâi cardul înapoi de la calculator, ca setările și poziția să ajungă pe el.
    if (ui_top(ui) == &app_usb) ui_pop(ui);
    ui_perf(ui, "sleep", 0, 0);
    save_settings(ui);
    ui->recent.reading = reader_is_open();
    reader_save(ui);
    if (reader_is_open()) ui_save_recent(ui);
    if (screen) screen(ui, ui->g);
    show(ui, true);
    leave_all(ui);     // închide cartea; trezirea e o pornire nouă
    ui->p.sleep(ui->p.ctx);
}

void ui_sleep(ui_t *ui) { sleep_now(ui, draw_sleep); }

static void low_battery(ui_t *ui, gfx_t *g) { draw_low_battery(g, ui->battery_mv); }

// ---- ciclul ----

static bool battery_low(const ui_t *ui) { return ui->battery_mv > NO_BATTERY_MV && ui->battery_mv < LOW_MV; }

// Se întoarce doar cu lectorul deasupra: în meniu sau în cuprins pagina stă.
static bool auto_due(const ui_t *ui, uint32_t now)
{
    return ui->auto_ms && ui_top(ui) == &app_reader && (int32_t)(now - ui->auto_at) >= 0;
}

void ui_start(ui_t *ui, ui_boot_t boot)
{
    if (battery_low(ui)) {
        // Fără să deschidă nimic; `recent` rămâne cum era, deci după încărcare cartea se redeschide.
        draw_low_battery(ui->g, ui->battery_mv);
        show(ui, true);
        ui->p.sleep(ui->p.ctx);
        return;
    }
    ui_push(ui, &app_home);
    if (boot == UI_BOOT_WAKE && ui->recent.reading && reader_open(ui, ui->recent.book[0].path)) {
        ui_push(ui, &app_reader);
        ui_redraw(ui, true);
        flush(ui);
        return;
    }
    if (boot == UI_BOOT_COLD) {
        draw_boot(ui, ui->g);
        show(ui, true);
    }
    ui_redraw(ui, boot == UI_BOOT_WAKE);
    flush(ui);
}

void ui_key(ui_t *ui, ui_key_t key, ui_press_t press)
{
    ui->last_key_ms = ui_now(ui);
    ui_perf(ui, "key", key, press);
    if (key == UI_KEY_MENU && press == UI_LONG && !usb_active()) {
        ui_sleep(ui);
        return;
    }
    const app_t *t = ui_top(ui);
    if (ui->auto_ms && t == &app_reader) {
        // Orice apăsare în lector oprește întoarcerea automată, și doar atât.
        ui->auto_ms = 0;
        ui_perf(ui, "auto_stop", 0, 0);
        ui_redraw(ui, false);        // dispare „auto” din subsol
    } else if (t && t->key) {
        t->key(ui, key, press);
    }
    flush(ui);
}

bool ui_idle(ui_t *ui)
{
    uint32_t now = ui_now(ui);
    if (now - ui->last_battery_ms >= BATTERY_EVERY_MS && ui->p.battery_mv) {
        ui->last_battery_ms = now;
        battery_sample(ui);
        int shown = battery_shown_mv(ui);
        ui_perf(ui, "battery", (uint32_t)(ui->battery_mv > 0 ? ui->battery_mv : 0), (uint32_t)(shown > 0 ? shown : 0));
        if (battery_low(ui)) {
            sleep_now(ui, low_battery);    // cu întoarcerea automată: capătul lui E4
            return false;
        }
    }
    // Cu întoarcerea automată pornită, sau cu cardul la calculator, dispozitivul
    // nu adoarme de la sine.
    if (!ui->auto_ms && !usb_active() && now - ui->last_key_ms >= (uint32_t)settings_value(&ui->set, SET_SLEEP) * 60000u) {
        ui_sleep(ui);
        return false;
    }
    if (auto_due(ui, now)) {
        ui->auto_at = now + ui->auto_ms;
        reader_auto_turn(ui);
        flush(ui);     // pagina întâi; indexul din fundal abia la apelul următor, ca să nu intre în timpi
        return true;
    }
    bool more = false;
    for (int i = ui->depth - 1; i >= 0; i--)
        if (ui->stack[i]->idle) more |= ui->stack[i]->idle(ui);
    flush(ui);         // lucrul de fundal poate schimba ecranul (subsolul „n / total”)
    return more;
}

uint32_t ui_wait_ms(ui_t *ui)
{
    uint32_t now = ui_now(ui);
    uint32_t sleep_at = ui->last_key_ms + (uint32_t)settings_value(&ui->set, SET_SLEEP) * 60000u;
    uint32_t battery_at = ui->last_battery_ms + BATTERY_EVERY_MS;
    uint32_t a = sleep_at - now, b = battery_at - now;
    if ((int32_t)a < 0) a = 0;
    if ((int32_t)b < 0) b = 0;
    if (ui->auto_ms || usb_active()) a = UINT32_MAX;      // fără somn de la sine
    uint32_t w = a < b ? a : b;
    uint32_t u = usb_wait_ms(ui);
    if (u < w) w = u;
    if (ui->auto_ms && ui_top(ui) == &app_reader) {
        uint32_t c = (int32_t)(ui->auto_at - now) < 0 ? 0 : ui->auto_at - now;
        if (c < w) w = c;
    }
    return w;
}

int ui_battery_pct(int mv)
{
    // Tensiunea în repaus a unei celule LiPo la fiecare 10 %, de la 0 % la 100 %.
    static const int16_t curve[11] = {3300, 3600, 3690, 3730, 3770, 3800, 3840, 3870, 3920, 4000, 4150};
    if (mv <= curve[0]) return 0;
    if (mv >= curve[10]) return 100;
    int i = 1;
    while (mv > curve[i]) i++;
    return (i - 1) * 10 + (mv - curve[i - 1]) * 10 / (curve[i] - curve[i - 1]);
}

// ---- desenarea comună ----

// Taie un text la max_w px, cu puncte de suspensie. `buf` are cel puțin len + 4 octeți.
static void cut_to(const font_t *f, char *buf, int max_w)
{
    size_t len = strlen(buf);
    if (gfx_text_width(f, buf) <= max_w) return;
    while (len > 0) {
        do len--; while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);   // înapoi cu un caracter
        while (len > 0 && buf[len - 1] == ' ') len--;
        memcpy(buf + len, "\xE2\x80\xA6", 4);
        if (gfx_text_width(f, buf) <= max_w) break;
    }
}

static void text_cut(gfx_t *g, const font_t *f, int x, int y, const char *s, size_t len, int max_w, gfx_color_t c)
{
    char buf[160];
    if (len > sizeof buf - 4) len = sizeof buf - 4;
    memcpy(buf, s, len);
    buf[len] = 0;
    cut_to(f, buf, max_w);
    gfx_text(g, f, x, y, buf, c);
}

void ui_text_fit(gfx_t *g, const font_t *f, int x, int y, const char *s, size_t len, int max_w)
{
    text_cut(g, f, x, y, s, len, max_w, GFX_BLACK);
}

void ui_copy(char *dst, size_t n, const char *src)
{
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;   // src[len] e primul octet care nu încape
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

void ui_text_center(gfx_t *g, const font_t *f, int y, const char *s)
{
    gfx_text(g, f, (g->width - gfx_text_width(f, s)) / 2, y, s, GFX_BLACK);
}

// Bara de sus: titlul ecranului și bateria.
void ui_header(ui_t *ui, gfx_t *g, const char *title)
{
    const font_t *f = &font_ui16;
    gfx_fill_rect(g, 0, 0, UI_W, HEADER_H, GFX_BLACK);
    int y = (HEADER_H - f->line_height) / 2 + f->ascent;
    int right = UI_W - 8;
    if (battery_shown_mv(ui) > NO_BATTERY_MV) {
        char pct[8];
        snprintf(pct, sizeof pct, "%d%%", ui_battery_pct(battery_shown_mv(ui)));
        right -= gfx_text_width(&font_ui12, pct);
        gfx_text(g, &font_ui12, right, y, pct, GFX_WHITE);
        right -= 8;
    }
    text_cut(g, f, 8, y, title, strlen(title), right - 16, GFX_WHITE);
}

static int row_height(const ui_list_t *l)
{
    return font_ui16.line_height + (l->two_line ? font_ui12.line_height : 0) + 10;
}

#define LIST_TOP (HEADER_H + 4)

static int rows_per_page(const ui_list_t *l) { return (UI_H - LIST_TOP - LIST_HINT_H) / row_height(l); }

void ui_list_reset(ui_list_t *l, const char *title, int count, int sel, bool two_line,
                   void (*item)(ui_t *, int, char *, char *, char *))
{
    l->title = title;
    l->count = count;
    l->sel = count ? (sel < 0 ? 0 : sel >= count ? count - 1 : sel) : 0;
    l->two_line = two_line;
    l->item = item;
    int per = rows_per_page(l);
    l->top = l->sel / per * per;
}

void ui_list_draw(ui_t *ui, gfx_t *g, ui_list_t *l)
{
    const font_t *f = &font_ui16, *s = &font_ui12;
    ui_header(ui, g, l->title);
    int per = rows_per_page(l), rh = row_height(l);
    if (l->sel < l->top) l->top = l->sel / per * per;
    if (l->sel >= l->top + per) l->top = l->sel / per * per;
    char label[UI_ITEM], value[UI_ITEM], sub[UI_ITEM];
    for (int i = l->top; i < l->count && i < l->top + per; i++) {
        int y = LIST_TOP + (i - l->top) * rh;
        label[0] = value[0] = sub[0] = 0;
        l->item(ui, i, label, value, sub);
        // Rândul ales e inversat: o casetă neagră cu textul alb.
        gfx_color_t ink = GFX_BLACK;
        if (i == l->sel) {
            gfx_fill_rect(g, 4, y, UI_W - 8, rh - 2, GFX_BLACK);
            ink = GFX_WHITE;
        }
        int vw = value[0] ? gfx_text_width(f, value) : 0;
        int base = y + 5 + f->ascent;
        text_cut(g, f, 12, base, label, strlen(label), UI_W - 24 - (vw ? vw + 8 : 0), ink);
        if (vw) gfx_text(g, f, UI_W - 12 - vw, base, value, ink);
        if (l->two_line && sub[0])
            text_cut(g, s, 12, base + f->descent + 2 + s->ascent, sub, strlen(sub), UI_W - 24, ink);
        if (i != l->sel && i + 1 < l->count && i + 1 < l->top + per && i + 1 != l->sel)
            gfx_hline(g, 12, y + rh - 1, UI_W - 24, GFX_BLACK);
    }
    if (!l->count) ui_text_center(g, f, LIST_TOP + 40, "(nimic)");

    // Jos: unde ești în listă și ce face fiecare buton.
    int hy = UI_H - 6;
    gfx_hline(g, 0, UI_H - LIST_HINT_H, UI_W, GFX_BLACK);
    const char *hint = "MENIU: alege · ÎNAPOI lung: ieși";
    int pw = 0;
    if (l->count > per) {
        char pg[24];
        snprintf(pg, sizeof pg, "%d/%d", l->top / per + 1, (l->count + per - 1) / per);
        pw = gfx_text_width(s, pg) + 8;
        gfx_text(g, s, UI_W - 8 - gfx_text_width(s, pg), hy, pg, GFX_BLACK);
    }
    if (gfx_text_width(s, hint) + pw > UI_W - 16) hint = "MENIU: alege";    // o listă cu sute de pagini
    gfx_text(g, s, 8, hy, hint, GFX_BLACK);
}

bool ui_list_key(ui_list_t *l, ui_key_t k, ui_press_t p)
{
    if (!l->count || k == UI_KEY_MENU) return false;
    if (k == UI_KEY_PREV && p == UI_LONG) return false;
    if (k == UI_KEY_NEXT && p == UI_LONG) {
        int per = rows_per_page(l);
        int next = (l->sel / per + 1) * per;
        l->sel = next < l->count ? next : 0;
        l->top = l->sel;
        return true;
    }
    l->sel = k == UI_KEY_NEXT ? (l->sel + 1) % l->count : (l->sel + l->count - 1) % l->count;
    return true;
}

// ---- lansatorul ----

enum { H_CONTINUE, H_LIBRARY, H_USB, H_SETTINGS, H_DIAG, H_SLEEP };
static int s_home_items[6], s_home_count;
static char s_home_title[64], s_home_author[48];

static void home_item(ui_t *ui, int i, char *label, char *value, char *sub)
{
    (void)ui, (void)value;
    switch (s_home_items[i]) {
    case H_CONTINUE:
        strcpy(label, "Continuă lectura");
        snprintf(sub, UI_ITEM, "%s%s", s_home_title, s_home_author[0] ? " · " : "");
        ui_copy(sub + strlen(sub), UI_ITEM - strlen(sub), s_home_author);
        break;
    case H_LIBRARY: strcpy(label, "Biblioteca"), strcpy(sub, "toate cărțile de pe card"); break;
    case H_USB: strcpy(label, "Conectare la PC"), strcpy(sub, "copiază cărți pe card prin USB"); break;
    case H_SETTINGS: strcpy(label, "Setări"), strcpy(sub, "textul, ecranul, somnul"); break;
    case H_DIAG: strcpy(label, "Diagnostic"), strcpy(sub, "bateria, memoria, versiunile"); break;
    case H_SLEEP: strcpy(label, "Somn"), strcpy(sub, "sau MENIU lung, oriunde"); break;
    }
}

static void home_build(ui_t *ui)
{
    s_home_count = 0;
    s_home_title[0] = s_home_author[0] = 0;
    if (ui->recent.count) {
        static ibv_t meta;
        char path[UI_PATH];
        snprintf(path, sizeof path, "%s/%s", ui->p.root, ui->recent.book[0].path);
        if (ibv_open_meta(&meta, path) == IBV_OK && !memcmp(meta.book_id, ui->recent.book[0].id, 8)) {
            ui_copy(s_home_title, sizeof s_home_title, meta.meta[IBV_TITLE] ? meta.meta[IBV_TITLE] : ui->recent.book[0].path);
            ui_copy(s_home_author, sizeof s_home_author, meta.meta[IBV_AUTHOR] ? meta.meta[IBV_AUTHOR] : "");
            s_home_items[s_home_count++] = H_CONTINUE;
        }
        ibv_close(&meta);
    }
    s_home_items[s_home_count++] = H_LIBRARY;
    if (ui->p.usb_start) s_home_items[s_home_count++] = H_USB;
    s_home_items[s_home_count++] = H_SETTINGS;
    s_home_items[s_home_count++] = H_DIAG;
    s_home_items[s_home_count++] = H_SLEEP;
    ui_list_reset(ui_my_list(ui), "Cititor", s_home_count, 0, true, home_item);
}

static void home_enter(ui_t *ui) { home_build(ui); }

static void home_resume(ui_t *ui)
{
    int sel = ui_my_list(ui)->sel;
    int was = s_home_count;
    home_build(ui);
    // Dacă „Continuă” a apărut sau a dispărut, selecția rămâne pe același element.
    ui_my_list(ui)->sel = sel + (s_home_count - was);
    if (ui_my_list(ui)->sel < 0 || ui_my_list(ui)->sel >= s_home_count) ui_my_list(ui)->sel = 0;
}

static void home_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    ui_list_t *l = ui_my_list(ui);
    if (ui_list_key(l, k, p)) {
        ui_redraw(ui, false);
        return;
    }
    if (k != UI_KEY_MENU) return;
    switch (s_home_items[l->sel]) {
    case H_CONTINUE:
        if (reader_open(ui, ui->recent.book[0].path)) ui_push(ui, &app_reader);
        else ui_redraw(ui, false);
        break;
    case H_LIBRARY: ui_push(ui, &app_library); break;
    case H_USB: ui_push(ui, &app_usb); break;
    case H_SETTINGS: ui_push(ui, &app_settings); break;
    case H_DIAG: ui_push(ui, &app_diag); break;
    case H_SLEEP: ui_sleep(ui); break;
    }
}

static void home_draw(ui_t *ui, gfx_t *g) { ui_list_draw(ui, g, ui_my_list(ui)); }

const app_t app_home = {
    .name = "lansator", .enter = home_enter, .resume = home_resume, .key = home_key, .draw = home_draw,
};

// ---- diagnosticul ----

static void diag_draw(ui_t *ui, gfx_t *g)
{
    static char lines[24][UI_DIAG_LINE];
    int n = 0;
    snprintf(lines[n++], UI_DIAG_LINE, "Interfața %s, LAYOUT %d", UI_VERSION, LAYOUT_VERSION);
    if (ui->battery_mv > NO_BATTERY_MV)
        snprintf(lines[n++], UI_DIAG_LINE, "Baterie: %d,%02d V, %d%%", ui->battery_mv / 1000,
                 ui->battery_mv % 1000 / 10, ui_battery_pct(battery_shown_mv(ui)));   // V citit, % netezit ca sus
    else
        snprintf(lines[n++], UI_DIAG_LINE, "Baterie: lipsă (USB)");
    if (ui->p.diag) n += ui->p.diag(ui->p.ctx, lines + n, 24 - n - 6);
    snprintf(lines[n++], UI_DIAG_LINE, "Cărți recente: %d", ui->recent.count);
    if (ui->lib_scanned) snprintf(lines[n++], UI_DIAG_LINE, "Biblioteca: %d cărți", ui->lib_count);
    n += reader_diag(lines + n, 24 - n);

    ui_header(ui, g, "Diagnostic");
    const font_t *f = &font_ui12;
    int y = HEADER_H + 6 + f->ascent;
    for (int i = 0; i < n && y < UI_H - LIST_HINT_H; i++, y += f->line_height)
        ui_text_fit(g, f, 8, y, lines[i], strlen(lines[i]), UI_W - 16);
    gfx_hline(g, 0, UI_H - LIST_HINT_H, UI_W, GFX_BLACK);
    gfx_text(g, f, 8, UI_H - 6, "ÎNAINTE: actualizează · MENIU: înapoi", GFX_BLACK);
}

static void diag_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    (void)p;
    if (k == UI_KEY_NEXT) {
        if (ui->p.battery_mv) battery_sample(ui);
        ui_redraw(ui, false);
    } else {
        ui_pop(ui);
    }
}

const app_t app_diag = {.name = "diagnostic", .key = diag_key, .draw = diag_draw};

// ---- setările ----

static void settings_item(ui_t *ui, int i, char *label, char *value, char *sub)
{
    (void)value;
    strcpy(label, settings_name(i));
    snprintf(sub, UI_ITEM, "%s", settings_option_name(i, ui->set.v[i] < settings_options(i) ? ui->set.v[i] : 0));
}

static void settings_enter(ui_t *ui) { ui_list_reset(ui_my_list(ui), "Setări", SET_COUNT, 0, true, settings_item); }

static void settings_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    ui_list_t *l = ui_my_list(ui);
    if (ui_list_key(l, k, p)) {
        ui_redraw(ui, false);
    } else if (k == UI_KEY_MENU) {
        settings_cycle(&ui->set, l->sel);
        ui->settings_dirty = true;
        ui_redraw(ui, false);
    } else if (k == UI_KEY_PREV && p == UI_LONG) {
        ui_pop(ui);
    }
}

static void settings_leave(ui_t *ui) { save_settings(ui); }
static void settings_draw(ui_t *ui, gfx_t *g) { ui_list_draw(ui, g, ui_my_list(ui)); }

const app_t app_settings = {
    .name = "setări", .enter = settings_enter, .leave = settings_leave, .key = settings_key, .draw = settings_draw,
};
