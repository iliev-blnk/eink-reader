#include "usbdisk.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "usbdisk";

// Volumul pe care TinyUSB îl montează singur pe dispozitiv după ce
// calculatorul scoate discul sau se deconectează (vezi usbdisk_state). Nu-l
// folosește nimeni; după calea lui se vede doar cine are cardul.
#define RELEASED_PATH "/usbdisk"

static bool s_on;
static volatile bool s_attached, s_suspended;
static tinyusb_msc_storage_handle_t s_storage;
// Cât e discul pornit, cipul nu intră în somnul ușor: perifericul USB s-ar
// opri, iar calculatorul ar pierde discul. (CONFIG_TINYUSB_PM ar elibera
// blocarea cât e magistrala suspendată; e un pas următor, de încercat pe placă.)
static esp_pm_lock_handle_t s_no_sleep;

// Din taskul TinyUSB.
static void on_usb(tinyusb_event_t *e, void *arg)
{
    (void)arg;
    switch (e->id) {
    case TINYUSB_EVENT_ATTACHED: s_attached = true, s_suspended = false; break;
    case TINYUSB_EVENT_DETACHED: s_attached = false; break;
    case TINYUSB_EVENT_SUSPENDED: s_suspended = true; break;
    case TINYUSB_EVENT_RESUMED: s_suspended = false; break;
    default: break;
    }
}

static void on_msc(tinyusb_msc_storage_handle_t h, tinyusb_msc_event_t *e, void *arg)
{
    (void)h, (void)arg;
    if (e->id == TINYUSB_MSC_EVENT_MOUNT_FAILED || e->id == TINYUSB_MSC_EVENT_FORMAT_REQUIRED)
        ESP_LOGW(TAG, "volumul nu se montează pe dispozitiv (evenimentul %d)", e->id);
}

esp_err_t usbdisk_start(const sdcard_config_t *cfg, int vbus_gpio)
{
    if (s_on) return ESP_ERR_INVALID_STATE;
    // Fără gestiunea energiei (CONFIG_PM_ENABLE oprit) blocarea nu există și nici nu trebuie.
    if (!s_no_sleep) esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usbdisk", &s_no_sleep);
    if (s_no_sleep) esp_pm_lock_acquire(s_no_sleep);
    sdmmc_card_t *card;
    ESP_RETURN_ON_ERROR(sdcard_open_raw(cfg, &card), TAG, "cardul nu pornește");
    s_attached = s_suspended = false;

    // Cu montarea automată pornită (auto_mount_off = 0), TinyUSB dă cardul
    // calculatorului când acesta se conectează și îl ia înapoi când îl scoate
    // în siguranță (SCSI START STOP UNIT, eject) sau se deconectează. Așa se
    // vede, fără alte apeluri inverse, că discul a fost eliberat.
    const tinyusb_msc_driver_config_t msc = {.callback = on_msc};
    esp_err_t err = tinyusb_msc_install_driver(&msc);
    if (err != ESP_OK) goto fail_card;
    const tinyusb_msc_storage_config_t storage = {
        .medium.card = card,
        .fat_fs = {.base_path = (char *)RELEASED_PATH, .config.max_files = 1, .do_not_format = true},
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    err = tinyusb_msc_new_storage_sdmmc(&storage, &s_storage);
    if (err != ESP_OK) goto fail_msc;

    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG(on_usb, NULL);
    if (vbus_gpio >= 0) {
        usb.phy.self_powered = true;
        usb.phy.vbus_monitor_io = vbus_gpio;
    }
    err = tinyusb_driver_install(&usb);
    if (err != ESP_OK) goto fail_storage;
    s_on = true;
    ESP_LOGI(TAG, "discul USB e pornit");
    return ESP_OK;

fail_storage:
    tinyusb_msc_delete_storage(s_storage);
fail_msc:
    tinyusb_msc_uninstall_driver();
fail_card:
    sdcard_close_raw();
    if (s_no_sleep) esp_pm_lock_release(s_no_sleep);
    ESP_LOGE(TAG, "discul USB nu pornește: %s", esp_err_to_name(err));
    return err;
}

usbdisk_state_t usbdisk_state(void)
{
    if (!s_on) return USBDISK_OFF;
    tinyusb_msc_mount_point_t mp = TINYUSB_MSC_STORAGE_MOUNT_USB;
    tinyusb_msc_get_storage_mount_point(s_storage, &mp);
    if (mp == TINYUSB_MSC_STORAGE_MOUNT_APP) return USBDISK_RELEASED;
    if (!s_attached) return USBDISK_WAITING;
    if (s_suspended) return USBDISK_PAUSED;
    return USBDISK_HOST;
}

esp_err_t usbdisk_stop(void)
{
    if (!s_on) return ESP_OK;
    // Întâi USB-ul: după asta nu mai vine nicio cerere de la calculator.
    esp_err_t err = tinyusb_driver_uninstall();
    if (err != ESP_OK) ESP_LOGE(TAG, "TinyUSB nu se oprește: %s", esp_err_to_name(err));
    esp_err_t e2 = tinyusb_msc_delete_storage(s_storage);
    if (e2 == ESP_OK) e2 = tinyusb_msc_uninstall_driver();
    if (e2 != ESP_OK) ESP_LOGE(TAG, "discul nu se închide: %s", esp_err_to_name(e2));
    sdcard_close_raw();
    s_on = false;
    s_storage = NULL;
    if (s_no_sleep) esp_pm_lock_release(s_no_sleep);
    ESP_LOGI(TAG, "discul USB e oprit");
    return err != ESP_OK ? err : e2;
}
