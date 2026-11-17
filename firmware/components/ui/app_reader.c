// Lectorul și ecranele lui: meniul lectorului, cuprinsul, semnele de carte.
//
// Butoanele în lector: ÎNAINTE / ÎNAPOI = pagina următoare / dinainte;
// ÎNAINTE lung = capitolul următor; ÎNAPOI lung = începutul capitolului (sau
// cel dinainte, dacă ești deja la început); MENIU = meniul; MENIU lung = somn.
// Cu întoarcerea automată pornită (din meniu), prima apăsare doar o oprește.
//
// O singură carte e deschisă odată, cu fonturile ei încărcate de pe card în
// memorie (Libron sau PT Serif după limbă, fontsel.c). Poziția se salvează în
// fișierul `.pos` la fiecare 10 pagini, la închidere și la somn. Indexul
// paginilor se construiește în fundal (reader_idle) și se salvează în `.pg`,
// deci subsolul trece de la procent la „pagina / pagini” după prima lectură.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontsel.h"
#include "ui_priv.h"

#define SAVE_EVERY 10           // pagini întoarse între două salvări ale poziției
#define INDEX_STEP 8            // pagini indexate la un apel ui_idle
#define INDEX_SAVE_EVERY 512    // pagini între două salvări ale unui index incomplet

typedef struct {
    bool open;
    char rel[RECENT_PATH];
    ibv_t book;
    uint8_t *font_data[4];
    font_t font[4];
    fontsel_t fs;
    bool builtin;               // fonturile cărții lipsesc de pe card: fontul de interfață
    layout_cfg_t cfg;
    int margin;
    settings_t built_from;      // setările după care s-au făcut cfg și indexul
    layout_page_t page, scratch;
    ibv_err_t page_err;
    uint32_t page_ms;
    pageidx_t idx;
    char idx_path[160];
    uint32_t idx_saved;         // câte pagini avea indexul la ultima salvare
    bool idx_failed;
    bookmarks_t bm;
    int turns;
    layout_pos_t saved_pos;
} reader_t;

static reader_t R;
static layout_fit_t s_fit;      // ~92 KB; în .bss, nu pe heap, ca să nu se fragmenteze

bool reader_is_open(void) { return R.open; }
const ibv_t *reader_book(void) { return R.open ? &R.book : NULL; }

// ---- fonturile și paginarea ----

static void free_fonts(void)
{
    for (int i = 0; i < 4; i++) {
        free(R.font_data[i]);
        R.font_data[i] = NULL;
    }
    memset(R.cfg.body, 0, sizeof R.cfg.body);
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *d = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (d = malloc((size_t)n))) {   // malloc aliniază la 4 sau mai mult
        if (fread(d, 1, (size_t)n, f) != (size_t)n) free(d), d = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

static void load_fonts(ui_t *ui)
{
    static const char *style[4] = {"r", "b", "i", "bi"};
    char dir[UI_PATH], path[UI_PATH];
    free_fonts();
    R.fs = fontsel_pick(R.book.meta[IBV_LANG], settings_value(&ui->set, SET_SIZE));
    snprintf(dir, sizeof dir, "%s/fonts", ui->p.root);
    for (int i = 0; i < 4; i++) {
        size_t len;
        if (!fontsel_path(path, sizeof path, dir, R.fs, style[i])) continue;
        R.font_data[i] = read_file(path, &len);
        if (R.font_data[i] && font_open(&R.font[i], R.font_data[i], len) == FONT_OK) {
            R.cfg.body[i] = &R.font[i];
        } else {
            free(R.font_data[i]);
            R.font_data[i] = NULL;
        }
    }
    R.builtin = !R.cfg.body[0];
    if (R.builtin) {
        free_fonts();
        R.cfg.body[0] = &font_ui16;
    }
}

static void build_cfg(ui_t *ui)
{
    R.margin = settings_value(&ui->set, SET_MARGIN);
    layout_cfg_t *c = &R.cfg;
    c->heading = NULL;
    c->width = UI_W - 2 * R.margin;
    c->height = UI_H - R.margin - UI_FOOTER;
    c->indent = c->body[0]->pixel_size;
    c->line_gap = settings_value(&ui->set, SET_SPACING);
    c->para_gap = 0;
    c->justify = settings_value(&ui->set, SET_JUSTIFY);
    c->hyphenate = settings_value(&ui->set, SET_HYPHENATE);
    c->chapter_page = true;
    c->keep_lines = true;
    c->fit = &s_fit;
    layout_fit_reset(&s_fit);   // fonturi noi, poate la aceleași adrese
    R.built_from = ui->set;
}

static void save_index(void)
{
    if (!R.idx.count || R.idx.count == R.idx_saved) return;
    if (pageidx_save(&R.idx, R.book.book_id, R.idx_path)) R.idx_saved = R.idx.count;
}

static void load_index(ui_t *ui)
{
    char suffix[16];
    uint32_t h = layout_hash(&R.cfg);
    snprintf(suffix, sizeof suffix, "-%08x.pg", (unsigned)h);
    sidecar_path(R.idx_path, sizeof R.idx_path, ui->p.root, R.book.book_id, suffix);
    pageidx_free(&R.idx);
    pageidx_init(&R.idx, h);
    pageidx_load(&R.idx, R.book.book_id, h, R.idx_path);
    R.idx_saved = R.idx.count;
    R.idx_failed = false;
}

static void go_to(ui_t *ui, layout_pos_t pos)
{
    uint32_t t0 = ui_now(ui);
    ui_perf(ui, "layout_begin", pos.para, pos.byte);
    R.page_err = layout_page(&R.cfg, &R.book, pos, &R.page);
    R.page_ms = ui_now(ui) - t0;
    ui_perf(ui, "layout_end", (uint32_t)R.page.line_count, R.page_err);
    if (R.page_err) {
        // Un bloc stricat: pagina arată eroarea, iar ÎNAINTE trece la înregistrarea următoare.
        R.page.start = pos;
        R.page.end = (layout_pos_t){pos.para + 1, 0};
        R.page.at_end = pos.para + 1 >= R.book.paragraph_count;
        R.page.line_count = 0;
    }
    ui_redraw(ui, false);
}

// Pagina care conține `pos`.
static void go_to_containing(ui_t *ui, layout_pos_t pos)
{
    layout_pos_t start;
    int32_t n = pageidx_find(&R.idx, pos);
    if (n >= 0) start = R.idx.starts[n];
    else if (layout_locate(&R.cfg, &R.book, pos, &R.scratch, &start) != IBV_OK) start = pos;
    go_to(ui, start);
}

bool reader_open(ui_t *ui, const char *rel)
{
    if (R.open) reader_close(ui);
    char path[UI_PATH];
    snprintf(path, sizeof path, "%s/%s", ui->p.root, rel);
    // `rel` poate fi chiar ui->recent.book[0].path (trezirea), pe care recent_touch îl rescrie.
    ui_copy(R.rel, sizeof R.rel, rel);
    uint32_t t0 = ui_now(ui);
    if (ibv_open(&R.book, path) != IBV_OK) return false;
    R.open = true;
    load_fonts(ui);
    build_cfg(ui);
    load_index(ui);
    bm_load(ui->p.root, R.book.book_id, &R.bm);
    layout_pos_t pos = {0, 0};
    if (!pos_load(ui->p.root, R.book.book_id, &pos) || pos.para >= R.book.paragraph_count) pos = (layout_pos_t){0, 0};
    R.saved_pos = pos;
    R.turns = 0;
    go_to_containing(ui, pos);
    recent_touch(&ui->recent, R.book.book_id, R.rel);
    ui->recent.reading = true;
    ui_save_recent(ui);
    ui_perf(ui, "book_open", R.book.paragraph_count, ui_now(ui) - t0);
    return true;
}

void reader_save(ui_t *ui)
{
    if (!R.open) return;
    if (layout_pos_cmp(R.page.start, R.saved_pos) != 0 && pos_save(ui->p.root, R.book.book_id, R.page.start))
        R.saved_pos = R.page.start;
    R.turns = 0;
    save_index();
}

void reader_close(ui_t *ui)
{
    if (!R.open) return;
    reader_save(ui);
    pageidx_free(&R.idx);
    free_fonts();
    ibv_close(&R.book);
    R.open = false;
}

// După setări: dacă s-a schimbat ceva ce mută rupturile, se refac fonturile,
// paginarea și indexul, iar pagina nouă e cea care conține începutul celei vechi.
static void apply_settings(ui_t *ui)
{
    bool layout = false, size = R.built_from.v[SET_SIZE] != ui->set.v[SET_SIZE];
    for (int k = 0; k < SET_COUNT; k++)
        if (settings_affects_layout(k) && R.built_from.v[k] != ui->set.v[k]) layout = true;
    if (!layout) return;
    save_index();
    layout_pos_t at = R.page.start;
    if (size) load_fonts(ui);
    build_cfg(ui);
    load_index(ui);
    go_to_containing(ui, at);
    ui_redraw(ui, true);
}

// ---- lectorul ----

static void next_page(ui_t *ui)
{
    if (R.page.at_end) return;
    go_to(ui, R.page.end);
    if (++R.turns >= SAVE_EVERY) reader_save(ui);
}

static void prev_page(ui_t *ui)
{
    int32_t n = pageidx_find(&R.idx, R.page.start);
    if (n >= 0) {
        // Din index: pagina dinainte sau, după o eroare, începutul celei curente.
        if (layout_pos_cmp(R.idx.starts[n], R.page.start) < 0) go_to(ui, R.idx.starts[n]);
        else if (n > 0) go_to(ui, R.idx.starts[n - 1]);
        return;
    }
    layout_pos_t out;
    int pages = 0;
    ibv_err_t err = layout_prev(&R.cfg, &R.book, R.page.start, &R.scratch, &out, &pages);
    ui_perf(ui, "prev_pages", (uint32_t)pages, err);
    if (err == IBV_OK) go_to(ui, out);
}

// ÎNAINTE lung: primul capitol care începe după pagina asta. ÎNAPOI lung: ultimul
// care începe înaintea ei, adică începutul capitolului curent, sau cel dinainte
// dacă pagina e chiar prima a capitolului. Fără cuprins: începutul cărții.
static void chapter(ui_t *ui, int dir)
{
    ibv_toc_t t;
    layout_pos_t target = {0, 0};
    for (uint32_t j = 0; ibv_toc(&R.book, j, &t); j++) {
        layout_pos_t at = {t.para, 0};
        if (dir > 0 && layout_pos_cmp(at, R.page.end) >= 0) {
            go_to_containing(ui, at);
            return;
        }
        if (dir < 0) {
            if (layout_pos_cmp(at, R.page.start) >= 0) break;
            target = at;
        }
    }
    if (dir < 0) go_to_containing(ui, target);
}

void reader_auto_turn(ui_t *ui)
{
    if (!R.open) return;
    ui_perf(ui, "auto_turn", R.page.start.para, (uint32_t)(ui->battery_mv > 0 ? ui->battery_mv : 0));
    if (R.page.at_end) go_to(ui, (layout_pos_t){0, 0});   // o ia de la capăt, cât ține bateria (E4)
    else next_page(ui);
}

static void reader_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    if (k == UI_KEY_NEXT) p == UI_SHORT ? next_page(ui) : chapter(ui, 1);
    else if (k == UI_KEY_PREV) p == UI_SHORT ? prev_page(ui) : chapter(ui, -1);
    else ui_push(ui, &app_reader_menu);
}

static void draw_footer(ui_t *ui, gfx_t *g)
{
    const font_t *f = &font_ui12;
    int y = UI_H - 7;
    char right[32];
    const char *mark = ui->auto_ms ? "auto · " : "";     // întoarcerea automată e pornită
    int32_t n = R.idx.complete ? pageidx_find(&R.idx, R.page.start) : -1;
    if (n >= 0) {
        snprintf(right, sizeof right, "%s%d / %u", mark, (int)n + 1, (unsigned)R.idx.count);
    } else {
        uint32_t off = 0;
        ibv_text_offset(&R.book, R.page.start.para, R.page.start.byte, &off);
        unsigned pct = R.page.at_end ? 100u : (unsigned)((uint64_t)off * 100 / (R.book.text_bytes ? R.book.text_bytes : 1));
        snprintf(right, sizeof right, "%s%u%%", mark, pct);
    }
    int rw = gfx_text_width(f, right);
    gfx_text(g, f, UI_W - R.margin - rw, y, right, GFX_BLACK);
    ibv_toc_t t;
    int32_t ti = ibv_toc_find(&R.book, R.page.start.para);
    if (ti >= 0 && ibv_toc(&R.book, (uint32_t)ti, &t))
        ui_text_fit(g, f, R.margin, y, t.title, t.title_len, UI_W - 2 * R.margin - rw - 12);
}

// Colțul îndoit din dreapta sus, pe o pagină cu semn de carte.
static void draw_dog_ear(gfx_t *g)
{
    for (int i = 0; i < 14; i++) gfx_hline(g, UI_W - 14 + i, i, 14 - i, GFX_BLACK);
}

static void reader_draw(ui_t *ui, gfx_t *g)
{
    if (!R.page_err) R.page_err = layout_draw(&R.cfg, &R.book, &R.page, g, R.margin, R.margin);
    if (R.page_err) {
        gfx_clear(g, GFX_WHITE);
        int y = UI_H / 3;
        ui_text_center(g, &font_ui16, y, "Pagina nu se poate citi");
        ui_text_center(g, &font_ui12, y + 22, ibv_strerror(R.page_err));
        ui_text_center(g, &font_ui12, y + 40, "ÎNAINTE trece mai departe");
    }
    if (bm_find(&R.bm, R.page.start, R.page.end) >= 0) draw_dog_ear(g);
    draw_footer(ui, g);
}

static bool reader_idle(ui_t *ui)
{
    if (!R.open || R.idx.complete || R.idx_failed) return false;
    uint32_t t0 = ui_now(ui), before = R.idx.count;
    ibv_err_t err = pageidx_step(&R.idx, &R.cfg, &R.book, INDEX_STEP, &R.scratch);
    ui_perf(ui, "index_step", R.idx.count - before, ui_now(ui) - t0);
    if (err) {
        R.idx_failed = true;     // un bloc stricat: rămâne procentul
        return false;
    }
    if (R.idx.complete) {
        save_index();
        ui_perf(ui, "index_done", R.idx.count, 0);
        if (ui_top(ui) == &app_reader) ui_redraw(ui, false);   // subsolul devine „n / total”
        return false;
    }
    if (R.idx.count - R.idx_saved >= INDEX_SAVE_EVERY) save_index();
    return true;
}

static void reader_resume(ui_t *ui) { apply_settings(ui); }
static void reader_enter(ui_t *ui) { ui_redraw(ui, true); }   // o carte deschisă: reîmprospătare completă
static void reader_leave(ui_t *ui) { reader_close(ui); }

const app_t app_reader = {
    .name = "lector", .enter = reader_enter, .resume = reader_resume, .leave = reader_leave,
    .key = reader_key, .draw = reader_draw, .idle = reader_idle,
};

int reader_diag(char (*lines)[UI_DIAG_LINE], int max)
{
    if (!R.open || max < 4) return 0;
    int n = 0;
    strcpy(lines[n], "Carte: ");
    ui_copy(lines[n] + 7, UI_DIAG_LINE - 7, R.book.meta[IBV_TITLE] ? R.book.meta[IBV_TITLE] : R.rel);
    n++;
    if (R.builtin) snprintf(lines[n++], UI_DIAG_LINE, "Font: de interfață (lipsește /fonts)");
    else snprintf(lines[n++], UI_DIAG_LINE, "Font: %s %u px", R.fs.family, (unsigned)R.fs.size);
    snprintf(lines[n++], UI_DIAG_LINE, "Index: %u pagini%s, %08x", (unsigned)R.idx.count,
             R.idx.complete ? "" : R.idx_failed ? " (oprit)" : " (în lucru)", (unsigned)R.idx.layout_hash);
    snprintf(lines[n++], UI_DIAG_LINE, "Pagina: %u ms, blocuri citite %u", (unsigned)R.page_ms,
             (unsigned)R.book.blocks_loaded);
    return n;
}

// ---- meniul lectorului ----

enum { M_BACK, M_TOC, M_MARK, M_MARKS, M_SIZE, M_SETTINGS, M_LIBRARY, M_AUTO, M_DIAG };
static int s_menu[10], s_menu_count;

// Întoarcerea automată, pentru măsurători: la 5 s pentru timpii unei pagini
// (E1, E2), la un minut pentru autonomie (E4).
static const uint32_t s_auto_ms[] = {0, 5000, 60000};
static const char *const s_auto_name[] = {"oprită", "la 5 s", "la 1 min"};

static int auto_option(const ui_t *ui)
{
    for (int i = 1; i < 3; i++)
        if (ui->auto_ms == s_auto_ms[i]) return i;
    return 0;
}

static void menu_item(ui_t *ui, int i, char *label, char *value, char *sub)
{
    (void)sub;
    switch (s_menu[i]) {
    case M_BACK: strcpy(label, "Înapoi la carte"); break;
    case M_TOC: strcpy(label, "Cuprins"); break;
    case M_MARK:
        strcpy(label, bm_find(&R.bm, R.page.start, R.page.end) >= 0 ? "Șterge semnul de aici" : "Pune un semn aici");
        break;
    case M_MARKS:
        strcpy(label, "Semnele de carte");
        snprintf(value, UI_ITEM, "%u", (unsigned)R.bm.count);
        break;
    case M_SIZE:
        strcpy(label, "Mărimea textului");
        snprintf(value, UI_ITEM, "%d / %d", ui->set.v[SET_SIZE] + 1, FONTSEL_STEPS);
        break;
    case M_SETTINGS: strcpy(label, "Setări"); break;
    case M_LIBRARY: strcpy(label, "Biblioteca"); break;
    case M_AUTO:
        strcpy(label, "Întoarcere automată");
        strcpy(value, s_auto_name[auto_option(ui)]);
        break;
    case M_DIAG: strcpy(label, "Diagnostic"); break;
    }
}

static void menu_build(ui_t *ui, int sel)
{
    s_menu_count = 0;
    s_menu[s_menu_count++] = M_BACK;
    if (R.book.toc_count) s_menu[s_menu_count++] = M_TOC;
    s_menu[s_menu_count++] = M_MARK;
    if (R.bm.count) s_menu[s_menu_count++] = M_MARKS;
    s_menu[s_menu_count++] = M_SIZE;
    s_menu[s_menu_count++] = M_SETTINGS;
    s_menu[s_menu_count++] = M_LIBRARY;
    s_menu[s_menu_count++] = M_AUTO;
    s_menu[s_menu_count++] = M_DIAG;     // cu rândurile lectorului: fontul, indexul, timpul paginii
    const char *t = R.book.meta[IBV_TITLE];
    ui_list_reset(ui_my_list(ui), t && *t ? t : "Cartea", s_menu_count, sel, false, menu_item);
}

static void menu_enter(ui_t *ui) { menu_build(ui, 0); }
static void menu_resume(ui_t *ui) { menu_build(ui, ui_my_list(ui)->sel); }

static void menu_key(ui_t *ui, ui_key_t k, ui_press_t p)
{
    ui_list_t *l = ui_my_list(ui);
    if (ui_list_key(l, k, p)) {
        ui_redraw(ui, false);
        return;
    }
    if (k == UI_KEY_PREV) {
        ui_pop(ui);
        return;
    }
    int bm;
    switch (s_menu[l->sel]) {
    case M_BACK: ui_pop(ui); break;
    case M_TOC: ui_push(ui, &app_toc); break;
    case M_MARK:
        bm = bm_find(&R.bm, R.page.start, R.page.end);
        if (bm >= 0) bm_remove(&R.bm, bm);
        else bm_add(&R.bm, R.page.start);
        bm_save(ui->p.root, R.book.book_id, &R.bm);
        ui_pop(ui);    // înapoi la pagină, care arată (sau nu mai arată) colțul îndoit
        break;
    case M_MARKS: ui_push(ui, &app_bookmarks); break;
    case M_SIZE:
        settings_cycle(&ui->set, SET_SIZE);
        ui->settings_dirty = true;
        ui_redraw(ui, false);
        break;
    case M_SETTINGS: ui_push(ui, &app_settings); break;
    case M_AUTO:
        // Pornește la ieșirea din meniu: prima pagină vine după o perioadă întreagă.
        ui->auto_ms = s_auto_ms[(auto_option(ui) + 1) % 3];
        ui->auto_at = ui_now(ui) + ui->auto_ms;
        ui_perf(ui, "auto_set", ui->auto_ms, 0);
        ui_redraw(ui, false);
        break;
    case M_DIAG: ui_push(ui, &app_diag); break;
    case M_LIBRARY:
        if (!ui_pop_to(ui, &app_library)) {
            ui_pop_to(ui, &app_home);
            ui_push(ui, &app_library);
        }
        break;
    }
}

static void list_draw(ui_t *ui, gfx_t *g) { ui_list_draw(ui, g, ui_my_list(ui)); }

const app_t app_reader_menu = {
    .name = "meniul lectorului", .enter = menu_enter, .resume = menu_resume, .key = menu_key, .draw = list_draw,
};

// ---- cuprinsul și semnele de carte ----

// „pagina n” dacă indexul e complet, altfel procentul.
static void where(ui_t *ui, layout_pos_t pos, char *out)
{
    (void)ui;
    int32_t n = R.idx.complete ? pageidx_find(&R.idx, pos) : -1;
    if (n >= 0) {
        snprintf(out, UI_ITEM, "%d", (int)n + 1);
        return;
    }
    uint32_t off = 0;
    ibv_text_offset(&R.book, pos.para, pos.byte, &off);
    snprintf(out, UI_ITEM, "%u%%", (unsigned)((uint64_t)off * 100 / (R.book.text_bytes ? R.book.text_bytes : 1)));
}

static void toc_item(ui_t *ui, int i, char *label, char *value, char *sub)
{
    (void)sub;
    ibv_toc_t t;
    if (!ibv_toc(&R.book, (uint32_t)i, &t)) return;
    int indent = 2 * (t.level > 1 ? t.level - 1 : 0);
    size_t len = t.title_len < UI_ITEM - 1 - indent ? t.title_len : UI_ITEM - 1 - indent;
    memset(label, ' ', (size_t)indent);
    memcpy(label + indent, t.title, len);
    label[indent + len] = 0;
    where(ui, (layout_pos_t){t.para, 0}, value);
}

static void toc_enter(ui_t *ui)
{
    int32_t ti = ibv_toc_find(&R.book, R.page.start.para);   // -1 înainte de prima intrare: ui_list_reset o duce la 0
    ui_list_reset(ui_my_list(ui), "Cuprins", R.book.toc_count, ti, false, toc_item);
}

static void jump_key(ui_t *ui, ui_key_t k, ui_press_t p, layout_pos_t (*target)(int))
{
    ui_list_t *l = ui_my_list(ui);
    if (ui_list_key(l, k, p)) {
        ui_redraw(ui, false);
    } else if (k == UI_KEY_PREV) {
        ui_pop(ui);
    } else if (l->count) {
        layout_pos_t at = target(l->sel);
        ui_pop_to(ui, &app_reader);
        go_to_containing(ui, at);
    }
}

static layout_pos_t toc_target(int i)
{
    ibv_toc_t t;
    return ibv_toc(&R.book, (uint32_t)i, &t) ? (layout_pos_t){t.para, 0} : R.page.start;
}

static void toc_key(ui_t *ui, ui_key_t k, ui_press_t p) { jump_key(ui, k, p, toc_target); }

const app_t app_toc = {.name = "cuprins", .enter = toc_enter, .key = toc_key, .draw = list_draw};

// Primele cuvinte de la un semn de carte, fără cratimele moi și spațiile de lățime
// zero, tăiate la un caracter întreg (lățimea o taie apoi ui_list_draw).
static void snippet(layout_pos_t at, char *out)
{
    ibv_record_t r;
    out[0] = 0;
    if (ibv_record(&R.book, at.para, &r) != IBV_OK || at.byte > r.text_len) return;
    const unsigned char *c = (const unsigned char *)r.text;
    size_t o = 0;
    for (uint32_t i = at.byte; i < r.text_len;) {
        uint32_t n = c[i] < 0x80 ? 1 : c[i] < 0xE0 ? 2 : c[i] < 0xF0 ? 3 : 4;
        if (i + n > r.text_len || o + n > UI_ITEM - 1) break;
        bool skip = (n == 2 && c[i] == 0xC2 && c[i + 1] == 0xAD)                                     // U+00AD
                 || (n == 3 && c[i] == 0xE2 && c[i + 1] == 0x80 && c[i + 2] == 0x8B);                // U+200B
        if (!skip) memcpy(out + o, c + i, n), o += n;
        i += n;
    }
    out[o] = 0;
}

static void bm_item(ui_t *ui, int i, char *label, char *value, char *sub)
{
    (void)sub;
    snippet(R.bm.at[i], label);
    if (!label[0]) strcpy(label, "(semn)");
    where(ui, R.bm.at[i], value);
}

static void bm_enter(ui_t *ui)
{
    ui_list_reset(ui_my_list(ui), "Semnele de carte", R.bm.count, 0, false, bm_item);
}

static layout_pos_t bm_target(int i) { return R.bm.at[i]; }
static void bm_key(ui_t *ui, ui_key_t k, ui_press_t p) { jump_key(ui, k, p, bm_target); }

const app_t app_bookmarks = {.name = "semne de carte", .enter = bm_enter, .key = bm_key, .draw = list_draw};
