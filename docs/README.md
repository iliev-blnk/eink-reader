# Diploma documentation material

Raw material for writing up the e-ink reader diploma.

- `MASTER-PLAN.md`: the master plan. Workstreams, tasks, timeline and milestones, the measurement plan, shopping list, risks, decisions and open questions. The single source of truth.
- `project-brief.md`: what the project is, the parts list, open purchases, display/power notes, firmware scope and the book-format plan.
- `../hardware/`: the KiCad schematic (`eink-reader.kicad_sch`), its PDF/SVG exports and `gen.py`, which generates them.
- `../firmware/`: the ESP-IDF firmware. Its README covers build, flash and wiring.
- `../sim/`: the PC simulator. It builds the same drawing code as the firmware and renders screens to images.
- `../tools/fonttool/`: converts a TTF font into the firmware's bitmap font.

## Log

### 2026-10-25: schematic v1.0
- Built in KiCad 10, because sPlan and Proteus use closed formats that can't be generated.
- Modules: ESP32-S3-DevKitC-1 16 MB (PSRAM unconfirmed; IO35–37 kept free in case it's N16R8), WeAct 3.7" EPD (UC8253, SPI), microSD module, TP4056 (DW01 protection), TPS63020 3.3 V buck-boost, 1S LiPo.
- Power path: LiPo → TP4056 B+/B− → OUT+ → SW1 → VSYS → TPS63020 → +3V3 (DevKit 3V3 pin, EPD, SD). System GND = TP4056 OUT−.
- The EPD and SD card share SPI2: SCK IO12, MOSI IO11, MISO IO13. EPD CS/DC/RST/BUSY = IO10/9/8/18, SD CS = IO14.
- Buttons PREV/NEXT/MENU on IO4/5/6 (RTC-capable, so they can wake from deep sleep), wired to GND with internal pull-ups. Battery sense on IO1 through a 100k/100k divider plus 100 nF.
- Avoided pins: IO35–37 (used by octal PSRAM on N16R8) and the strapping pins 0/3/45/46.
- ERC shows 0 violations.

### 2026-10-25: project brief collected
- Brought the earlier planning notes (parts, display, power, firmware, book format) into `project-brief.md`.
- Checked the schematic against them and it matches: 3V3-pin feed (not 5V), shared SPI with separate CS pins, wake buttons on RTC GPIOs, 100k/100k + 100 nF gauge.
- To do on the hardware: set the EPD's 4-wire SPI pad and check its SB3/SB4/SB5 power bridges before first power-up. Check the JST polarity on the LiPo.

### 2026-10-25: master plan draft v0.9
- Decisions from the brainstorm: measurements and the text layout engine are must-dos. Custom OS on the ESP32 (FreeRTOS + our own layers), not Linux, because the S3 has no MMU and a distro would mostly be configuration rather than original work. Custom PCB added, because it suits the EE half of the degree and fixes the DevKit's mA-level sleep current, which also gives a before/after measurement. Deadline ~9 months (end of July 2027).
- Wrote `MASTER-PLAN.md`: 10 workstreams, milestones M0–M5, experiments E1–E9, open questions Q1–Q7.
- Plan approved as v1.0. Thesis answers recorded (Romanian, ~40 pages, topic approved). Shopping list refined: µA-range multimeter required, jumpers and flux added, Sigmanortec links.

### 2026-10-25: Phase 0 firmware, skeleton and bring-up app
- ESP-IDF v6.1 project in `firmware/`. It builds for the ESP32-S3 with no warnings, and the app uses 343 KB of the 4 MB app partition. Board pin maps are their own component (`boards/`), so the custom PCB can be added later as a second board.
- Our own UC8253 display driver, written from the command set: full refresh (normal and a ~1 s "fast" waveform), partial refresh of a window, deep sleep, BUSY on an interrupt instead of polling. Each refresh records the time spent in each phase (SPI write, power on, refresh, power off), which is the raw data for E1.
- Drivers for the SD card (FAT32 over the shared SPI bus, never formats), the buttons (debounce, short and long press, event queue, wake from deep sleep) and the battery ADC (eFuse calibration).
- Bring-up app: shows chip, memory, SD, battery and wake reason; PREV/NEXT test partial refresh, MENU tests full refresh, holding MENU tests deep sleep and button wake. Prints `PERF,...` lines for every refresh and appends a line per boot to `bringup.txt` on the card.
- Portable core: 1-bit framebuffer, UTF-8 text, a 16 px Noto Sans bitmap font (Romanian with comma-below ș ț, Cyrillic). The simulator renders the same screens on the PC. The longest strings the firmware can produce were checked there to make sure they fit on the 240 px width.
- Change from the plan: the smoke test uses this app instead of an Arduino + GxEPD2 sketch, so our driver is tested from the first power-up. GxEPD2 remains the fallback if the panel stays blank.
- Not yet run on hardware: the parts aren't wired yet.

### 2026-10-26: case mockup (3D, real size)
- Interactive 3D mockup of three case options on an A3 cutting mat beside a 19 cm banana: A chin keys (58 × 132 × 12 mm), B side keys (82 × 110 × 12 mm, recommended), C edge keys (58 × 110 × 13 mm).
- Display geometry is from the GDEY037T03 / WeAct drawings. Case sizes are estimates until the PCB is laid out.
- The screens are drawn by the firmware's own gfx and font code (`docs/mockup/mock.c`). Reader pages use pyphen soft hyphens and a whole-paragraph line breaker, which is an early preview of the LAYOUT workstream. Sleep and Bring-up are exactly what today's firmware draws, with sample hardware values.
- Rebuild: see the commands in the header of `docs/mockup/mock.c`, then `python3 docs/mockup/build.py docs/mockup/screens docs/mockup/template.html docs/mockup/eink-mockup.html`.
- Next: pick a case, then ENC-1 (CAD) and the PCB outline.
