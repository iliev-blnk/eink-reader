// Înregistratorul de curent pentru E3/E4 (MEAS-2): un INA226 pe I2C, citit la
// fiecare conversie, trimis pe consolă ca CSV.
//
// Rândurile de date sunt brute, așa cum le dă INA226: `t_us,shunt,bus`, cu
// t_us ceasul înregistratorului la sfârșitul conversiei, shunt registrul 0x01
// (cu semn, 2,5 µV pe bit) și bus registrul 0x02 (1,25 mV pe bit). Conversia în
// mA și mV o face tools/ina226/ina226.py, cu valorile din rândurile `#`.
// Rândurile care încep cu `#` sunt antetul (repetat la 2 s, ca o captură pornită
// oricând să-l aibă), pierderile și erorile.
//
// Două taskuri: cel de citire, cu prioritate mare, pune eșantioanele într-o
// coadă; cel de scriere le formatează și le trimite pe consolă. Scrierea pe
// UART blochează, iar citirea nu trebuie să aștepte după ea.
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

// Foaia INA226, 7.6: registrele.
#define REG_CONFIG 0x00
#define REG_SHUNT 0x01
#define REG_BUS 0x02
#define REG_MASK 0x06
#define REG_MANUF 0xFE
#define REG_DIE 0xFF

#define CONFIG_RST 0x8000
#define MODE_CONT_BOTH 0x7          // șunt și tensiune, continuu
#define MASK_CNVR 0x0400            // ALERT la fiecare conversie gata
#define MASK_CVRF 0x0008            // conversia gata; citirea registrului îl șterge

#define I2C_HZ 400000
#define TIMEOUT_MS 20
#define QUEUE_LEN 4096              // ~5 s la 850/s
#define HEADER_US 2000000

typedef struct {
    int64_t t_us;
    int16_t shunt;
    uint16_t bus;
} sample_t;

static i2c_master_dev_handle_t s_dev;
static QueueHandle_t s_queue;
static TaskHandle_t s_reader;
static volatile int64_t s_alert_us;
static atomic_uint s_lost, s_i2c_errors;

static esp_err_t rd(uint8_t reg, uint16_t *out)
{
    uint8_t b[2];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, TIMEOUT_MS);
    if (err == ESP_OK) *out = (uint16_t)(b[0] << 8 | b[1]);
    return err;
}

static esp_err_t wr(uint8_t reg, uint16_t v)
{
    const uint8_t b[3] = {reg, v >> 8, v & 0xFF};
    return i2c_master_transmit(s_dev, b, 3, TIMEOUT_MS);
}

static void IRAM_ATTR on_alert(void *arg)
{
    (void)arg;
    s_alert_us = esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_reader, &woken);
    portYIELD_FROM_ISR(woken);
}

static void reader(void *arg)
{
    (void)arg;
    for (;;) {
        uint16_t mask = 0;
        int64_t t;
        if (CONFIG_LOGGER_ALERT >= 0) {
            // ALERT coboară la conversia gata și urcă la citirea MASK. Dacă un front
            // s-a pierdut, pinul rămâne jos: după 50 ms fără front se citește oricum.
            bool edge = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) > 0;
            if (!edge && gpio_get_level(CONFIG_LOGGER_ALERT)) continue;
            t = edge ? s_alert_us : esp_timer_get_time();
            if (rd(REG_MASK, &mask) != ESP_OK) goto i2c_error;
        } else {
            // Fără ALERT: MASK în buclă până apare CVRF; ceasul e cel al citirii.
            if (rd(REG_MASK, &mask) != ESP_OK) goto i2c_error;
            if (!(mask & MASK_CVRF)) continue;
            t = esp_timer_get_time();
        }
        sample_t s = {.t_us = t};
        uint16_t sh;
        if (rd(REG_SHUNT, &sh) != ESP_OK || rd(REG_BUS, &s.bus) != ESP_OK) goto i2c_error;
        s.shunt = (int16_t)sh;
        if (xQueueSend(s_queue, &s, 0) != pdTRUE) atomic_fetch_add(&s_lost, 1);
        continue;
    i2c_error:
        atomic_fetch_add(&s_i2c_errors, 1);
        vTaskDelay(1);
    }
}

static void header(void)
{
    printf("# ina226,v=1,target=%s,addr=0x%02x,shunt_mohm=%d,ct=%d,avg=%d,alert=%d,"
           "shunt_lsb_nv=2500,bus_lsb_uv=1250,coloane=t_us;shunt;bus\n",
           CONFIG_IDF_TARGET, CONFIG_LOGGER_ADDR, CONFIG_LOGGER_SHUNT_MOHM, CONFIG_LOGGER_CT, CONFIG_LOGGER_AVG,
           CONFIG_LOGGER_ALERT);
}

static void writer(void *arg)
{
    (void)arg;
    static char buf[4096];
    size_t n = 0;
    int64_t last_header = -HEADER_US;
    unsigned lost_seen = 0, err_seen = 0;
    for (;;) {
        sample_t s;
        bool got = xQueueReceive(s_queue, &s, pdMS_TO_TICKS(20)) == pdTRUE;
        int64_t now = esp_timer_get_time();
        unsigned lost = atomic_load(&s_lost), errs = atomic_load(&s_i2c_errors);
        bool meta = now - last_header >= HEADER_US || lost != lost_seen || errs != err_seen;
        if (got && n + 40 < sizeof buf) {
            n += snprintf(buf + n, sizeof buf - n, "%" PRId64 ",%d,%u\n", s.t_us, s.shunt, s.bus);
        }
        // Se scrie când bufferul e aproape plin, când coada s-a golit sau înaintea unui rând `#`.
        if (n > 0 && (!got || n + 40 >= sizeof buf || meta)) {
            fwrite(buf, 1, n, stdout);
            n = 0;
        }
        if (meta) {
            if (now - last_header >= HEADER_US) {
                header();
                last_header = now;
            }
            if (lost != lost_seen) printf("# pierdute,%u\n", lost);
            if (errs != err_seen) printf("# erori_i2c,%u\n", errs);
            lost_seen = lost;
            err_seen = errs;
        }
        if (!got || meta) fflush(stdout);
    }
}

static bool init_ina(void)
{
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t bc = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_LOGGER_SDA,
        .scl_io_num = CONFIG_LOGGER_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // modulele au de obicei 10 kΩ; astea doar ajută
    };
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK) return false;
    if (i2c_master_probe(bus, CONFIG_LOGGER_ADDR, TIMEOUT_MS) != ESP_OK) {
        printf("# eroare,INA226 nu răspunde la 0x%02x (SDA=%d SCL=%d)\n", CONFIG_LOGGER_ADDR, CONFIG_LOGGER_SDA,
               CONFIG_LOGGER_SCL);
        return false;
    }
    const i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_LOGGER_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) return false;

    uint16_t manuf = 0, die = 0;
    rd(REG_MANUF, &manuf);
    rd(REG_DIE, &die);
    if (manuf != 0x5449 || (die >> 4) != 0x226) {
        printf("# eroare,la 0x%02x nu e un INA226 (fabricant 0x%04x, cip 0x%04x)\n", CONFIG_LOGGER_ADDR, manuf, die);
        return false;
    }
    wr(REG_CONFIG, CONFIG_RST);
    esp_rom_delay_us(1000);
    // Bitul 14 e rezervat și rămâne 1 (valoarea de după reset, 0x4127).
    uint16_t cfg = 0x4000 | CONFIG_LOGGER_AVG << 9 | CONFIG_LOGGER_CT << 6 | CONFIG_LOGGER_CT << 3 | MODE_CONT_BOTH;
    if (wr(REG_CONFIG, cfg) != ESP_OK || wr(REG_MASK, MASK_CNVR) != ESP_OK) return false;
    return true;
}

void app_main(void)
{
    if (!init_ina()) {
        // Se repetă mesajul, ca să-l vadă și o captură pornită mai târziu.
        for (;;) {
            printf("# eroare,INA226 neinițializat; verifică firele și repornește\n");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(sample_t));
    xTaskCreate(writer, "scriere", 4096, NULL, 5, NULL);
    xTaskCreate(reader, "citire", 4096, NULL, 10, &s_reader);
    if (CONFIG_LOGGER_ALERT >= 0) {
        const gpio_config_t io = {
            .pin_bit_mask = BIT64(CONFIG_LOGGER_ALERT),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,  // ALERT e cu drenă deschisă
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&io);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(CONFIG_LOGGER_ALERT, on_alert, NULL);
    }
}
