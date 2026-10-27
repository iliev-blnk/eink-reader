#include "epd_uc8253.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "epd";

// Comenzile UC8253
#define CMD_PSR 0x00    // panel setting
#define CMD_POF 0x02    // power off
#define CMD_PON 0x04    // power on
#define CMD_DSLP 0x07   // deep sleep, needs check code 0xA5
#define CMD_DTM1 0x10   // image RAM "old": what is on screen now
#define CMD_DRF 0x12    // display refresh
#define CMD_DTM2 0x13   // image RAM "new": what should be on screen
#define CMD_CDI 0x50    // VCOM and data interval, border
#define CMD_PTL 0x90    // partial window
#define CMD_PTIN 0x91   // partial mode in
#define CMD_PTOUT 0x92  // partial mode out
#define CMD_CCSET 0xE0  // cascade setting; 0x02 = TSFIX: use the TSSET temperature
#define CMD_TSSET 0xE5  // forced temperature, selects the OTP waveform

// PSR, al doilea octet ca în exemplul producătorului pentru panoul ăsta.
// 0x1f: LUT din OTP, mod alb-negru, scanare în sus, deplasare la dreapta, booster pornit.
// Același lucru cu bitul 0 șters (0x1e) e un reset software.
#define PSR_NORMAL 0x1f
#define PSR_SOFT_RESET 0x1e
#define PSR_2 0x0d

// CDI: 0x97 face marginea albă (reîmprospătare completă), 0xD7 o lasă în aer
// ca să nu pâlpâie la reîmprospătarea parțială.
#define CDI_FULL 0x97
#define CDI_PARTIAL 0xD7

// Temperaturile forțate pentru formele de undă rapide (completă ~1,0 s, parțială ~0,35 s).
#define TSSET_FAST_FULL 0x5A
#define TSSET_PARTIAL 0x6E

// BUSY e în 0 cât lucrează controlerul. Timpi obișnuiți: pornire/oprire ~45 ms,
// completă rapidă ~1,0 s, completă normală ~3 s, parțială ~0,35 s.
#define BUSY_START_US 5000  // BUSY must go low this soon after a command
#define TIMEOUT_RESET_MS 100
#define TIMEOUT_POWER_MS 300
#define TIMEOUT_FULL_MS 6000
#define TIMEOUT_PARTIAL_MS 2000

#define SPI_CHUNK 4092  // bytes per transaction, fits the default bus limit

static struct {
    epd_config_t cfg;
    spi_device_handle_t spi;
    SemaphoreHandle_t busy_sem;
    uint8_t *scratch;  // buffer DMA pentru scrierea ferestrelor
    bool asleep;       // în somn adânc: îi trebuie un reset hardware
    bool ram_valid;    // RAM-ul „vechi” se potrivește cu ecranul, se poate face reîmprospătare parțială
    epd_timing_t timing;
} s;

static uint32_t since(int64_t t0)
{
    return (uint32_t)(esp_timer_get_time() - t0);
}

static void IRAM_ATTR busy_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s.busy_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

// ---- SPI ----

static esp_err_t send(bool is_data, const uint8_t *buf, size_t len)
{
    gpio_set_level(s.cfg.dc, is_data);
    while (len > 0) {
        size_t n = len < SPI_CHUNK ? len : SPI_CHUNK;
        spi_transaction_t t = {.length = n * 8, .tx_buffer = buf};
        // Cu întreruperi pentru datele mari, ca procesorul să poată sta; prin
        // interogare pentru octeții scurți de comandă, unde pregătirea ar costa
        // mai mult decât transferul.
        esp_err_t err = n > 32 ? spi_device_transmit(s.spi, &t) : spi_device_polling_transmit(s.spi, &t);
        ESP_RETURN_ON_ERROR(err, TAG, "spi transfer");
        buf += n;
        len -= n;
    }
    return ESP_OK;
}

static esp_err_t cmd(uint8_t c)
{
    return send(false, &c, 1);
}

static esp_err_t cmd_data(uint8_t c, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_ERROR(cmd(c), TAG, "cmd 0x%02x", c);
    return send(true, data, len);
}

static esp_err_t cmd_byte(uint8_t c, uint8_t d)
{
    return cmd_data(c, &d, 1);
}

// ---- BUSY ----

// Așteaptă până BUSY e în 1 (liber), dormind pe întreruperea de front crescător.
static esp_err_t wait_idle(int64_t deadline)
{
    xSemaphoreTake(s.busy_sem, 0);  // se aruncă un front vechi
    gpio_intr_enable(s.cfg.busy);
    esp_err_t err = ESP_OK;
    while (gpio_get_level(s.cfg.busy) == 0) {
        int64_t left_us = deadline - esp_timer_get_time();
        if (left_us <= 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        xSemaphoreTake(s.busy_sem, pdMS_TO_TICKS(left_us / 1000) + 1);
    }
    gpio_intr_disable(s.cfg.busy);
    return err;
}

// După o comandă care pune controlerul la lucru: așteaptă ca BUSY să treacă în
// 0, apoi înapoi în 1. busy_us primește timpul de la comandă până la liber.
static esp_err_t wait_busy(const char *what, int timeout_ms, uint32_t *busy_us)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(s.cfg.busy) != 0) {
        if (esp_timer_get_time() - t0 > BUSY_START_US) {
            ESP_LOGE(TAG, "%s: BUSY never went low, the panel isn't answering", what);
            return ESP_ERR_INVALID_RESPONSE;
        }
        esp_rom_delay_us(10);
    }
    esp_err_t err = wait_idle(t0 + (int64_t)timeout_ms * 1000);
    if (busy_us) *busy_us = since(t0);
    if (err != ESP_OK) ESP_LOGE(TAG, "%s: BUSY still low after %d ms", what, timeout_ms);
    return err;
}

// ---- panoul ----

static void hw_reset(void)
{
    gpio_hold_dis(s.cfg.rst);
    gpio_set_level(s.cfg.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s.cfg.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s.cfg.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    s.asleep = false;
}

// Stare cunoscută: reset hardware la trezirea din somnul adânc, altfel reset
// software (care renunță și la o temperatură forțată), apoi setarea panoului.
static esp_err_t panel_setup(void)
{
    if (s.asleep) {
        hw_reset();
    } else {
        ESP_RETURN_ON_ERROR(cmd_data(CMD_PSR, (const uint8_t[]){PSR_SOFT_RESET, PSR_2}, 2), TAG, "soft reset");
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_RETURN_ON_ERROR(wait_idle(esp_timer_get_time() + TIMEOUT_RESET_MS * 1000), TAG, "BUSY low after reset");
    return cmd_data(CMD_PSR, (const uint8_t[]){PSR_NORMAL, PSR_2}, 2);
}

static esp_err_t power(bool on, uint32_t *busy_us)
{
    ESP_RETURN_ON_ERROR(cmd(on ? CMD_PON : CMD_POF), TAG, "power cmd");
    return wait_busy(on ? "power on" : "power off", TIMEOUT_POWER_MS, busy_us);
}

// Pornire, reîmprospătare, oprire. Oprește chiar și după ce reîmprospătarea a depășit timpul.
static esp_err_t run_refresh(epd_timing_t *t, int timeout_ms)
{
    ESP_RETURN_ON_ERROR(power(true, &t->power_on_us), TAG, "power on");
    esp_err_t err = cmd(CMD_DRF);
    if (err == ESP_OK) err = wait_busy("refresh", timeout_ms, &t->refresh_us);
    esp_err_t off = power(false, &t->power_off_us);
    return err != ESP_OK ? err : off;
}

// x și w trebuie să fie multipli de 8. Coloana de final e inclusă, deci cei 3
// biți de jos ajung 1; ultimul octet (0x01) ține porțile scanând și în afara ferestrei.
static esp_err_t set_window(int x, int y, int w, int h)
{
    int xe = x + w - 1, ye = y + h - 1;
    const uint8_t d[7] = {x, xe, y >> 8, y & 0xff, ye >> 8, ye & 0xff, 0x01};
    return cmd_data(CMD_PTL, d, sizeof d);
}

// Scrie o fereastră din framebuffer într-unul din RAM-urile de imagine.
static esp_err_t write_ram(uint8_t ram, const uint8_t *fb, int x, int y, int w, int h)
{
    const int stride = EPD_WIDTH / 8, bw = w / 8;
    for (int r = 0; r < h; r++) memcpy(s.scratch + r * bw, fb + (y + r) * stride + x / 8, bw);
    ESP_RETURN_ON_ERROR(cmd(CMD_PTIN), TAG, "partial in");
    ESP_RETURN_ON_ERROR(set_window(x, y, w, h), TAG, "window");
    ESP_RETURN_ON_ERROR(cmd(ram), TAG, "ram");
    ESP_RETURN_ON_ERROR(send(true, s.scratch, (size_t)bw * h), TAG, "image data");
    return cmd(CMD_PTOUT);
}

// ---- publice ----

esp_err_t epd_init(const epd_config_t *cfg)
{
    s.cfg = *cfg;
    s.asleep = true;  // stare necunoscută: se începe cu un reset hardware
    s.ram_valid = false;

    gpio_set_level(cfg->rst, 1);
    gpio_set_level(cfg->dc, 1);
    const gpio_config_t out = {
        .pin_bit_mask = BIT64(cfg->dc) | BIT64(cfg->rst),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "dc/rst pins");
    // RST a fost ținut în 1 în ultimul somn adânc. Se eliberează abia acum, când
    // pinul e ieșire în 1, altfel ar coborî scurt în 0.
    gpio_hold_dis(cfg->rst);
    // Pull-up: un BUSY neconectat se citește ca liber, deci verificarea de
    // pornire de mai jos raportează „nu răspunde” în loc să se blocheze.
    const gpio_config_t in = {
        .pin_bit_mask = BIT64(cfg->busy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "busy pin");

    if (!s.busy_sem) s.busy_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s.busy_sem, ESP_ERR_NO_MEM, TAG, "semaphore");
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(cfg->busy, busy_isr, NULL), TAG, "busy isr");
    gpio_intr_disable(cfg->busy);

    if (!s.scratch) s.scratch = heap_caps_malloc(EPD_FB_SIZE, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s.scratch, ESP_ERR_NO_MEM, TAG, "scratch buffer");

    const spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = cfg->clock_hz,
        .spics_io_num = cfg->cs,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(cfg->host, &dev, &s.spi), TAG, "spi device");

    ESP_RETURN_ON_ERROR(panel_setup(), TAG, "setup");
    uint32_t on_us = 0, off_us = 0;
    ESP_RETURN_ON_ERROR(power(true, &on_us), TAG, "power-on check");
    ESP_RETURN_ON_ERROR(power(false, &off_us), TAG, "power-off check");
    ESP_LOGI(TAG, "panel answers: power on %lu us, power off %lu us", (unsigned long)on_us,
             (unsigned long)off_us);
    return ESP_OK;
}

esp_err_t epd_full_refresh(const uint8_t *fb)
{
    int64_t t0 = esp_timer_get_time();
    epd_timing_t t = {.full = true, .w = EPD_WIDTH, .h = EPD_HEIGHT};
    if (s.asleep) ESP_RETURN_ON_ERROR(panel_setup(), TAG, "wake");

    // Ambele RAM-uri primesc imaginea nouă: forma de undă completă redesenează
    // fiecare pixel, iar după aceea „vechiul” se potrivește deja cu ecranul
    // pentru următoarea reîmprospătare parțială.
    int64_t w0 = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(write_ram(CMD_DTM1, fb, 0, 0, EPD_WIDTH, EPD_HEIGHT), TAG, "old ram");
    ESP_RETURN_ON_ERROR(write_ram(CMD_DTM2, fb, 0, 0, EPD_WIDTH, EPD_HEIGHT), TAG, "new ram");
    t.write_us = since(w0);

    if (s.cfg.fast_full) {
        ESP_RETURN_ON_ERROR(cmd_byte(CMD_CCSET, 0x02), TAG, "ccset");
        ESP_RETURN_ON_ERROR(cmd_byte(CMD_TSSET, TSSET_FAST_FULL), TAG, "tsset");
    }
    ESP_RETURN_ON_ERROR(cmd_byte(CMD_CDI, CDI_FULL), TAG, "cdi");
    esp_err_t err = run_refresh(&t, TIMEOUT_FULL_MS);
    if (s.cfg.fast_full && err == ESP_OK) err = panel_setup();  // înapoi la temperatura senzorului
    s.ram_valid = err == ESP_OK;

    t.total_us = since(t0);
    s.timing = t;
    return err;
}

esp_err_t epd_partial_refresh(const uint8_t *fb, int x, int y, int w, int h)
{
    if (!s.ram_valid || s.asleep) return epd_full_refresh(fb);

    // Tăiere la marginile panoului, apoi lărgire la octeți întregi.
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > EPD_WIDTH) w = EPD_WIDTH - x;
    if (y + h > EPD_HEIGHT) h = EPD_HEIGHT - y;
    if (w <= 0 || h <= 0) return ESP_OK;
    int x1 = (x + w + 7) & ~7;
    x &= ~7;
    w = x1 - x;

    int64_t t0 = esp_timer_get_time();
    epd_timing_t t = {.full = false, .x = x, .y = y, .w = w, .h = h};

    int64_t w0 = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(write_ram(CMD_DTM2, fb, x, y, w, h), TAG, "new ram");
    t.write_us = since(w0);

    ESP_RETURN_ON_ERROR(cmd(CMD_PTIN), TAG, "partial in");
    ESP_RETURN_ON_ERROR(set_window(x, y, w, h), TAG, "window");
    ESP_RETURN_ON_ERROR(cmd_byte(CMD_CCSET, 0x02), TAG, "ccset");
    ESP_RETURN_ON_ERROR(cmd_byte(CMD_TSSET, TSSET_PARTIAL), TAG, "tsset");
    ESP_RETURN_ON_ERROR(cmd_byte(CMD_CDI, CDI_PARTIAL), TAG, "cdi");
    esp_err_t err = run_refresh(&t, TIMEOUT_PARTIAL_MS);
    if (err == ESP_OK) err = panel_setup();
    if (err == ESP_OK) err = cmd(CMD_PTOUT);

    // „Vechiul” ține pasul cu ecranul pentru următoarea reîmprospătare parțială.
    if (err == ESP_OK) {
        w0 = esp_timer_get_time();
        err = write_ram(CMD_DTM1, fb, x, y, w, h);
        t.write_us += since(w0);
    }
    s.ram_valid = err == ESP_OK;

    t.total_us = since(t0);
    s.timing = t;
    return err;
}

esp_err_t epd_sleep(void)
{
    if (s.asleep) return ESP_OK;
    // Fiecare reîmprospătare se termină cu oprirea, deci controlerul e deja liber.
    ESP_RETURN_ON_ERROR(cmd_byte(CMD_DSLP, 0xA5), TAG, "deep sleep");
    s.asleep = true;
    s.ram_valid = false;
    // Un RST lăsat în aer ar putea scoate panoul din somnul adânc cât doarme ESP32.
    gpio_hold_en(s.cfg.rst);
    return ESP_OK;
}

const epd_timing_t *epd_last_timing(void)
{
    return &s.timing;
}
