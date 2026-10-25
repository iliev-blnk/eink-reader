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

// UC8253 commands
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

// PSR, second byte as in the vendor sample for this panel.
// 0x1f: LUT from OTP, black/white mode, scan up, shift right, booster on.
// The same with bit 0 cleared (0x1e) is a soft reset.
#define PSR_NORMAL 0x1f
#define PSR_SOFT_RESET 0x1e
#define PSR_2 0x0d

// CDI: 0x97 drives the border white (full refresh), 0xD7 leaves it floating
// so it doesn't flicker on partial refresh.
#define CDI_FULL 0x97
#define CDI_PARTIAL 0xD7

// Forced temperatures for the fast waveforms (full ~1.0 s, partial ~0.35 s).
#define TSSET_FAST_FULL 0x5A
#define TSSET_PARTIAL 0x6E

// BUSY is low while the controller works. Typical times: power on/off ~45 ms,
// fast full ~1.0 s, normal full ~3 s, partial ~0.35 s.
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
    uint8_t *scratch;  // DMA buffer for window writes
    bool asleep;       // in deep sleep: needs a hardware reset
    bool ram_valid;    // "old" RAM matches the screen, partial refresh possible
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
        // Interrupt-driven for bulk data so the CPU can idle; polling for the
        // short command bytes, where the setup would cost more than the transfer.
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

// Waits until BUSY is high (idle), sleeping on the rising-edge interrupt.
static esp_err_t wait_idle(int64_t deadline)
{
    xSemaphoreTake(s.busy_sem, 0);  // drop a stale edge
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

// After a command that makes the controller work: waits for BUSY to go low,
// then for it to go high again. busy_us gets the time from the command to idle.
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

// ---- panel ----

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

// Known state: hardware reset when waking from deep sleep, soft reset
// otherwise (this also drops a forced temperature), then the panel setting.
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

// Power on, refresh, power off. Powers off even after a refresh timeout.
static esp_err_t run_refresh(epd_timing_t *t, int timeout_ms)
{
    ESP_RETURN_ON_ERROR(power(true, &t->power_on_us), TAG, "power on");
    esp_err_t err = cmd(CMD_DRF);
    if (err == ESP_OK) err = wait_busy("refresh", timeout_ms, &t->refresh_us);
    esp_err_t off = power(false, &t->power_off_us);
    return err != ESP_OK ? err : off;
}

// x and w must be multiples of 8. The end column is inclusive, so its low
// 3 bits end up 1; the last byte (0x01) keeps the gates scanning outside the window.
static esp_err_t set_window(int x, int y, int w, int h)
{
    int xe = x + w - 1, ye = y + h - 1;
    const uint8_t d[7] = {x, xe, y >> 8, y & 0xff, ye >> 8, ye & 0xff, 0x01};
    return cmd_data(CMD_PTL, d, sizeof d);
}

// Writes a window of the framebuffer into one of the image RAMs.
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

// ---- public ----

esp_err_t epd_init(const epd_config_t *cfg)
{
    s.cfg = *cfg;
    s.asleep = true;  // unknown state: start with a hardware reset
    s.ram_valid = false;

    gpio_set_level(cfg->rst, 1);
    gpio_set_level(cfg->dc, 1);
    const gpio_config_t out = {
        .pin_bit_mask = BIT64(cfg->dc) | BIT64(cfg->rst),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "dc/rst pins");
    // RST was held high through the last deep sleep. Release it only now that
    // the pin is an output at 1, or it would glitch low.
    gpio_hold_dis(cfg->rst);
    // Pull-up: an unconnected BUSY reads idle, so the power-on check below
    // reports "not answering" instead of hanging.
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

    // Both RAMs get the new image: the full waveform redraws every pixel, and
    // afterwards "old" already matches the screen for the next partial refresh.
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
    if (s.cfg.fast_full && err == ESP_OK) err = panel_setup();  // back to the sensor temperature
    s.ram_valid = err == ESP_OK;

    t.total_us = since(t0);
    s.timing = t;
    return err;
}

esp_err_t epd_partial_refresh(const uint8_t *fb, int x, int y, int w, int h)
{
    if (!s.ram_valid || s.asleep) return epd_full_refresh(fb);

    // Clip to the panel, then widen to whole bytes.
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

    // Keep "old" in step with the screen for the next partial refresh.
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
    // Every refresh ends with power off, so the controller is already idle.
    ESP_RETURN_ON_ERROR(cmd_byte(CMD_DSLP, 0xA5), TAG, "deep sleep");
    s.asleep = true;
    s.ram_valid = false;
    // A floating RST could pull the panel out of deep sleep while the ESP32 sleeps.
    gpio_hold_en(s.cfg.rst);
    return ESP_OK;
}

const epd_timing_t *epd_last_timing(void)
{
    return &s.timing;
}
