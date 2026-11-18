#include "sdcard.h"

#include <string.h>

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;
static char s_mount_point[16];
static sdmmc_card_t s_raw;      // cardul deschis fără FAT (sdcard_open_raw)
static bool s_raw_open;

static const int speeds_khz[] = {SDMMC_FREQ_DEFAULT, 5000};

esp_err_t sdcard_mount(const sdcard_config_t *cfg, sdcard_info_t *info)
{
    if (s_card || s_raw_open) return ESP_ERR_INVALID_STATE;

    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,  // nu se șterge niciodată cardul utilizatorului
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.host_id = cfg->host;
    dev.gpio_cs = cfg->cs;

    esp_err_t err = ESP_FAIL;
    for (size_t i = 0; i < sizeof speeds_khz / sizeof speeds_khz[0]; i++) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = cfg->host;
        host.max_freq_khz = speeds_khz[i];
        err = esp_vfs_fat_sdspi_mount(cfg->mount_point, &host, &dev, &mount, &s_card);
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "montarea la %d kHz a eșuat: %s", speeds_khz[i], esp_err_to_name(err));
        s_card = NULL;
        if (err == ESP_FAIL) break;  // cardul a răspuns, dar nu are volum FAT
    }
    if (err != ESP_OK) return err;

    strlcpy(s_mount_point, cfg->mount_point, sizeof s_mount_point);
    if (info) {
        info->capacity_bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
        info->freq_khz = s_card->real_freq_khz;
        memcpy(info->name, s_card->cid.name, sizeof info->name);
        info->name[sizeof info->name - 1] = '\0';
    }
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (!s_card) return ESP_OK;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    s_card = NULL;
    return err;
}

esp_err_t sdcard_check(void)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    return sdmmc_get_status(s_card);
}

esp_err_t sdcard_open_raw(const sdcard_config_t *cfg, sdmmc_card_t **card)
{
    if (s_card || s_raw_open) return ESP_ERR_INVALID_STATE;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.host_id = cfg->host;
    dev.gpio_cs = cfg->cs;

    esp_err_t err = ESP_FAIL;
    for (size_t i = 0; i < sizeof speeds_khz / sizeof speeds_khz[0]; i++) {
        sdspi_dev_handle_t handle;
        err = sdspi_host_init_device(&dev, &handle);
        if (err != ESP_OK) return err;
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = handle;
        host.max_freq_khz = speeds_khz[i];
        err = sdmmc_card_init(&host, &s_raw);
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "cardul nu pornește la %d kHz: %s", speeds_khz[i], esp_err_to_name(err));
        // Scoate doar dispozitivul cardului; magistrala rămâne a panoului.
        sdspi_host_remove_device(handle);
    }
    if (err != ESP_OK) return err;
    s_raw_open = true;
    *card = &s_raw;
    return ESP_OK;
}

void sdcard_close_raw(void)
{
    if (!s_raw_open) return;
    sdspi_host_remove_device(s_raw.host.slot);
    s_raw_open = false;
}
