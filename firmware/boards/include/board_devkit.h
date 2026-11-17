// Prototipul: ESP32-S3 DevKitC-1 (N16R8) cu modulul e-paper WeAct 3,7", un modul
// microSD și trei butoane. Se potrivește cu hardware/eink-reader.kicad_sch v1.0.
//
// Pini evitați: 0/3/45/46 (strapping), 19/20 (USB nativ), 26-32 (flash),
// 33-37 (PSRAM octal pe modulele R8), 43/44 (consola UART0).
#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"

#define BOARD_NAME "devkit-proto"

// O singură magistrală SPI, comună: microSD și e-paper (doar scriere).
#define BOARD_SD_SPI_HOST SPI2_HOST
#define BOARD_SD_SCK GPIO_NUM_12
#define BOARD_SD_MOSI GPIO_NUM_11
#define BOARD_SD_MISO GPIO_NUM_13
#define BOARD_SD_CS GPIO_NUM_14

// E-paper WeAct 3,7", controler UC8253, 240x416, pe aceeași magistrală.
#define BOARD_EPD_SPI_HOST BOARD_SD_SPI_HOST
#define BOARD_EPD_SCK BOARD_SD_SCK
#define BOARD_EPD_MOSI BOARD_SD_MOSI
#define BOARD_EPD_CS GPIO_NUM_10
#define BOARD_EPD_DC GPIO_NUM_9
#define BOARD_EPD_RST GPIO_NUM_8
#define BOARD_EPD_BUSY GPIO_NUM_18
#define BOARD_EPD_SPI_HZ (4 * 1000 * 1000)  // prudent pentru firele de pe breadboard

// Cardul și panoul sunt legate direct la 3V3: fără comutatoare de alimentare.
#define BOARD_SD_PWR_EN (-1)
#define BOARD_EPD_PWR_EN (-1)

// Butoane la GND, pull-up-uri interne. Toate trei sunt pini RTC, deci oricare
// poate trezi cipul din somnul adânc (ext1).
#define BOARD_BTN_PREV GPIO_NUM_4
#define BOARD_BTN_NEXT GPIO_NUM_5
#define BOARD_BTN_MENU GPIO_NUM_6

// Tensiunea de pe USB (VBUS), pentru discul USB: placa de dezvoltare nu are
// divizorul, deci scoaterea cablului se vede doar ca magistrală în repaus.
#define BOARD_VBUS_SENSE_GPIO (-1)

// Tensiunea bateriei printr-un divizor 100k/100k.
#define BOARD_BAT_ADC_GPIO GPIO_NUM_1
#define BOARD_BAT_DIVIDER 2.0f

// Fără indicator de baterie (MAX17048): tensiunea vine doar din ADC.
#define BOARD_I2C_SDA (-1)
#define BOARD_I2C_SCL (-1)
#define BOARD_FG_ALRT (-1)
