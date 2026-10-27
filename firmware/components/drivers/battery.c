#include "battery.h"

#include <stddef.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define SAMPLES 16
#define ATTEN ADC_ATTEN_DB_12  // cap de scală ~3,1 V calibrat, destul pentru 4,2 V / 2

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static float s_divider;

esp_err_t battery_init(gpio_num_t pin, float divider)
{
    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(pin, &unit, &s_chan), TAG, "GPIO%d nu are ADC", pin);
    const adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "unitatea ADC");
    const adc_oneshot_chan_cfg_t chan_cfg = {.atten = ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_chan, &chan_cfg), TAG, "canalul ADC");

    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_chan,
        .atten = ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC-ul nu are calibrare în eFuse, citirile sunt aproximative");
        s_cali = NULL;
    }
    s_divider = divider;
    return ESP_OK;
}

int battery_read_mv(void)
{
    if (!s_adc) return -1;
    long sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int v;
        esp_err_t err = s_cali ? adc_oneshot_get_calibrated_result(s_adc, s_cali, s_chan, &v)
                               : adc_oneshot_read(s_adc, s_chan, &v);
        if (err != ESP_OK) return -1;
        if (!s_cali) v = v * 3100 / 4095;  // conversie aproximativă din valoarea brută în mV
        sum += v;
    }
    return (int)(sum / SAMPLES * s_divider);
}
