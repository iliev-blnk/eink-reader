// Cititorul: pornește perifericele și conduce interfața (components/ui) cu
// butoanele. Interfața e C portabil; aici sunt doar ce ține de placă: ceasul,
// panoul, bateria, setările în NVS, somnul adânc și cifrele pentru diagnostic.
//
// MENIU ținut la o pornire la rece pornește în schimb aplicația de punere în
// funcțiune (bringup.c), iar o trezire din somnul ei revine tot acolo. Altfel o
// trezire din somnul adânc e o pornire nouă: interfața redeschide cartea citită
// înainte de somn (UI_BOOT_WAKE).
//
// Măsurătorile (FW-13): cu dosarul /perf pe card, marcajele de timp ale
// interfeței, ale panoului și ale butoanelor merg și în perf/NNNN.csv
// (perflog.h), scrise doar când bucla așteaptă o apăsare. Pe serial rămân
// rândurile PERF, ca până acum.
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "board.h"
#include "board_io.h"
#include "bringup.h"
#include "buttons.h"
#include "driver/spi_master.h"
#include "epd_uc8253.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_psram.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fuelgauge.h"
#include "gfx.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "perflog.h"
#include "sdcard.h"
#include "ui.h"
#include "usbdisk.h"

static const char *TAG = "cititor";

#define MOUNT_POINT "/sd"
#define RELEASE_WAIT_US (5 * 1000 * 1000)

static RTC_DATA_ATTR uint32_t s_boot_count;

static uint8_t s_fb[EPD_FB_SIZE];
static gfx_t s_gfx;
static bool s_epd_ok, s_sd_ok, s_battery_ok, s_gauge_ok, s_usb;
static char s_sd[32];

// ---- platforma interfeței ----

static void mount_sd(void);

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }

static void show(void *ctx, const gfx_t *g, ui_refresh_t kind, gfx_rect_t r)
{
    (void)ctx;
    if (!s_epd_ok) return;
    esp_err_t err = kind == UI_REFRESH_FULL ? epd_full_refresh(g->buf) : epd_partial_refresh(g->buf, r.x, r.y, r.w, r.h);
    const epd_timing_t *t = epd_last_timing();
    // La sfârșitul reîmprospătării; începutul e cu total_us mai devreme.
    uint64_t end = now_us();
    perflog_add(end, "refresh", t->full, t->total_us);
    perflog_add(end, "refresh_io", t->write_us, t->refresh_us);
    perflog_add(end, "refresh_rect", (uint32_t)t->x << 16 | (uint16_t)t->y, (uint32_t)t->w << 16 | (uint16_t)t->h);
    printf("PERF,refresh,full=%d,x=%d,y=%d,w=%d,h=%d,write_us=%" PRIu32 ",refresh_us=%" PRIu32 ",total_us=%" PRIu32
           ",err=%d\n",
           t->full, t->x, t->y, t->w, t->h, t->write_us, t->refresh_us, t->total_us, err);
}

static int battery_mv(void *ctx)
{
    (void)ctx;
    // Indicatorul măsoară direct celula; ADC-ul trece prin divizor și e mai imprecis.
    if (s_gauge_ok) {
        int mv = fuelgauge_read_mv();
        if (mv > 0) return mv;
    }
    return s_battery_ok ? battery_read_mv() : -1;
}

static bool load_settings(void *ctx, void *buf, size_t len)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = len;
    bool ok = nvs_get_blob(h, "settings", buf, &n) == ESP_OK && n == len;
    nvs_close(h);
    return ok;
}

static void save_settings(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "settings", buf, len) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

// ---- somnul ușor (FW-7) ----
//
// Somnul ușor automat: când niciun task nu are de lucru cel puțin
// CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP ticuri, taskul IDLE oprește ceasul
// procesoarelor până la următorul termen (tic, esp_timer) sau până la o sursă
// de trezire: un buton (buttons.c) sau BUSY-ul panoului (epd_uc8253.c).
// Perifericele care nu suportă somnul îl opresc singure cât lucrează (SPI, I²C)
// sau cu o blocare (discul USB). Frecvența rămâne fixă (minimul = maximul):
// fără o blocare CPU_FREQ_MAX ținută de interfață, paginarea ar rula la
// frecvența minimă. Frecvența variabilă e de încercat pe placă, cu E2.
static bool s_pm_ok;
static int64_t s_pm_since_us;
static volatile uint64_t s_light_us;
static volatile uint32_t s_light_n;

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
// Rulează în taskul IDLE, la ieșirea din somn, cu memoria cache poate oprită.
static esp_err_t IRAM_ATTR on_light_exit(int64_t slept_us, void *arg)
{
    (void)arg;
    s_light_us += (uint64_t)slept_us;
    s_light_n++;
    return ESP_OK;
}
#endif

static void init_pm(void)
{
#if CONFIG_PM_ENABLE
    const esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = true,
    };
    board_light_sleep_pins();    // înainte de primul somn
    esp_err_t err = esp_pm_configure(&pm);
    s_pm_ok = err == ESP_OK;
    if (!s_pm_ok) {
        ESP_LOGW(TAG, "somnul ușor nu pornește: %s", esp_err_to_name(err));
        return;
    }
    s_pm_since_us = esp_timer_get_time();
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    esp_pm_sleep_cbs_register_config_t cbs = {.exit_cb = on_light_exit};
    err = esp_pm_light_sleep_register_cbs(&cbs);
    if (err != ESP_OK) ESP_LOGW(TAG, "contorul somnului ușor: %s", esp_err_to_name(err));
#endif
#endif
}

static void deep_sleep(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "intră în somn adânc");
    if (s_epd_ok) epd_sleep();
    // Cât a dormit ușor de la pornire: ms și de câte ori.
    if (s_pm_ok) perflog_add(now_us(), "light_sleep", (uint32_t)(s_light_us / 1000), s_light_n);
    perflog_flush(now_us);
    if (s_sd_ok) sdcard_unmount();

    // Un buton încă apăsat (MENIU lung) ar trezi cipul imediat.
    int64_t t0 = esp_timer_get_time();
    while (buttons_any_down() && esp_timer_get_time() - t0 < RELEASE_WAIT_US) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(50));
    board_power_off();

    esp_err_t err = buttons_enable_wakeup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "trezirea din butoane nu se poate activa (%s), se repornește", esp_err_to_name(err));
        esp_restart();
    }
    fflush(stdout);
    // Somnul ușor automat lasă pornit temporizatorul cu care s-a trezit ultima
    // oară (esp_pm nu-l oprește), deja expirat: cipul s-ar trezi din somnul
    // adânc după câteva zeci de ms. Se oprește chiar aici, după ultimul
    // apel care mai poate bloca taskul și lăsa cipul să mai doarmă ușor o dată.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_deep_sleep_start();
}

// Discul USB (FW-12). Cât timp e pornit, cardul e demontat și jurnalul de
// măsurători rămâne în memorie (o scriere ar eșua și l-ar opri).
static bool usb_start(void *ctx)
{
    (void)ctx;
    perflog_flush(now_us);
    if (s_sd_ok) sdcard_unmount();
    s_sd_ok = false;
    const sdcard_config_t cfg = {.host = BOARD_SD_SPI_HOST, .cs = BOARD_SD_CS, .mount_point = MOUNT_POINT};
    if (usbdisk_start(&cfg, BOARD_VBUS_SENSE_GPIO) != ESP_OK) {
        mount_sd();
        return false;
    }
    s_usb = true;
    return true;
}

static ui_usb_t usb_state(void *ctx)
{
    (void)ctx;
    switch (usbdisk_state()) {
    case USBDISK_HOST: return UI_USB_HOST;
    case USBDISK_PAUSED: return UI_USB_PAUSED;
    case USBDISK_RELEASED: return UI_USB_RELEASED;
    default: return UI_USB_WAITING;
    }
}

static void usb_stop(void *ctx)
{
    (void)ctx;
    usbdisk_stop();
    s_usb = false;
    mount_sd();
}

// FW-4: un card montat care nu mai răspunde a fost scos; se demontează (ce
// era deschis pe el e pierdut oricum) și se încearcă montarea din nou, ca la
// pornire, cu aceleași două frecvențe.
static bool card_ready(void *ctx)
{
    (void)ctx;
    if (s_usb) return false;
    if (s_sd_ok && sdcard_check() == ESP_OK) return true;
    if (s_sd_ok) {
        ESP_LOGW(TAG, "cardul nu mai răspunde, se montează din nou");
        sdcard_unmount();
        s_sd_ok = false;
    }
    mount_sd();
    return s_sd_ok;
}

static int diag(void *ctx, char (*lines)[UI_DIAG_LINE], int max)
{
    (void)ctx;
    int n = 0;
    if (n < max)
        snprintf(lines[n++], UI_DIAG_LINE, "Heap: %u KB liberi, bloc %u KB",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    if (n < max)
        snprintf(lines[n++], UI_DIAG_LINE, "Minimul de heap: %u KB",
                 (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    if (n < max && esp_psram_is_initialized())
        snprintf(lines[n++], UI_DIAG_LINE, "PSRAM: %u KB liberi",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    if (n < max)
        snprintf(lines[n++], UI_DIAG_LINE, "Stiva: %u octeți nefolosiți", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (n < max) snprintf(lines[n++], UI_DIAG_LINE, "Card: %s", s_sd);
    if (n < max && s_gauge_ok) {
        int mv = fuelgauge_read_mv(), soc = fuelgauge_read_soc_permille();
        snprintf(lines[n++], UI_DIAG_LINE, "Indicator: %d,%d %%, %d mV", soc / 10, soc % 10, mv);
    }
    if (n < max && perflog_on())
        snprintf(lines[n++], UI_DIAG_LINE, "Jurnal: %s, %d în memorie", perflog_file(), perflog_pending());
    else if (n < max)
        snprintf(lines[n++], UI_DIAG_LINE, "Jurnal: oprit (fără /perf)");
    if (n < max) snprintf(lines[n++], UI_DIAG_LINE, "Pornirea nr. %" PRIu32, s_boot_count);
    if (n < max && s_pm_ok) {
        uint64_t up = (uint64_t)(esp_timer_get_time() - s_pm_since_us);
        snprintf(lines[n++], UI_DIAG_LINE, "Somn ușor: %u %% din %u s, %" PRIu32 " ori",
                 (unsigned)(up ? s_light_us * 100 / up : 0), (unsigned)(up / 1000000), s_light_n);
    } else if (n < max) {
        snprintf(lines[n++], UI_DIAG_LINE, "Somn ușor: oprit");
    }
    const epd_timing_t *t = epd_last_timing();
    if (n < max && s_epd_ok)
        snprintf(lines[n++], UI_DIAG_LINE, "Panoul: %s %" PRIu32 " ms", t->full ? "completă" : "parțială",
                 t->total_us / 1000);
    return n;
}

static void perf(void *ctx, const char *event, uint32_t a, uint32_t b)
{
    (void)ctx;
    uint64_t t = now_us();
    perflog_add(t, event, a, b);
    printf("PERF,%s,a=%" PRIu32 ",b=%" PRIu32 ",ms=%" PRIu32 "\n", event, a, b, (uint32_t)(t / 1000));
}

// ---- pornirea ----

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partiția e plină sau de la altă versiune: se pierd doar setările.
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "NVS: %s, setările nu se păstrează", esp_err_to_name(err));
}

static void init_bus_and_panel(void)
{
    ESP_ERROR_CHECK(board_spi_init());
    const epd_config_t epd = {
        .host = BOARD_EPD_SPI_HOST,
        .cs = BOARD_EPD_CS,
        .dc = BOARD_EPD_DC,
        .rst = BOARD_EPD_RST,
        .busy = BOARD_EPD_BUSY,
        .clock_hz = BOARD_EPD_SPI_HZ,
        .fast_full = true,
    };
    esp_err_t err = epd_init(&epd);
    s_epd_ok = err == ESP_OK;
    if (!s_epd_ok) ESP_LOGE(TAG, "panoul nu răspunde (%s); MENIU ținut la pornire: punerea în funcțiune",
                            esp_err_to_name(err));
}

static void mount_sd(void)
{
    const sdcard_config_t cfg = {.host = BOARD_SD_SPI_HOST, .cs = BOARD_SD_CS, .mount_point = MOUNT_POINT};
    sdcard_info_t sd;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = sdcard_mount(&cfg, &sd);
    printf("PERF,sd_mount,ok=%d,us=%lld\n", err == ESP_OK, (long long)(esp_timer_get_time() - t0));
    s_sd_ok = err == ESP_OK;
    if (s_sd_ok) snprintf(s_sd, sizeof s_sd, "%s, %d kHz", sd.name, sd.freq_khz);
    else snprintf(s_sd, sizeof s_sd, "lipsă (%s)", esp_err_to_name(err));
}

static ui_key_t to_key(button_t b)
{
    return b == BUTTON_PREV ? UI_KEY_PREV : b == BUTTON_NEXT ? UI_KEY_NEXT : UI_KEY_MENU;
}

void app_main(void)
{
    const gpio_num_t pins[BUTTON_COUNT] = {
        [BUTTON_PREV] = BOARD_BTN_PREV,
        [BUTTON_NEXT] = BOARD_BTN_NEXT,
        [BUTTON_MENU] = BOARD_BTN_MENU,
    };
    // Primul, ca butonul care a trezit cipul să fie văzut ca ținut și ignorat.
    ESP_ERROR_CHECK(buttons_init(pins));
    s_boot_count++;
    board_power_on();
    bool wake = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    if (wake ? bringup_slept() : buttons_any_down()) bringup_run();

    init_nvs();
    s_battery_ok = battery_init(BOARD_BAT_ADC_GPIO, BOARD_BAT_DIVIDER) == ESP_OK;
    s_gauge_ok = BOARD_I2C_SDA >= 0 && fuelgauge_init(BOARD_I2C_SDA, BOARD_I2C_SCL) == ESP_OK;
    init_bus_and_panel();
    mount_sd();
    if (s_sd_ok && perflog_open(MOUNT_POINT, now_us())) ESP_LOGI(TAG, "jurnalul de măsurători: %s", perflog_file());
    gfx_init(&s_gfx, s_fb, EPD_WIDTH, EPD_HEIGHT);

    const ui_platform_t platform = {
        .root = MOUNT_POINT, .now_ms = now_ms, .show = show, .battery_mv = battery_mv,
        .load_settings = load_settings, .save_settings = save_settings, .sleep = deep_sleep, .diag = diag,
        .perf = perf, .usb_start = usb_start, .usb_state = usb_state, .usb_stop = usb_stop,
        .card_ready = card_ready,
    };
    ui_t *ui = ui_init(&platform, &s_gfx);
    init_pm();
    ui_start(ui, wake ? UI_BOOT_WAKE : UI_BOOT_COLD);

    // Un singur task: apăsările au întâietate, iar între ele se face lucrul de
    // fundal (indexul paginilor), câte puțin, cât timp nu vine nicio apăsare.
    // Jurnalul se scrie pe card înainte de o așteptare, sau mai devreme dacă
    // memoria lui se umple.
    bool more = true;
    for (;;) {
        button_event_t ev;
        if (!s_usb && (!more || perflog_urgent())) perflog_flush(now_us);
        TickType_t wait = more ? 0 : pdMS_TO_TICKS(ui_wait_ms(ui));
        if (buttons_wait(&ev, wait)) {
            perflog_add((uint64_t)ev.edge_us, "btn_edge", ev.button, ev.press);
            ui_key(ui, to_key(ev.button), ev.press == PRESS_LONG ? UI_LONG : UI_SHORT);
            more = true;
        } else {
            more = ui_idle(ui);
            if (more) vTaskDelay(1);    // lasă și taskul IDLE să ruleze (watchdogul)
        }
    }
}
