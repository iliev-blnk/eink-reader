#include "sdcard.h"

#include <string.h>

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;
static char s_mount_point[16];

esp_err_t sdcard_mount(const sdcard_config_t *cfg, sdcard_info_t *info)
{
    if (s_card) return ESP_ERR_INVALID_STATE;

    static const int speeds_khz[] = {SDMMC_FREQ_DEFAULT, 5000};
    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,  // never wipe the user's card
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
        ESP_LOGW(TAG, "mount at %d kHz failed: %s", speeds_khz[i], esp_err_to_name(err));
        s_card = NULL;
        if (err == ESP_FAIL) break;  // the card answered but has no FAT volume
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
