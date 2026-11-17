// Cardul microSD ca disc USB (FW-12, clasa Mass Storage prin TinyUSB): pe
// portul USB nativ (GPIO19/20) calculatorul vede cardul ca pe un stick și
// copiază cărțile direct, fără să scoată cardul din cititor.
//
// Cât timp discul e pornit, cardul îl are calculatorul: sistemul de fișiere al
// dispozitivului e demontat, iar dispozitivul nu citește și nu scrie nimic pe
// card (două sisteme de fișiere pe același volum îl strică). Ce face
// calculatorul, sector cu sector, se vede pe dispozitiv abia după
// usbdisk_stop() și o montare nouă.
//
// Firmware-ul nu formatează niciodată cardul, nici aici.
#pragma once

#include "esp_err.h"
#include "sdcard.h"

typedef enum {
    USBDISK_OFF,        // oprit
    USBDISK_WAITING,    // pornit, niciun calculator încă
    USBDISK_HOST,       // calculatorul a montat discul: nu se scoate cablul
    USBDISK_PAUSED,     // magistrala e în repaus: cablul scos fără senzor VBUS, sau calculatorul doarme
    USBDISK_RELEASED,   // calculatorul a scos discul în siguranță, sau cablul a fost scos
} usbdisk_state_t;

// Cardul trebuie să fie demontat (sdcard_unmount). `vbus_gpio` e pinul care
// vede tensiunea de pe USB, sau -1 dacă placa nu are (prototipul): atunci
// scoaterea cablului apare doar ca USBDISK_PAUSED.
esp_err_t usbdisk_start(const sdcard_config_t *cfg, int vbus_gpio);
usbdisk_state_t usbdisk_state(void);
// Oprește USB-ul (calculatorul vede discul dispărând) și închide cardul.
// Apoi cardul se montează din nou cu sdcard_mount().
esp_err_t usbdisk_stop(void);
