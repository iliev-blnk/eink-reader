// Phase 0 bring-up app. Checks every part of the prototype once and shows the
// result on the panel: chip and memory, SD card, battery ADC, buttons with
// partial refresh, and deep sleep with button wake. Refresh timings go to the
// serial log as PERF lines for experiment E1.
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "board.h"
#include "buttons.h"
#include "driver/spi_master.h"
#include "epd_uc8253.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "screen_bringup.h"
#include "sdcard.h"

static const char *TAG = "bringup";

#define MOUNT_POINT "/sd"
#define RELEASE_WAIT_US (5 * 1000 * 1000)

// Survives deep sleep, so it also shows that RTC memory is kept.
static RTC_DATA_ATTR uint32_t s_boot_count;

static uint8_t s_fb[EPD_FB_SIZE];
static gfx_t s_gfx;
static bool s_epd_ok;
static bool s_sd_ok;

static char s_chip[40], s_memory[40], s_sd[40], s_battery[32], s_wake[40], s_button[16], s_timing[40];
static bringup_info_t s_info = {
    .board = BOARD_NAME,
    .chip = s_chip,
    .memory = s_memory,
    .sd = s_sd,
    .battery = s_battery,
    .wake = s_wake,
    .last_button = s_button,
    .last_timing = s_timing,
};

static void describe_chip(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    snprintf(s_chip, sizeof s_chip, "ESP32-S3 v%d.%d, %d cores", chip.revision / 100, chip.revision % 100,
             chip.cores);

    uint32_t flash = 0;
    esp_flash_get_physical_size(NULL, &flash);
    if (esp_psram_is_initialized()) {
        snprintf(s_memory, sizeof s_memory, "%" PRIu32 "M flash, %uM PSRAM", flash >> 20,
                 (unsigned)(esp_psram_get_size() >> 20));
    } else {
        snprintf(s_memory, sizeof s_memory, "%" PRIu32 "M flash, no PSRAM", flash >> 20);
    }
}

static const char *reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB reset";
    default: return "other reset";
    }
}

// Call after buttons_init(), which knows the pins.
static void describe_wake(void)
{
    s_boot_count++;
    const char *cause = reset_reason();
    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        int b = buttons_from_wake_mask(esp_sleep_get_ext1_wakeup_status());
        cause = b >= 0 ? button_name(b) : "button";
    }
    snprintf(s_wake, sizeof s_wake, "%s · boot %" PRIu32, cause, s_boot_count);
}

static void read_battery(void)
{
    int mv = battery_read_mv();
    printf("PERF,battery,mv=%d\n", mv);
    if (mv < 0) {
        snprintf(s_battery, sizeof s_battery, "ADC error");
    } else if (mv < 2500) {
        // Below any usable Li-ion voltage: nothing on the divider.
        snprintf(s_battery, sizeof s_battery, "none (%d.%02d V)", mv / 1000, mv % 1000 / 10);
    } else {
        snprintf(s_battery, sizeof s_battery, "%d.%02d V", mv / 1000, mv % 1000 / 10);
    }
}

static int count_root_files(void)
{
    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) return -1;
    int n = 0;
    for (struct dirent *e; (e = readdir(dir));) {
        if (e->d_type == DT_REG) n++;
    }
    closedir(dir);
    return n;
}

// Appends one line per boot, so the card shows the device really wrote to it.
static void log_boot_to_sd(void)
{
    FILE *f = fopen(MOUNT_POINT "/bringup.txt", "a");
    if (!f) {
        ESP_LOGW(TAG, "can't open bringup.txt for writing");
        return;
    }
    fprintf(f, "boot %" PRIu32 ", wake %s, battery %s\n", s_boot_count, s_wake, s_battery);
    fclose(f);
}

static void mount_sd(void)
{
    const sdcard_config_t cfg = {.host = BOARD_SPI_HOST, .cs = BOARD_SD_CS, .mount_point = MOUNT_POINT};
    sdcard_info_t sd;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = sdcard_mount(&cfg, &sd);
    printf("PERF,sd_mount,ok=%d,us=%lld\n", err == ESP_OK, (long long)(esp_timer_get_time() - t0));

    s_sd_ok = err == ESP_OK;
    if (err == ESP_FAIL) {
        snprintf(s_sd, sizeof s_sd, "not FAT32 formatted");
    } else if (err == ESP_ERR_TIMEOUT) {
        snprintf(s_sd, sizeof s_sd, "no card (timeout)");
    } else if (err != ESP_OK) {
        snprintf(s_sd, sizeof s_sd, "failed: 0x%x", err);
    } else {
        unsigned gb10 = (unsigned)(sd.capacity_bytes / 100000000);  // tenths of a GB
        snprintf(s_sd, sizeof s_sd, "OK, %u.%u GB, %d files", gb10 / 10, gb10 % 10, count_root_files());
        log_boot_to_sd();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "SD card: %s", esp_err_to_name(err));
}

// ---- display ----

static void log_refresh(esp_err_t err)
{
    const epd_timing_t *t = epd_last_timing();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "refresh failed: %s", esp_err_to_name(err));
        return;
    }
    printf("PERF,%s,x=%d,y=%d,w=%d,h=%d,write_us=%" PRIu32 ",on_us=%" PRIu32 ",refresh_us=%" PRIu32
           ",off_us=%" PRIu32 ",total_us=%" PRIu32 "\n",
           t->full ? "epd_full" : "epd_partial", t->x, t->y, t->w, t->h, t->write_us, t->power_on_us,
           t->refresh_us, t->power_off_us, t->total_us);
    // Shown on the next update: a screen can't show its own refresh time.
    snprintf(s_timing, sizeof s_timing, "prev: %s %" PRIu32 " ms", t->full ? "full" : "partial",
             t->total_us / 1000);
}

static void show_full(void)
{
    gfx_rect_t r;
    gfx_take_dirty(&s_gfx, &r);  // the whole screen goes out anyway
    if (s_epd_ok) log_refresh(epd_full_refresh(s_fb));
}

static void show_partial(void)
{
    gfx_rect_t r;
    if (!gfx_take_dirty(&s_gfx, &r) || !s_epd_ok) return;
    log_refresh(epd_partial_refresh(s_fb, r.x, r.y, r.w, r.h));
}

static void draw_all(void)
{
    screen_bringup_draw(&s_gfx, &s_info);
    show_full();
}

// ---- sleep ----

static void go_to_sleep(void)
{
    ESP_LOGI(TAG, "going to deep sleep");
    screen_sleep_draw(&s_gfx);
    show_full();
    if (s_epd_ok) epd_sleep();
    if (s_sd_ok) sdcard_unmount();

    // A button still down would wake the chip right away.
    int64_t t0 = esp_timer_get_time();
    while (buttons_any_down() && esp_timer_get_time() - t0 < RELEASE_WAIT_US) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(50));  // let the contacts settle

    esp_err_t err = buttons_enable_wakeup();
    if (err != ESP_OK) {
        // Without a wake source only the reset button would bring it back.
        ESP_LOGE(TAG, "can't arm button wake-up (%s), restarting instead", esp_err_to_name(err));
        esp_restart();
    }
    fflush(stdout);
    esp_deep_sleep_start();
}

// ---- main ----

void app_main(void)
{
    const gpio_num_t pins[BUTTON_COUNT] = {
        [BUTTON_PREV] = BOARD_BTN_PREV,
        [BUTTON_NEXT] = BOARD_BTN_NEXT,
        [BUTTON_MENU] = BOARD_BTN_MENU,
    };
    // First, so the button that woke the chip is seen as held and ignored.
    ESP_ERROR_CHECK(buttons_init(pins));
    describe_chip();
    describe_wake();
    snprintf(s_button, sizeof s_button, "ready");
    snprintf(s_timing, sizeof s_timing, "prev: none yet");

    if (battery_init(BOARD_BAT_ADC_GPIO, BOARD_BAT_DIVIDER) == ESP_OK) {
        read_battery();
    } else {
        snprintf(s_battery, sizeof s_battery, "ADC error");
    }

    // The SD card's CS must be high before any traffic for the panel, or the
    // card could take part of it as commands.
    gpio_set_level(BOARD_SD_CS, 1);
    gpio_config(&(gpio_config_t){.pin_bit_mask = BIT64(BOARD_SD_CS), .mode = GPIO_MODE_OUTPUT});

    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_SPI_SCK,
        .mosi_io_num = BOARD_SPI_MOSI,
        .miso_io_num = BOARD_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_FB_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    const epd_config_t epd = {
        .host = BOARD_SPI_HOST,
        .cs = BOARD_EPD_CS,
        .dc = BOARD_EPD_DC,
        .rst = BOARD_EPD_RST,
        .busy = BOARD_EPD_BUSY,
        .clock_hz = BOARD_EPD_SPI_HZ,
        .fast_full = true,
    };
    esp_err_t err = epd_init(&epd);
    s_epd_ok = err == ESP_OK;
    if (!s_epd_ok) {
        ESP_LOGE(TAG, "e-paper not answering (%s). Check BUSY=GPIO%d, RST=GPIO%d, DC=GPIO%d, CS=GPIO%d, "
                      "SCK=GPIO%d, MOSI=GPIO%d, 3V3, GND, and the module's 4-wire SPI setting.",
                 esp_err_to_name(err), BOARD_EPD_BUSY, BOARD_EPD_RST, BOARD_EPD_DC, BOARD_EPD_CS, BOARD_SPI_SCK,
                 BOARD_SPI_MOSI);
    }

    mount_sd();

    ESP_LOGI(TAG, "%s | %s | %s | SD %s | battery %s | wake %s", s_info.board, s_chip, s_memory, s_sd, s_battery,
             s_wake);

    gfx_init(&s_gfx, s_fb, EPD_WIDTH, EPD_HEIGHT);
    draw_all();
    ESP_LOGI(TAG, "ready: PREV/NEXT count (hold = 10), MENU redraws, hold MENU to sleep");

    for (;;) {
        button_event_t ev;
        if (!buttons_wait(&ev, portMAX_DELAY)) continue;
        bool hold = ev.press == PRESS_LONG;
        ESP_LOGI(TAG, "%s %s", button_name(ev.button), hold ? "long" : "short");

        if (ev.button == BUTTON_MENU) {
            if (hold) go_to_sleep();  // doesn't return
            // Re-check what may have changed: battery, and a card put in since boot.
            read_battery();
            if (!s_sd_ok) mount_sd();
            snprintf(s_button, sizeof s_button, "MENU");
            draw_all();
            continue;
        }

        int step = hold ? 10 : 1;
        s_info.counter += ev.button == BUTTON_NEXT ? step : -step;
        snprintf(s_button, sizeof s_button, "%s%s", button_name(ev.button), hold ? " long" : "");
        screen_bringup_draw_counter(&s_gfx, &s_info);
        show_partial();
    }
}
