// `sim ui`: interfața întreagă (firmware/components/ui) pe PC, condusă de un
// scenariu de apăsări. Fiecare reîmprospătare a panoului devine un PNG numerotat,
// iar jurnal.txt spune ce tastă a produs-o, dacă a fost completă sau parțială
// și zona parțială.
//
//   sim ui CARD -o DIR --keys "n n N m ..." [--wake] [--battery MV] [--perf]
//
// Cu dosarul CARD/perf, jurnalul de măsurători (perflog.h) se scrie ca pe
// dispozitiv, în CARD/perf/NNNN.csv, un fișier la fiecare pornire. Timpul din
// el e ceasul simulat, plus timpul real petrecut în pasul curent: duratele
// paginării sunt cele de pe PC, ale panoului 0. Bun pentru încercat
// tools/perf/perf.py, nu pentru cifre.
//
// CARD e rădăcina cardului: cărțile în CARD/ și CARD/books/, fonturile în
// CARD/fonts/ (vezi sim/make_card.sh). Indexurile, pozițiile și semnele de carte
// se scriu în CARD/.ibv/, ca pe dispozitiv; setările (NVS pe dispozitiv) în
// CARD/.ibv/sim-settings.
//
// Scenariul, separat prin spații:
//   n p m        ÎNAINTE, ÎNAPOI, MENIU, apăsare scurtă
//   N P M        aceleași, apăsare lungă
//   i            lucrul de fundal până la capăt (indexul paginilor)
//   t:SEC        trec SEC secunde fără apăsări (somnul după inactivitate)
//   b:MV         bateria are acum MV milivolți
//   u:STARE      discul USB (FW-12), din partea calculatorului: w niciun
//                calculator, h discul montat, p magistrala în repaus, r discul
//                scos în siguranță; x face ca următorul usb_start să eșueze
//                (cardul nu răspunde). Interfața vede schimbarea la următoarea
//                verificare, ca pe dispozitiv.
// Fiecare apăsare înseamnă o secundă pe ceasul simulat și, cu --idle, lucrul de
// fundal până la capăt după ea, cum ar fi între pagini pe dispozitiv. După
// somn, apăsarea următoare trezește dispozitivul (o pornire nouă, UI_BOOT_WAKE)
// și nu ajunge la interfață, ca pe dispozitiv.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gfx.h"
#include "perflog.h"
#include "png.h"
#include "ui.h"

#define W 240
#define H 416

typedef struct {
    const char *root, *outdir;
    FILE *log;
    uint32_t now;
    int battery;
    bool slept, perf;
    bool usb_on, usb_fail;
    ui_usb_t usb;       // starea discului, cum ar vedea-o firmware-ul
    int shown;
    char key[16];       // ce a produs reîmprospătările care urmează
} sim_ui_t;

static uint32_t now_ms(void *ctx) { return ((sim_ui_t *)ctx)->now; }

static sim_ui_t *s_sim;
static uint64_t s_tick_real, s_last_us;    // timpul real la ultima schimbare a ceasului simulat

static uint64_t real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void set_now(sim_ui_t *s, uint32_t now)
{
    s->now = now;
    s_tick_real = real_us();
}

// Crește mereu, chiar când un pas real ține mai mult decât pasul simulat.
static uint64_t perf_us(void)
{
    uint64_t t = (uint64_t)s_sim->now * 1000u + (real_us() - s_tick_real);
    if (t <= s_last_us) t = s_last_us + 1;
    return s_last_us = t;
}
static int battery_mv(void *ctx) { return ((sim_ui_t *)ctx)->battery; }

static void show(void *ctx, const gfx_t *g, ui_refresh_t kind, gfx_rect_t r)
{
    sim_ui_t *s = ctx;
    char path[512];
    s->shown++;
    snprintf(path, sizeof path, "%s/%03d.png", s->outdir, s->shown);
    png_write(g, path);
    uint64_t t = perf_us();
    perflog_add(t, "refresh", kind == UI_REFRESH_FULL, 0);
    perflog_add(t, "refresh_io", 0, 0);
    perflog_add(t, "refresh_rect", (uint32_t)r.x << 16 | (uint16_t)r.y, (uint32_t)r.w << 16 | (uint16_t)r.h);
    if (kind == UI_REFRESH_FULL)
        fprintf(s->log, "%03d  %-6s completă\n", s->shown, s->key);
    else
        fprintf(s->log, "%03d  %-6s parțială  %d,%d %dx%d\n", s->shown, s->key, r.x, r.y, r.w, r.h);
}

static void settings_path(sim_ui_t *s, char *buf, size_t n) { snprintf(buf, n, "%s/.ibv/sim-settings", s->root); }

static bool load_settings(void *ctx, void *buf, size_t len)
{
    char path[512];
    settings_path(ctx, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(buf, 1, len, f) == len;
    fclose(f);
    return ok;
}

static void save_settings(void *ctx, const void *buf, size_t len)
{
    char path[512];
    settings_path(ctx, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, len, f);
    fclose(f);
}

static void do_sleep(void *ctx)
{
    sim_ui_t *s = ctx;
    s->slept = true;
    perflog_flush(perf_us);
    fprintf(s->log, "     %-6s somn\n", s->key);
}

static int diag(void *ctx, char (*lines)[UI_DIAG_LINE], int max)
{
    sim_ui_t *s = ctx;
    if (max < 2) return 0;
    snprintf(lines[0], UI_DIAG_LINE, "Platformă: simulator");
    snprintf(lines[1], UI_DIAG_LINE, "Ceas: %u s", (unsigned)(s->now / 1000));
    if (max < 3 || !perflog_on()) return 2;
    snprintf(lines[2], UI_DIAG_LINE, "Jurnal: %s, %d în memorie", perflog_file(), perflog_pending());
    return 3;
}

static void perf(void *ctx, const char *event, uint32_t a, uint32_t b)
{
    sim_ui_t *s = ctx;
    perflog_add(perf_us(), event, a, b);
    if (s->perf) fprintf(stderr, "perf %8u %-14s %u %u\n", (unsigned)s->now, event, (unsigned)a, (unsigned)b);
}

// Discul USB fals: nu se atinge nimic pe card; doar starea, din scenariu.
static bool usb_start(void *ctx)
{
    sim_ui_t *s = ctx;
    if (s->usb_fail) {
        s->usb_fail = false;
        return false;
    }
    s->usb_on = true;
    s->usb = UI_USB_WAITING;
    return true;
}

static ui_usb_t usb_state(void *ctx) { return ((sim_ui_t *)ctx)->usb; }

static void usb_stop(void *ctx) { ((sim_ui_t *)ctx)->usb_on = false; }

static void idle_all(ui_t *ui)
{
    while (ui_idle(ui)) {}
}

int cmd_ui(int argc, char **argv)
{
    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    sim_ui_t s = {.outdir = ".", .battery = 3900};
    s_sim = &s;
    set_now(&s, 1000);
    const char *keys = "";
    bool wake = false, idle = false;
    for (int i = 1; i < argc; i++) {
        if (i + 1 < argc && !strcmp(argv[i], "-o")) s.outdir = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--keys")) keys = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--battery")) s.battery = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wake")) wake = true;
        else if (!strcmp(argv[i], "--idle")) idle = true;
        else if (!strcmp(argv[i], "--perf")) s.perf = true;
        else if (argv[i][0] != '-' && !s.root) s.root = argv[i];
        else { fprintf(stderr, "ui: opțiune necunoscută %s\n", argv[i]); return 2; }
    }
    if (!s.root) { fprintf(stderr, "ui: trebuie rădăcina cardului\n"); return 2; }

    char path[512];
    snprintf(path, sizeof path, "%s/jurnal.txt", s.outdir);
    if (!(s.log = fopen(path, "w"))) { perror(path); return 1; }

    gfx_t g;
    gfx_init(&g, buf, W, H);
    ui_platform_t p = {
        .root = s.root, .ctx = &s, .now_ms = now_ms, .show = show, .battery_mv = battery_mv,
        .load_settings = load_settings, .save_settings = save_settings, .sleep = do_sleep, .diag = diag,
        .perf = perf, .usb_start = usb_start, .usb_state = usb_state, .usb_stop = usb_stop,
    };
    strcpy(s.key, "start");
    perflog_open(s.root, perf_us());
    ui_t *ui = ui_init(&p, &g);
    ui_start(ui, wake ? UI_BOOT_WAKE : UI_BOOT_COLD);
    if (idle) idle_all(ui);

    char *script = strdup(keys);
    for (char *tok = strtok(script, " \t\n"); tok; tok = strtok(NULL, " \t\n")) {
        snprintf(s.key, sizeof s.key, "%s", tok);
        if (!strncmp(tok, "t:", 2)) {
            uint32_t end = s.now + (uint32_t)atoi(tok + 2) * 1000u;
            // Ca platforma: ui_idle la fiecare termen dat de ui_wait_ms, până trec secundele sau doarme.
            while (!s.slept && s.now < end) {
                uint32_t step = ui_wait_ms(ui);
                set_now(&s, step && s.now + step < end ? s.now + step : end);
                idle_all(ui);
                perflog_flush(perf_us);
            }
            continue;
        }
        if (!strncmp(tok, "u:", 2)) {
            static const char st[] = "whpr";
            const char *c = tok[2] && !tok[3] ? strchr(st, tok[2]) : NULL;
            if (tok[2] == 'x' && !tok[3]) s.usb_fail = true;
            else if (!c) {
                fprintf(stderr, "ui: stare USB necunoscută „%s”\n", tok);
                return 2;
            } else {
                s.usb = (ui_usb_t)(c - st);
                set_now(&s, s.now + 1000);      // următoarea verificare a interfeței
                idle_all(ui);
            }
            continue;
        }
        if (!strncmp(tok, "b:", 2)) {
            s.battery = atoi(tok + 2);
            continue;
        }
        set_now(&s, s.now + 1000);
        if (s.slept) {
            s.slept = false;
            perflog_open(s.root, perf_us());     // o pornire nouă, un fișier nou
            ui = ui_init(&p, &g);
            ui_start(ui, UI_BOOT_WAKE);
            if (idle) idle_all(ui);
            continue;
        }
        if (!strcmp(tok, "i")) {
            idle_all(ui);
            continue;
        }
        static const char k[] = "pnm";
        const char *c = tok[1] ? NULL : strchr(k, tok[0] | 0x20);
        if (!c || !*c) {
            fprintf(stderr, "ui: tastă necunoscută „%s”\n", tok);
            return 2;
        }
        ui_key(ui, (ui_key_t)(c - k), tok[0] >= 'a' ? UI_SHORT : UI_LONG);
        if (idle && !s.slept) idle_all(ui);
        perflog_flush(perf_us);
    }
    perflog_flush(perf_us);
    free(script);
    fclose(s.log);
    printf("%d reîmprospătări în %s (jurnal.txt)\n", s.shown, s.outdir);
    return 0;
}
