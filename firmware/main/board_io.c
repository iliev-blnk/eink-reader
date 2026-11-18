#include "board_io.h"

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epd_uc8253.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "placa";

// TPS22917 cu CT de 1 nF urcă în ~3,6 ms; cardul mai cere 1 ms după ce tensiunea
// e stabilă, înaintea primelor impulsuri de ceas.
#define RAIL_RISE_MS 10
// Descărcarea ieșirii prin QOD, ca pinii să nu fie trași pe 0 cât șina mai e sus.
#define RAIL_FALL_MS 5

#define HAS_RAILS (BOARD_SD_PWR_EN >= 0 || BOARD_EPD_PWR_EN >= 0)

// Liniile conduse de ESP spre card și panou. Cu șina oprită, oricare rămasă în 1
// (sau cu pull-up) ar alimenta piesa prin diodele de protecție ale pinului.
static const gpio_num_t s_lines[] = {
    BOARD_SD_SCK,  BOARD_SD_MOSI, BOARD_SD_MISO, BOARD_SD_CS,  BOARD_EPD_SCK,
    BOARD_EPD_MOSI, BOARD_EPD_CS,  BOARD_EPD_DC,  BOARD_EPD_RST,
};

// Pinul ca ieșire GPIO simplă (scos din matricea SPI dacă era acolo), fără
// pull-uri, pe nivelul dat. Fără gpio_config, care l-ar trece ca ocupat.
static void drive(gpio_num_t pin, int level)
{
    // Întâi configurat, apoi eliberat: un pin ținut în somn ia la gpio_hold_dis
    // configurația de acum, deci nu trece prin altă stare (comentariul lui gpio_hold_dis din gpio.h).
    gpio_set_level(pin, level);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
    gpio_hold_dis(pin);
    gpio_set_level(pin, level);
}

static void set_rails(int on)
{
    // R10/R11 de 1 MΩ spre masă sunt pe placă: fără pull-uri interne.
    if (BOARD_SD_PWR_EN >= 0) drive(BOARD_SD_PWR_EN, on);
    if (BOARD_EPD_PWR_EN >= 0) drive(BOARD_EPD_PWR_EN, on);
}

void board_light_sleep_pins(void)
{
    // Restul pinilor rămân deconectați în somn, cum îi lasă ESP-IDF: mai puțin
    // curent, iar o intrare în aer nu mai poate reseta cipul la o descărcare ESD.
    for (size_t i = 0; i < sizeof s_lines / sizeof s_lines[0]; i++) gpio_sleep_sel_dis(s_lines[i]);
    if (BOARD_SD_PWR_EN >= 0) gpio_sleep_sel_dis(BOARD_SD_PWR_EN);
    if (BOARD_EPD_PWR_EN >= 0) gpio_sleep_sel_dis(BOARD_EPD_PWR_EN);
}

void board_power_on(void)
{
    if (!HAS_RAILS) return;
    set_rails(1);
    vTaskDelay(pdMS_TO_TICKS(RAIL_RISE_MS));
    // Pinii au stat pe 0 în somn; de acum îi configurează driverele.
    for (size_t i = 0; i < sizeof s_lines / sizeof s_lines[0]; i++) gpio_hold_dis(s_lines[i]);
    gpio_hold_dis(BOARD_EPD_BUSY);
    ESP_LOGI(TAG, "șinele cardului și panoului pornite");
}

esp_err_t board_spi_init(void)
{
    // CS-ul cardului în 1 înaintea oricărui trafic pe magistrală, altfel cardul
    // ar putea lua o parte din el drept comenzi. Cu gpio_set_direction, nu cu
    // gpio_config: gpio_config ar trece pinul ca ocupat, iar driverul SD, care îl
    // ia apoi în primire, ar da „conflict found for GPIO”.
    gpio_set_level(BOARD_SD_CS, 1);
    gpio_set_direction(BOARD_SD_CS, GPIO_MODE_OUTPUT);

    const bool shared = BOARD_EPD_SPI_HOST == BOARD_SD_SPI_HOST;
    const spi_bus_config_t sd = {
        .sclk_io_num = BOARD_SD_SCK,
        .mosi_io_num = BOARD_SD_MOSI,
        .miso_io_num = BOARD_SD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // Pe magistrala comună trece și imaginea panoului dintr-o bucată; altfel
        // ajunge implicitul (4 KB), destul pentru blocurile cardului.
        .max_transfer_sz = shared ? EPD_FB_SIZE : 0,
    };
    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &sd, SPI_DMA_CH_AUTO);
    if (err != ESP_OK || shared) return err;

    const spi_bus_config_t epd = {
        .sclk_io_num = BOARD_EPD_SCK,
        .mosi_io_num = BOARD_EPD_MOSI,
        .miso_io_num = -1,  // panoul doar primește
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_FB_SIZE,
    };
    return spi_bus_initialize(BOARD_EPD_SPI_HOST, &epd, SPI_DMA_CH_AUTO);
}

void board_power_off(void)
{
    if (!HAS_RAILS) return;
    // Pinii trec din matricea SPI înapoi la GPIO, ieșiri pe 0. RST e ținut în 1
    // de epd_sleep(); drive() îl eliberează întâi.
    for (size_t i = 0; i < sizeof s_lines / sizeof s_lines[0]; i++) drive(s_lines[i], 0);
    // BUSY e ieșirea panoului: intrare oprită, fără pull-up-ul pus de epd_init.
    gpio_hold_dis(BOARD_EPD_BUSY);
    gpio_intr_disable(BOARD_EPD_BUSY);
    gpio_set_direction(BOARD_EPD_BUSY, GPIO_MODE_DISABLE);
    gpio_set_pull_mode(BOARD_EPD_BUSY, GPIO_FLOATING);

    set_rails(0);
    vTaskDelay(pdMS_TO_TICKS(RAIL_FALL_MS));

    // Pinii rămân așa în somnul adânc. GPIO0–21 sunt pini RTC și se țin singuri;
    // IO47/IO48 (SCK și MOSI ai panoului) sunt digitali și mai cer gpio_deep_sleep_hold_en.
    for (size_t i = 0; i < sizeof s_lines / sizeof s_lines[0]; i++) gpio_hold_en(s_lines[i]);
    gpio_hold_en(BOARD_EPD_BUSY);
    if (BOARD_SD_PWR_EN >= 0) gpio_hold_en(BOARD_SD_PWR_EN);
    if (BOARD_EPD_PWR_EN >= 0) gpio_hold_en(BOARD_EPD_PWR_EN);
    gpio_deep_sleep_hold_en();
}
