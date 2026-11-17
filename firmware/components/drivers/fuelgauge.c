#include "fuelgauge.h"

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "fuelgauge";

// Foaia MAX17048, tabelul registrelor. Adresa 0x6C/0x6D pe 8 biți = 0x36.
#define ADDR 0x36
#define REG_VCELL 0x02    // 78,125 µV pe bit
#define REG_SOC 0x04      // 1/256 %
#define REG_VERSION 0x08  // 0x001_
#define TIMEOUT_MS 50

// Firmware-ul doar citește. Nu scrie MODE, deci EnSleep rămâne 0: în somn
// indicatorul nu se oprește, ci trece singur în hibernare (~3 µA) și continuă
// să numere. Dacă s-ar opri, n-ar mai ști cât s-a consumat între treziri.

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t read_reg(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2, TIMEOUT_MS), TAG, "registrul 0x%02x",
                        reg);
    *out = (uint16_t)(buf[0] << 8 | buf[1]);  // MSB întâi
    return ESP_OK;
}

esp_err_t fuelgauge_init(gpio_num_t sda, gpio_num_t scl)
{
    if (!s_bus) {
        const i2c_master_bus_config_t bus = {
            .i2c_port = -1,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = false,  // R12/R13 de 4,7 kΩ pe placă
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_bus), TAG, "magistrala I2C");
    }
    if (i2c_master_probe(s_bus, ADDR, TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "MAX17048 nu răspunde la 0x%02x", ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_dev) {
        const i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ADDR,
            .scl_speed_hz = 100 * 1000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_dev), TAG, "dispozitivul I2C");
    }
    uint16_t version;
    ESP_RETURN_ON_ERROR(read_reg(REG_VERSION, &version), TAG, "VERSION");
    if ((version & 0xFFF0) != 0x0010) {
        ESP_LOGW(TAG, "VERSION = 0x%04x, nu pare un MAX17048", version);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "MAX17048, versiunea 0x%04x", version);
    return ESP_OK;
}

int fuelgauge_read_mv(void)
{
    uint16_t v;
    if (!s_dev || read_reg(REG_VCELL, &v) != ESP_OK) return -1;
    return (int)((uint32_t)v * 78125 / 1000000);  // 78,125 µV pe bit
}

int fuelgauge_read_soc_permille(void)
{
    uint16_t v;
    if (!s_dev || read_reg(REG_SOC, &v) != ESP_OK) return -1;
    int p = (int)((uint32_t)v * 10 / 256);  // octetul de sus e procentul întreg
    return p > 1000 ? 1000 : p;              // poate depăși 100 % puțin, după încărcare
}
