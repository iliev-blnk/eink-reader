// Prototype: ESP32-S3 DevKitC-1 (N16R8) with the WeAct 3.7" e-paper module,
// a microSD module and three buttons. Matches hardware/eink-reader.kicad_sch v1.0.
//
// Avoided pins: 0/3/45/46 (strapping), 19/20 (native USB), 26-32 (flash),
// 33-37 (octal PSRAM on R8 modules), 43/44 (UART0 console).
#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"

#define BOARD_NAME "devkit-proto"

// Shared SPI bus: e-paper (write-only) and microSD.
#define BOARD_SPI_HOST SPI2_HOST
#define BOARD_SPI_SCK GPIO_NUM_12
#define BOARD_SPI_MOSI GPIO_NUM_11
#define BOARD_SPI_MISO GPIO_NUM_13

// WeAct 3.7" e-paper, UC8253 controller, 240x416.
#define BOARD_EPD_CS GPIO_NUM_10
#define BOARD_EPD_DC GPIO_NUM_9
#define BOARD_EPD_RST GPIO_NUM_8
#define BOARD_EPD_BUSY GPIO_NUM_18
#define BOARD_EPD_SPI_HZ (4 * 1000 * 1000)  // conservative for jumper wires

// microSD module.
#define BOARD_SD_CS GPIO_NUM_14

// Buttons to GND, internal pull-ups. All three are RTC pins, so any of them
// can wake the chip from deep sleep (ext1).
#define BOARD_BTN_PREV GPIO_NUM_4
#define BOARD_BTN_NEXT GPIO_NUM_5
#define BOARD_BTN_MENU GPIO_NUM_6

// Battery voltage through a 100k/100k divider.
#define BOARD_BAT_ADC_GPIO GPIO_NUM_1
#define BOARD_BAT_DIVIDER 2.0f
