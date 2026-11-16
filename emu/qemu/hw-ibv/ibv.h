/*
 * Emulatorul cititorului IBV: declarații comune.
 *
 * Perifericele din ESP32-S3 care lipsesc din QEMU (pinii, ADC-ul, SPI2,
 * somnul adânc), panoul e-paper UC8253 și placa pe care stau toate.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_IBV_H
#define HW_IBV_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"

/* ---- jurnalul rulării ---- */

/* Unde ajung cadrele și jurnalul; NULL = doar pe stderr. */
void ibv_set_out_dir(const char *dir);
const char *ibv_out_dir(void);
/* O linie în jurnal.txt, cu timpul virtual în față. */
void ibv_journal(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
/* O abatere de la ce ar accepta placa adevărată: în jurnal și pe stderr. */
void ibv_warn(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
int ibv_warn_count(void);

/* ---- pinii: GPIO + IO_MUX + RTC IO ---- */

#define TYPE_IBV_S3_PINS "ibv.s3.pins"
#define IBV_PIN_COUNT 49
#define IBV_RTC_PIN_COUNT 22
#define IBV_PINS_PAD "pad"       /* ieșiri: nivelul electric al fiecărui pin */
#define IBV_PINS_EXT "ext-in"    /* intrări: un dispozitiv din afară trage pinul în 0 sau 1 */
#define IBV_PINS_SIG_CS "sig-cs" /* intrări: liniile CS0..5 ale SPI2, duse la pini prin matrice */

/* Ce face lumea din afară cu pinul: -1 îl lasă în aer, 0 sau 1 îl trage. */
void ibv_pins_set_ext(DeviceState *dev, int pin, int level);
/* Nivelul la care ajunge un pin lăsat în aer de toată lumea (rezistențele de pe module). */
void ibv_pins_set_float_level(DeviceState *dev, int pin, int level);
int ibv_pins_level(DeviceState *dev, int pin);
/* Pe unde iese pinul: 0x1000 | funcția IO_MUX, altfel semnalul din matricea GPIO. */
uint32_t ibv_pins_route(DeviceState *dev, int pin);
/* RTC_CNTL_PAD_HOLD: pinii RTC înghețați în starea de acum. */
void ibv_pins_set_hold(DeviceState *dev, uint32_t mask);
/* Domeniul digital se oprește (somn adânc): registrele GPIO și IO_MUX revin la reset. */
void ibv_pins_digital_off(DeviceState *dev);
/* Perifericele RTC se opresc în somn: registrele RTC IO revin la reset, pinii ținuți rămân. */
void ibv_pins_rtc_off(DeviceState *dev);
/* Chemat la orice schimbare de nivel pe vreun pin. */
void ibv_pins_set_listener(DeviceState *dev, void (*fn)(void *opaque), void *opaque);

/* ---- ADC-ul din SENS ---- */

#define TYPE_IBV_S3_SENS "ibv.s3.sens"
/* Tensiunea de pe pinul canalului ADC1 dat, în mV. */
void ibv_sens_set_mv(DeviceState *dev, int channel, int mv);

/* ---- SPI2 (GPSPI) ---- */

#define TYPE_IBV_S3_GPSPI "ibv.s3.gpspi"
#define IBV_GPSPI_CS_COUNT 6
/* Dacă cineva ține MISO; altfel linia se citește 1. */
void ibv_gpspi_set_miso_driven(DeviceState *dev, bool driven);
/* De câte ori a pornit o tranzacție; pentru detectarea liniștii. */
uint64_t ibv_gpspi_xfer_count(DeviceState *dev);
void ibv_gpspi_set_start_hook(DeviceState *dev, void (*fn)(void *opaque), void *opaque);

/* ---- panoul UC8253 ---- */

#define TYPE_IBV_UC8253 "ibv.uc8253"
#define IBV_UC8253_DC "dc"
#define IBV_UC8253_RST "rst"
#define IBV_UC8253_BUSY "busy"
#define IBV_EPD_WIDTH 240
#define IBV_EPD_HEIGHT 416
bool ibv_uc8253_is_busy(DeviceState *dev);
int ibv_uc8253_frame_count(DeviceState *dev);

/* ---- somnul adânc, peste RTC_CNTL ---- */

#define TYPE_IBV_S3_SLEEP "ibv.s3.sleep"
bool ibv_sleep_is_asleep(DeviceState *dev);
/* De chemat când se schimbă un pin, ca trezirea EXT1 să fie reevaluată. */
void ibv_sleep_pins_changed(DeviceState *dev);

#endif
