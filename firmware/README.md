# Firmware

ESP-IDF v6.1 project for the ESP32-S3. Right now it builds the **Phase 0 bring-up app**, which checks each part of the prototype and shows the results on the display.

```
firmware/
  boards/              pin maps, one header per board; pick the board in menuconfig
  components/core/     portable C (fonts, 1-bit graphics); sim/ builds the same files
  components/drivers/  epd_uc8253 (display), sdcard, buttons, battery
  main/                the bring-up app and its screen
```

## Build

ESP-IDF lives in `~/esp/esp-idf`. Load it once per terminal:

```sh
. ~/esp/esp-idf/export.sh
cd ~/DEV/eink-reader/firmware
idf.py build
```

The ESP32-S3 target and the settings that matter (16 MB flash, octal PSRAM, the custom partition table) come from `sdkconfig.defaults`. `sdkconfig` is generated and not tracked. To start clean: `rm sdkconfig && idf.py set-target esp32s3`.

## Flash

The DevKitC-1 has two USB-C ports. Use the one labelled **UART**: it stays connected while the chip is in deep sleep, so you can see it wake up. The **USB** port (native USB) also flashes, but it drops off the bus during sleep.

1. One time only on Arch: `sudo usermod -aG uucp $USER`, then log out and back in. Without this the port says "permission denied".
2. Find the port. Run `ls /dev/ttyUSB* /dev/ttyACM*` before and after plugging the board in. The new entry is the port.
3. Flash and watch the log:

   ```sh
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

   To leave the monitor, press `Ctrl+]`. If flashing can't connect, hold **BOOT**, tap **RST**, then release **BOOT**.

## Wiring (prototype, `boards/include/board_devkit.h`)

| Signal | GPIO | Notes |
|---|---|---|
| SPI SCK / MOSI / MISO | 12 / 11 / 13 | shared by the display and the SD card |
| Display CS / DC / RST / BUSY | 10 / 9 / 8 / 18 | the display module must be set to 4-wire SPI |
| SD card CS | 14 | |
| Buttons PREV / NEXT / MENU | 4 / 5 / 6 | each button connects the pin to GND; the pull-ups are internal |
| Battery sense | 1 | 100k/100k divider from the battery, 100 nF to GND |

Everything runs on **3V3**, never 5V.

## What the bring-up app does

At boot it reads the chip, flash and PSRAM, the battery voltage and the SD card, then does a full refresh of this screen. The PC simulator draws the same screen: see `sim/`.

- **PREV / NEXT**: count down / up and redraw only the counter box with a partial refresh. A long press steps by 10. The box shows how long the previous refresh took.
- **MENU**: re-read the battery, retry the SD card if it failed, and redraw everything with a full refresh.
- **Hold MENU**: show "Sleeping", put the display and the chip into deep sleep. Any button wakes it. "Wake" on the screen then shows the button that woke it, plus a boot counter that survives sleep.

Each boot appends a line to `bringup.txt` on the SD card. **The app never formats the card.** The card must be FAT32, because exFAT is not supported. Cards over 32 GB usually come as exFAT, so reformat those as FAT32 first.

## Timing lines (experiment E1)

Every refresh prints a machine-readable line to the serial log:

```
PERF,epd_partial,x=8,y=282,w=224,h=56,write_us=…,on_us=…,refresh_us=…,off_us=…,total_us=…
PERF,epd_full,…
PERF,sd_mount,ok=1,us=…
PERF,battery,mv=…
```

To save the log to a file, press `Ctrl+T` then `Ctrl+L` inside the monitor. It writes `log.eink-reader.<time>.txt` in the current folder. Press the same keys again to stop. Then filter it with `grep ^PERF log.eink-reader.*.txt`.

## If something's wrong

- **"e-paper not answering" in the log**: the display never pulled BUSY low. Check BUSY, RST, CS, DC, SCK and MOSI against the table, check 3V3 and GND, and check that the module's interface pad is set to 4-wire SPI.
- **"BUSY still low"**: the display started a refresh and didn't finish it. Usually this means a weak 3V3 supply or a loose GND.
- **SD "no card (timeout)"**: check the card is seated and the SD module gets 3V3. Some modules have a regulator and only work from 5V. Then check CS=14 and MISO=13.
- **SD "not FAT32 formatted"**: the card answered but has no FAT volume. Reformat it as FAT32 on a PC.
- **Battery "none (…)"**: nothing is on the divider. That's expected while running from USB.
