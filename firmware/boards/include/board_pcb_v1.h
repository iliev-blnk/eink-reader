// Placa proprie v1 (hardware/pcb): ESP32-S3-WROOM-1-N16R8, același panou WeAct
// 3,7" pe bareta J3, soclu microSD, încărcător BQ24074, indicator MAX17048.
// Rețelele și pinii vin din hardware/pcb/circuit.py; hardware/pcb/verifica.py
// compară antetul acesta cu placa la fiecare make.sh.
//
// Față de DevKit: cardul și panoul au fiecare magistrala lui, iar alimentarea
// fiecăruia trece printr-un comutator (TPS22917) pe care îl conduce firmware-ul.
// În somnul adânc ambele sunt oprite (board_io.c).
#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"

#define BOARD_NAME "pcb-v1"

// Cardul: aceiași pini ca pe DevKit, pe SPI2.
#define BOARD_SD_SPI_HOST SPI2_HOST
#define BOARD_SD_SCK GPIO_NUM_12
#define BOARD_SD_MOSI GPIO_NUM_11
#define BOARD_SD_MISO GPIO_NUM_13
#define BOARD_SD_CS GPIO_NUM_14

// Panoul: SCK și MOSI proprii, pe SPI3 (REVIEW.md, punctul 1). IO47/IO48 sunt
// pini de 3,3 V pe N16R8. Restul pinilor, ca pe DevKit.
#define BOARD_EPD_SPI_HOST SPI3_HOST
#define BOARD_EPD_SCK GPIO_NUM_47
#define BOARD_EPD_MOSI GPIO_NUM_48
#define BOARD_EPD_CS GPIO_NUM_10
#define BOARD_EPD_DC GPIO_NUM_9
#define BOARD_EPD_RST GPIO_NUM_8
#define BOARD_EPD_BUSY GPIO_NUM_18
// Ca pe DevKit până la măsurare; pe placă traseele sunt scurte, deci se poate urca.
#define BOARD_EPD_SPI_HZ (4 * 1000 * 1000)

// Comutatoarele de alimentare (U4 pentru card, U5 pentru panou): 1 = pornit.
// Au 1 MΩ spre masă pe ON, deci sunt oprite cât pinul nu e condus.
#define BOARD_SD_PWR_EN GPIO_NUM_15
#define BOARD_EPD_PWR_EN GPIO_NUM_21

// Butoanele SW2–SW4 la masă, pull-up-uri interne; pini RTC, trezesc din somn.
#define BOARD_BTN_PREV GPIO_NUM_4
#define BOARD_BTN_NEXT GPIO_NUM_5
#define BOARD_BTN_MENU GPIO_NUM_6

// VBUS prin divizorul R17/R18 (470k/470k, cu C17 de 100 nF): 2,5 V cât e cablul pus;
// constanta de timp e ~24 ms, destul pentru detectarea cablului.
#define BOARD_VBUS_SENSE_GPIO GPIO_NUM_2

// Tensiunea bateriei prin R15/R16 (1M/1M), cu 100 nF pe ADC.
#define BOARD_BAT_ADC_GPIO GPIO_NUM_1
#define BOARD_BAT_DIVIDER 2.0f

// Indicatorul MAX17048 (U6), cu pull-up-uri de 4,7 kΩ pe placă. ALRT nu e
// folosit încă (pull-up de 100 kΩ, pin RTC: ar putea trezi cipul la baterie slabă).
#define BOARD_I2C_SDA GPIO_NUM_16
#define BOARD_I2C_SCL GPIO_NUM_17
#define BOARD_FG_ALRT GPIO_NUM_7

// TPS63900 dă ~400 mA, iar ESP32-S3 cere vârfuri de ~350 mA la emisie WiFi
// (REVIEW.md, punctul 4). Firmware-ul nu are încă WiFi; când îl va avea, puterea
// de emisie se limitează la atât (unități de 0,25 dBm, esp_wifi_set_max_tx_power).
#define BOARD_WIFI_MAX_TX_QDBM 52  // 13 dBm
