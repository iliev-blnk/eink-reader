# Project brief

Bachelor's diploma project: a custom e-ink e-reader with its own firmware (a "custom OS") and its own book format.

Collected 2026-10-25 from earlier planning chats (Oct 19 parts planning, Oct 25 purchased-parts list).

## Hardware: purchased (as of 2026-10-25)

| Part | Notes |
|---|---|
| ESP32-S3 DevKitC-1, 16 MB flash | Replaced the ESP32-WROOM-32D (48 lei). PSRAM (N16R8 or not) unconfirmed. |
| WeAct Studio 3.7" e-paper module, 416×240 | ~93.92 lei, Elektroweb |
| TPS63020 buck-boost module, 3.3 V out | 49.09 lei |
| TP4056 LiPo charger module | |
| LiPo battery | |
| microSD card module | |
| MediaRange microSDHC card | SDHC (FAT32) instead of the 128 GB card, to avoid exFAT issues |
| USB-C microSD card reader | for loading books from the PC |
| 15-in-1 60 W soldering kit | |
| Existing wires, resistors, misc parts | |

## Hardware: still open

- Multimeter (UT33A, 92.25 lei), unless the soldering kit includes one
- 3–5 tact buttons and an SS12D00 slide switch, unless the kit has them
- Perfboard, female headers, JST PH2.0 pigtail for the LiPo (**check polarity**)
- Case: 3D print or a project box
- Check whether the ESP32-S3 is N16R8 (with PSRAM). It matters less now; see the book format section.

## Display: WeAct 3.7"

- Panel is likely the GDEY037T03 (UC8253). GxEPD2 class: `GxEPD2_370_GDEY037T03`
- Refresh times: about 3 s full, 1 s fast, 0.2 s partial
- 8 pins: VCC, GND, SDA (MOSI), SCL (SCK), CS, D/C, RES, BUSY. 3.3 V logic, so no level shifter is needed.
- Power is selected by solder bridges: SB4+SB5 for 3.3–5 V, SB3 for 3.3 V only. **Check the default bridges on arrival.**
- **Set the 4-wire SPI pad before the first power-up.**
- Board is 105.79 × 53.80 mm. The 3.2 mm mounting holes are about 93 × 48 mm apart (needed for the case).
- In portrait orientation the page is 240×416, which is narrow.

## Power

- Path: LiPo → TP4056 OUT → slide switch → TPS63020 IN → TPS63020 OUT → ESP32 3V3 pin.
- Don't feed the DevKit's 5V pin: its onboard AMS1117 browns out at around 3.9 V.
- Battery gauge: 2× 100k divider plus 100 nF into an ADC pin.
- The DevKit draws a few mA in deep sleep (USB-UART chip, LED), so expect battery life in days, not weeks. That's fine for a prototype. If battery life matters at the defense, a LOLIN D32 or FireBeetle-style board fixes it.
- The display and SD card share SPI, with a separate CS pin each.
- Buttons go on RTC-capable GPIOs so they can wake the chip from deep sleep.

## Firmware ("custom OS")

Custom firmware on ESP-IDF/FreeRTOS, not a from-scratch kernel. It covers:

- Boot screen and launcher (library, settings, sleep)
- Button input and wake from deep sleep
- Storage layer on the SD card's FAT filesystem
- Power management: deep sleep, battery gauge, low-voltage shutdown
- Display layer: partial refresh on page turns, full refresh every N pages to clear ghosting

## Custom book format

A PC-side converter turns EPUB/TXT into the device format, so the device never parses EPUB. That removes the need for PSRAM. The S3 is still useful for native USB and for the 16 MB of flash, which holds the fonts.

Design points:

- Header and index: metadata, chapter table, byte offsets for seeking
- Pre-paginated (simple and fast, but locked to one screen and font) **or** reflowable (needs layout on the device). Not decided yet.
- Bitmap fonts with Romanian diacritics and Cyrillic
- Optional compression
- Bookmarks in a separate small file

For the thesis, the converter tool, the format spec and the device firmware together make the strongest case.

## Next steps (open)

- [x] ESP32-S3 pin map: done as the KiCad schematic, 2026-10-25 (see `../hardware/` and the Log in `README.md`)
- [ ] Book format spec v0.1
