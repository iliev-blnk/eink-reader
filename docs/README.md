# Diploma documentation material

Raw material for writing up the e-ink reader diploma.

- `MASTER-PLAN.md`: the master plan. Workstreams, tasks, timeline and milestones, the measurement plan, shopping list, risks, decisions and open questions. The single source of truth.
- `project-brief.md`: what the project is, the parts list, open purchases, display/power notes, firmware scope and the book-format plan.
- `../hardware/`: the KiCad schematic (`eink-reader.kicad_sch`), its PDF/SVG exports and `gen.py`, which generates them.

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
