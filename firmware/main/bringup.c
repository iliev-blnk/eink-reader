// Aplicația de punere în funcțiune din Faza 0, pornită ținând MENIU apăsat la o
// pornire la rece (main.c). Verifică o dată fiecare parte a prototipului și arată
// rezultatul pe panou: cipul și memoria, cardul SD, ADC-ul bateriei, butoanele
// cu reîmprospătare parțială și somnul adânc cu trezire din buton. Timpii de
// reîmprospătare merg în jurnalul serial ca rânduri PERF pentru experimentul E1.
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "bringup.h"
#include "board.h"
#include "board_io.h"
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
#include "fuelgauge.h"
#include "gfx.h"
#include "screen_bringup.h"
#include "sdcard.h"

static const char *TAG = "bringup";

#define MOUNT_POINT "/sd"
#define RELEASE_WAIT_US (5 * 1000 * 1000)

// Supraviețuiește somnului adânc, deci arată și că memoria RTC se păstrează.
static RTC_DATA_ATTR uint32_t s_boot_count;
// Somnul l-a început aplicația asta, deci trezirea revine tot aici (bringup_slept).
static RTC_DATA_ATTR bool s_slept;

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
    snprintf(s_chip, sizeof s_chip, "ESP32-S3 v%d.%d, %d nuclee", chip.revision / 100, chip.revision % 100,
             chip.cores);

    uint32_t flash = 0;
    esp_flash_get_physical_size(NULL, &flash);
    if (esp_psram_is_initialized()) {
        snprintf(s_memory, sizeof s_memory, "%" PRIu32 "M flash, %uM PSRAM", flash >> 20,
                 (unsigned)(esp_psram_get_size() >> 20));
    } else {
        snprintf(s_memory, sizeof s_memory, "%" PRIu32 "M flash, fără PSRAM", flash >> 20);
    }
}

static const char *reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "alimentare";
    case ESP_RST_EXT: return "pin de reset";
    case ESP_RST_SW: return "repornire";
    case ESP_RST_PANIC: return "eroare";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "somn adânc";
    case ESP_RST_BROWNOUT: return "subtensiune";
    case ESP_RST_USB: return "reset USB";
    default: return "alt reset";
    }
}

// Se apelează după buttons_init(), care știe pinii.
static void describe_wake(void)
{
    s_boot_count++;
    const char *cause = reset_reason();
    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        int b = buttons_from_wake_mask(esp_sleep_get_ext1_wakeup_status());
        cause = b >= 0 ? button_name(b) : "buton";
    }
    snprintf(s_wake, sizeof s_wake, "%s · nr. %" PRIu32, cause, s_boot_count);
}

static bool s_gauge_ok;

static void read_battery(void)
{
    // Pe placa proprie, indicatorul MAX17048: tensiunea celulei și procentul.
    if (s_gauge_ok) {
        int mv = fuelgauge_read_mv(), soc = fuelgauge_read_soc_permille();
        printf("PERF,battery,mv=%d,soc=%d,adc_mv=%d\n", mv, soc, battery_read_mv());
        if (mv > 0 && soc >= 0) {
            snprintf(s_battery, sizeof s_battery, "%d,%02d V · %d %%", mv / 1000, mv % 1000 / 10, soc / 10);
            return;
        }
    }
    int mv = battery_read_mv();
    printf("PERF,battery,mv=%d\n", mv);
    if (mv < 0) {
        snprintf(s_battery, sizeof s_battery, "eroare ADC");
    } else if (mv < 2500) {
        // Sub orice tensiune utilizabilă de Li-ion: nimic pe divizor.
        snprintf(s_battery, sizeof s_battery, "lipsă (%d,%02d V)", mv / 1000, mv % 1000 / 10);
    } else {
        snprintf(s_battery, sizeof s_battery, "%d,%02d V", mv / 1000, mv % 1000 / 10);
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

// Adaugă un rând la fiecare pornire, deci cardul arată că dispozitivul chiar a scris pe el.
static void log_boot_to_sd(void)
{
    FILE *f = fopen(MOUNT_POINT "/bringup.txt", "a");
    if (!f) {
        ESP_LOGW(TAG, "nu se poate deschide bringup.txt pentru scriere");
        return;
    }
    fprintf(f, "pornirea %" PRIu32 ", trezire: %s, baterie: %s\n", s_boot_count, s_wake, s_battery);
    fclose(f);
}

static void mount_sd(void)
{
    const sdcard_config_t cfg = {.host = BOARD_SD_SPI_HOST, .cs = BOARD_SD_CS, .mount_point = MOUNT_POINT};
    sdcard_info_t sd;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = sdcard_mount(&cfg, &sd);
    printf("PERF,sd_mount,ok=%d,us=%lld\n", err == ESP_OK, (long long)(esp_timer_get_time() - t0));

    s_sd_ok = err == ESP_OK;
    if (err == ESP_FAIL) {
        snprintf(s_sd, sizeof s_sd, "nu e formatat FAT32");
    } else if (err == ESP_ERR_TIMEOUT) {
        snprintf(s_sd, sizeof s_sd, "niciun card (timp depășit)");
    } else if (err != ESP_OK) {
        snprintf(s_sd, sizeof s_sd, "eroare: 0x%x", err);
    } else {
        unsigned gb10 = (unsigned)(sd.capacity_bytes / 100000000);  // zecimi de GB
        snprintf(s_sd, sizeof s_sd, "OK, %u,%u GB, %d fișiere", gb10 / 10, gb10 % 10, count_root_files());
        log_boot_to_sd();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "cardul SD: %s", esp_err_to_name(err));
}

// ---- ecranul ----

static void log_refresh(esp_err_t err)
{
    const epd_timing_t *t = epd_last_timing();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reîmprospătarea a eșuat: %s", esp_err_to_name(err));
        return;
    }
    printf("PERF,%s,x=%d,y=%d,w=%d,h=%d,write_us=%" PRIu32 ",on_us=%" PRIu32 ",refresh_us=%" PRIu32
           ",off_us=%" PRIu32 ",total_us=%" PRIu32 "\n",
           t->full ? "epd_full" : "epd_partial", t->x, t->y, t->w, t->h, t->write_us, t->power_on_us,
           t->refresh_us, t->power_off_us, t->total_us);
    // Se arată la următoarea actualizare: un ecran nu-și poate arăta propriul timp de reîmprospătare.
    snprintf(s_timing, sizeof s_timing, "ultima: %s %" PRIu32 " ms", t->full ? "completă" : "parțială",
             t->total_us / 1000);
}

static void show_full(void)
{
    gfx_rect_t r;
    gfx_take_dirty(&s_gfx, &r);  // oricum pleacă tot ecranul
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

// ---- somnul ----

static void go_to_sleep(void)
{
    ESP_LOGI(TAG, "intră în somn adânc");
    screen_sleep_draw(&s_gfx);
    show_full();
    if (s_epd_ok) epd_sleep();
    if (s_sd_ok) sdcard_unmount();
    board_power_off();

    // Un buton încă apăsat ar trezi cipul imediat.
    int64_t t0 = esp_timer_get_time();
    while (buttons_any_down() && esp_timer_get_time() - t0 < RELEASE_WAIT_US) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(50));  // contactele se liniștesc

    esp_err_t err = buttons_enable_wakeup();
    if (err != ESP_OK) {
        // Fără o sursă de trezire, doar butonul de reset l-ar mai aduce înapoi.
        ESP_LOGE(TAG, "trezirea din butoane nu se poate activa (%s), se repornește", esp_err_to_name(err));
        esp_restart();
    }
    s_slept = true;
    fflush(stdout);
    // Temporizatorul lăsat de somnul ușor automat ar trezi cipul imediat (ca în main.c).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_deep_sleep_start();
}

// ---- principal ----

bool bringup_slept(void) { return s_slept; }

void bringup_run(void)
{
    // Butoanele le-a pornit deja main.c, care a văzut MENIU ținut (sau o trezire din somnul de aici).
    s_slept = false;
    describe_chip();
    describe_wake();
    snprintf(s_button, sizeof s_button, "gata");
    snprintf(s_timing, sizeof s_timing, "ultima: încă niciuna");

    s_gauge_ok = BOARD_I2C_SDA >= 0 && fuelgauge_init(BOARD_I2C_SDA, BOARD_I2C_SCL) == ESP_OK;
    if (battery_init(BOARD_BAT_ADC_GPIO, BOARD_BAT_DIVIDER) == ESP_OK) {
        read_battery();
    } else {
        snprintf(s_battery, sizeof s_battery, "eroare ADC");
    }

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
    if (!s_epd_ok) {
        ESP_LOGE(TAG, "panoul e-paper nu răspunde (%s). Verifică BUSY=GPIO%d, RST=GPIO%d, DC=GPIO%d, CS=GPIO%d, "
                      "SCK=GPIO%d, MOSI=GPIO%d, 3V3, GND și setarea SPI pe 4 fire a modulului.",
                 esp_err_to_name(err), BOARD_EPD_BUSY, BOARD_EPD_RST, BOARD_EPD_DC, BOARD_EPD_CS, BOARD_EPD_SCK,
                 BOARD_EPD_MOSI);
    }

    mount_sd();

    ESP_LOGI(TAG, "%s | %s | %s | SD %s | baterie %s | trezire %s", s_info.board, s_chip, s_memory, s_sd, s_battery,
             s_wake);

    gfx_init(&s_gfx, s_fb, EPD_WIDTH, EPD_HEIGHT);
    draw_all();
    ESP_LOGI(TAG, "gata: ÎNAPOI/ÎNAINTE numără (lung = 10), MENIU redesenează, MENIU lung adoarme");

    for (;;) {
        button_event_t ev;
        if (!buttons_wait(&ev, portMAX_DELAY)) continue;
        bool hold = ev.press == PRESS_LONG;
        ESP_LOGI(TAG, "%s %s", button_name(ev.button), hold ? "lung" : "scurt");

        if (ev.button == BUTTON_MENU) {
            if (hold) go_to_sleep();  // nu se întoarce
            // Se verifică din nou ce s-ar fi putut schimba: bateria și un card pus după pornire.
            read_battery();
            if (!s_sd_ok) mount_sd();
            snprintf(s_button, sizeof s_button, "MENIU");
            draw_all();
            continue;
        }

        int step = hold ? 10 : 1;
        s_info.counter += ev.button == BUTTON_NEXT ? step : -step;
        snprintf(s_button, sizeof s_button, "%s%s", button_name(ev.button), hold ? " lung" : "");
        screen_bringup_draw_counter(&s_gfx, &s_info);
        show_partial();
    }
}
