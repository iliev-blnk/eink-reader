# Master plan: e-ink reader diploma

v1.0, 2026-10-25. Approved. The remaining open questions in [section 12](#12-open-questions) don't block Phase 0.

This file is the single source of truth for what gets built, in what order, by whom, and how we prove it works. When something changes, update this file and add a line to the Log in `README.md`.

**Status marks:** `[ ]` todo · `[~]` in progress · `[x]` done · `[-]` dropped (say why)

---

## 1. What we're building and why

A handheld e-ink e-reader, designed end to end:

1. **Hardware:** a perfboard prototype first, then a custom PCB.
2. **Firmware:** our own "custom OS", a layered firmware platform on FreeRTOS. It has its own drivers, services and apps.
3. **Book format:** our own file format, plus a PC tool that converts EPUB/FB2/TXT into it.
4. **Text layout engine:** real typography on a 240×416 1-bit screen: reflow, justification, and Romanian and Russian hyphenation.
5. **Measured evaluation:** every claim in the thesis is backed by a measurement.

**The thesis claim, in one sentence:** *Moving document parsing to a PC tool, together with a purpose-built format, layout engine and power-aware hardware, lets a low-cost microcontroller give a usable e-reading experience (good typography, fast page turns, weeks of battery life) without the application-processor-plus-Linux stack that commercial e-readers use.*

**Why it fits an EE + CS degree:** the EE half is the circuit design, power electronics, PCB and current measurements. The CS half is the firmware architecture, file format design, text layout algorithms and the toolchain. Neither half is padding.

## 2. Goals, non-goals, anti-goals

**Goals (must ship)**
- A device that opens a converted book and turns pages, with partial refresh, on battery.
- Reflowable text: 3+ font sizes, justified, hyphenated, with full Romanian (ă â î ș ț) and Cyrillic coverage.
- Library, reading progress, bookmarks, settings, sleep screen.
- A custom PCB that replaces the dev board and gets sleep current down to microamps.
- A converter for EPUB and TXT (FB2 if time allows) with a written, versioned format spec.
- A measurement chapter: timings, power, battery life, format efficiency, layout quality.

**Non-goals (deliberately out)**
- Parsing EPUB, PDF or HTML on the device
- DRM, online stores, cloud sync, accounts
- Images inside books (v1). Only the cover, dithered, on the sleep screen.
- Touchscreen, front light, audio
- Linux, or a from-scratch kernel. FreeRTOS is the kernel; everything above it is ours.
- Making it a product: no certification, no mass manufacturing

**Anti-goals (would count as failure even if it "works")**
- Many half-finished features instead of a few finished ones
- A device that only works with the one demo book
- Thesis claims with no measurement behind them
- Anything you can't explain yourself at the defense
- Unsafe LiPo handling: shorts, no protection, a battery that isn't secured

## 3. Constraints

| | |
|---|---|
| Deadline | ~9 months: submission around **end of July 2027** (exact date: open question Q2) |
| Degree | Electrical Engineering & Computer Science |
| Must-dos (your call) | measured results, text layout engine |
| Platform | ESP32-S3 (you own a DevKitC-1 16 MB). No PSRAM required by design. |
| Memory budget | 512 KB SRAM. Framebuffer is 240×416 at 1 bpp = 12,480 B. |
| Budget | Parts you've bought, plus roughly 600–900 RON more (see [section 9](#9-shopping-list)) |
| Ordering | PCBs from JLCPCB/PCBWay, shipped to Romania (VAT collected at checkout) |

## 4. System architecture

```
 PC side                                   Device side
 ┌─────────────────────────┐               ┌───────────────────────────────────────────┐
 │ converter (Python CLI)  │   microSD     │ Apps:      launcher · library · reader ·  │
 │  EPUB/FB2/TXT → parse   │  or USB-MSC   │            settings · sleep · diagnostics │
 │  → normalize (NFC, ș/ț) │ ────────────▶ │ Services:  layout engine · font renderer ·│
 │  → structure → hyphenate│   .book file  │            book reader · power manager ·  │
 │  → compress → write     │   + fonts     │            storage · settings · input     │
 │ fonttool: TTF → bitmap  │               │ Drivers:   UC8253 EPD · SD/FATFS · buttons│
 │ validator / dumper      │               │            · battery ADC / fuel gauge     │
 └─────────────────────────┘               │ HAL:       ESP-IDF (SPI, GPIO, ADC, NVS)  │
                                           │ Kernel:    FreeRTOS                       │
 PC simulator (same C core) → PNG pages    └───────────────────────────────────────────┘
```

**Key idea, my call:** the **core** (format reader, font renderer, layout engine) is portable C with no ESP-IDF dependencies. It compiles for the ESP32 and for the PC. The PC build is a **simulator** that renders pages to PNG. That gives us:
- unit tests that run on the PC
- software development that doesn't wait for hardware
- figures for the thesis
- layout-quality experiments on hundreds of pages in seconds

## 5. Repository layout (my call)

```
eink-reader/
  hardware/            prototype schematic (exists, v1.0)
  hardware/pcb/        custom board: schematic, layout, fab outputs, BOM, review checklist
  firmware/            ESP-IDF project
    components/core/   portable C: format, fonts, layout (shared with sim/)
    components/drivers/  epd_uc8253, sdcard, buttons, battery
    components/services/ power, storage, settings, input, ui
    main/              app manager + apps
    boards/            board_devkit.h, board_pcb_v1.h (pin maps)
  sim/                 PC build of core → PNG pages, tests
  tools/converter/     Python converter + validator
  tools/fonttool/      TTF → bitmap font
  spec/                book-format.md, font-format.md (versioned)
  measurements/<exp>/  raw CSV, scripts, generated figures (one folder per experiment)
  enclosure/           CAD + STL
  docs/                README log, brief, this plan, photos/, decisions
  thesis/              thesis source (format depends on Q1)
```

Version control: a local git repo from day one. A private GitHub remote is open question Q6.

---

## 6. Workstreams

Each workstream lists its deliverables, tasks, a "done when" check, and the thesis chapter it feeds.

### HW: Prototype hardware (perfboard, DevKit)

**Deliverable:** a working handheld prototype matching `hardware/eink-reader.kicad_sch`.
**Thesis:** Hardware design, plus the baseline for every before/after measurement.

- [ ] HW-1 Check the DevKit module marking: N16R8 (has PSRAM) or N16. Photograph it.
- [ ] HW-2 EPD module: photograph the SB3/SB4/SB5 bridges and the SPI-mode pad before any power. Set it to 4-wire SPI. I'll tell you exactly what to bridge from the photos.
- [ ] HW-3 Smoke test on a breadboard or jumpers with the bring-up app in `firmware/` (see `firmware/README.md`): text on the display, partial refresh, SD read and write, buttons, deep sleep and wake. App done 2026-10-25. Wire it up and flash it.
- [ ] HW-4 Buy the missing parts (section 9, "Now").
- [ ] HW-5 Check the LiPo JST polarity against the TP4056 B+/B− with the multimeter *before* connecting anything.
- [ ] HW-6 Perfboard layout drawing: where each module sits and the wire routing, so it fits roughly behind the 106×54 mm display.
- [ ] HW-7 Solder the prototype and photograph each stage.
- [ ] HW-8 Bring-up checklist: rail voltages at no load and under refresh, battery-sense ADC reading vs the multimeter, every button, SD at full SPI speed.
- [ ] HW-9 Update the schematic if anything changed during the build. Bump to v1.1 with a changelog.

**Done when:** it runs on battery, charges over USB-C through the TP4056, and every item in HW-8 passes.

### FW: Firmware platform ("custom OS")

**Deliverable:** an ESP-IDF firmware with the layered architecture from section 4.
**Thesis:** Firmware architecture.

Language and stack (my call): **C on ESP-IDF** (latest stable), with **our own UC8253 driver**. GxEPD2 is only a reference for the init sequences and a hardware sanity check. It's not a dependency.

- [x] FW-1 Project skeleton: components layout, `boards/*.h` pin maps, build and flash instructions. Install ESP-IDF, or run my install script. *Done 2026-10-25: `firmware/` builds on ESP-IDF v6.1, which was already installed.*
- [ ] FW-2 EPD driver: init, full, fast and partial refresh, deep-sleep command, BUSY handling via interrupt. Optional 4-level grayscale, if the panel supports it. *v0 2026-10-25: everything except grayscale, plus per-phase timing. Builds, but hasn't run on the panel yet.*
- [ ] FW-3 Graphics: 1 bpp framebuffer, rotation (portrait 240×416), dirty-rectangle tracking for partial refresh. *v0: framebuffer, UTF-8 text with a compiled-in font, dirty rectangles. No rotation yet.*
- [ ] FW-4 Storage: SPI SD + FATFS mount and recovery, file API used by the services. *v0: mount at 20 MHz with a 5 MHz fallback; never formats. No recovery or file API yet.*
- [ ] FW-5 Input: debounced buttons, short and long press, an event queue. *v0: all of it (10 ms polling, 30 ms debounce, 800 ms long press). Not run on hardware yet.*
- [ ] FW-6 App manager: an app lifecycle (enter, event, render, exit) and a FreeRTOS task layout (UI task, input task, background task for pagination).
- [ ] FW-7 Power manager:
  - light sleep while reading, deep sleep after idle timeout, wake on any button (ext1 ANY_LOW, RTC pull-ups enabled). *The button wake already works in the bring-up app.*
  - low-battery screen and shutdown below the cutoff
  - every state change timestamped, for the measurements
- [ ] FW-8 Battery: ADC calibration (eFuse curve), divider correction, a voltage→% curve for the LiPo, smoothing. *v0: calibration and divider correction. No percentage or smoothing yet.*
- [ ] FW-9 Settings in NVS: font size, line spacing, margins, full-refresh interval N, sleep timeout, hyphenation on/off.
- [ ] FW-10 Apps:
  - boot screen
  - launcher
  - library: list books from the metadata, sort by recent
  - reader
  - settings
  - sleep screen: cover or title, shown on an unpowered panel
  - diagnostics: voltages, timings, versions
- [ ] FW-11 Progress and bookmarks: sidecar files next to each book (see SPEC).
- [ ] FW-12 USB mass storage over native USB (TinyUSB MSC): plug into a PC, the SD card appears as a drive, copy books. FATFS is unmounted on the device while USB is active.
- [ ] FW-13 Measurement hooks: a `perf` log of timestamps (button edge, layout start/end, SPI transfer, BUSY low/high) written to SD as CSV, plus an "auto page-turn" test mode.
- [ ] FW-14 Flash and test on the prototype after every feature. Report and photograph anything weird.

**Done when:** you can read a whole novel on the prototype using only the device (library → book → read → sleep → wake → same page), with no crashes and no SD corruption after 100 sleep/wake cycles.

### SPEC: Book format

**Deliverable:** `spec/book-format.md` (v0.1 → v1.0) and `spec/font-format.md`.
**Thesis:** Book format design, with a comparison against EPUB/TXT/FB2.

My calls for design (to be justified in the spec):
- **Reflowable, not pre-paginated.** Text layout is a must-do, so pagination happens on the device.
- **Little-endian binary file:** magic + version header, metadata (title, author, language), a chapter table with byte offsets, and a content stream of paragraph records. Each record holds UTF-8 text plus style runs (bold, italic, heading level) and soft hyphens (U+00AD) inserted by the converter.
- **Compression per block** of ≤ 32 KB (whole paragraphs, 16 KB target), so RAM use and seek time stay constant however long a chapter is. DEFLATE, decoded by the miniz `tinfl` already in the ESP32-S3 ROM. Experiment E5 measures block sizes and decode time. *(Was "per chapter"; changed in SPEC-1 because TXT books are one chapter and real chapters pass the 32 KB window.)*
- **Cover:** an optional 1-bit dithered image block.
- **Integrity:** CRC32 on the header, the section directory, every section and every decoded block. The validator checks all of it, and a test flips every bit of a sample file.
- **Sidecar files** in `/.ibv/`, named by a content hash (`book_id`) so renaming a book keeps its progress: `<id>.pos` (progress), `<id>.bm` (bookmarks), `<id>-<layout>.pg` (pagination cache keyed by font, size, margins and spacing). Written atomically (tmp + rename).
- **Font format:** a glyph table with Unicode ranges, 1 bpp (optional 2 bpp) bitmaps, advance widths and optional kerning pairs. Fonts live in a flash partition (16 MB is plenty) or on SD.

- [x] SPEC-1 Write v0.1 with a byte-level layout diagram and a worked example. Done 2026-10-26: `spec/book-format.md`, reference codec + validator `tools/converter/ibv.py` (15 tests), example `spec/examples/example.ibv`.
- [ ] SPEC-2 Review it. Pick the name and file extension (Q5). Name done: IBV.
- [x] SPEC-3 Font format spec v0.1. Done 2026-10-26: `spec/font-format.md` (IBF), reference codec + validator `tools/fonttool/ibf.py`, C loader `firmware/components/core/font.c` that checks the same rules.
- [ ] SPEC-4 Freeze v1.0 after the converter and firmware both implement it. Changes after that bump the version.

**Done when:** the converter writes it, the validator checks it, the firmware and simulator read it, and the spec matches all three.

### CONV: PC converter and tools

**Deliverable:** `tools/converter`, a Python 3 CLI: `convert`, `validate`, `dump`. Plus `tools/fonttool`.
**Thesis:** Toolchain.

- [x] CONV-1 TXT input: UTF-8, plus CP1250/CP1251 detection for old Romanian/Russian files, and paragraph detection. Done 2026-10-26 (`txt.py`): BOM / UTF-8 / CP1250 / ISO-8859-2/-16 / CP1251 / KOI8-R / CP1252 scored per word; blank-line, indented and line-per-paragraph modes; Gutenberg and lib.ru headers; headings, scene breaks, verse, printed contents lists.
- [x] CONV-2 EPUB 2/3 input: OPF spine, NCX/nav TOC, XHTML → paragraph records with basic styles. Unknown tags degrade to plain text. Done 2026-10-26 (`epub.py`): nav → NCX → headings for the TOC, CSS bold/italic, footnotes and PG boilerplate dropped, poems → VERSE.
- [x] CONV-3 Normalization:
  - Unicode NFC
  - Romanian cedilla ş/ţ → comma ș/ț
  - quotes, dashes, ellipsis
  - whitespace

  Done 2026-10-26 (`textnorm.py`), plus per-language quotes, no space before punctuation, and Romanian typed with ã/º/þ mapped back.
- [x] CONV-4 Hyphenation: Liang's algorithm with the TeX hyphenation patterns for RO, RU and EN (check the licenses), selected by the book's language. Inserts soft hyphens. Done 2026-10-26 (`hyphenate.py`): own Liang implementation (not pyphen, so it stays portable) reading hyph-utf8 patterns. Licences in `tools/converter/patterns/README.md`: en-gb MIT, en-us permissive, ru LPPL, ro none stated (fallback: LibreOffice `hyph_ro_RO`).
- [x] CONV-5 Glyph coverage check against the target font. Warn about missing characters and map common fallbacks. Done 2026-10-26 (`glyphs.py`): character set read from fonttool's ranges, fallbacks, the rest → U+FFFD and reported.
- [x] CONV-6 Cover extraction → resize → Floyd–Steinberg dither → 1 bpp. Done 2026-10-26 (`cover.py`): fit 240×416, serpentine Floyd–Steinberg, ~50 ms.
- [x] CONV-7 Writer, validator, and a dumper that prints the human-readable structure. Done 2026-10-26 (`ibv.py`, `convert.py`): `convert`, `validate`, `dump`, `preview-cover`; every output is validated before it's written.
- [x] CONV-8 fonttool: TTF/OTF → our font format at chosen sizes, with the RO + Cyrillic + punctuation ranges. Font choice: open-licensed, e.g. Literata, Noto Serif or PT Serif (to compare in the simulator). Done 2026-10-26 (`tools/fonttool`, 23 tests): all three families × 4 styles × 16/18/20 px build, 19–41 KB per face; borrowed kerning (`kern_as`) for accented letters; stand-ins for missing glyphs. The pick is LAY-7.
- [x] CONV-9 FB2 input (common for Russian books). Should-have. Done 2026-10-26 (`fb2.py`): FB2 and `.fb2.zip`; tested on a synthetic book only.
- [x] CONV-10 Tests on the test corpus (see MEAS). Every book converts and validates. Done 2026-10-26: 66 tests; all 15 corpus files convert and validate (`corpus/README.md`). Opening them on the device waits for LAY/FW.

**Done when:** all corpus books convert and validate, and they open on the device.

### LAYOUT: Text layout engine (must-do)

**Deliverable:** the layout part of `firmware/components/core`, shared with `sim/`.
**Thesis:** Text layout.

- [x] LAY-1 Font renderer: glyph lookup, blit to the 1 bpp framebuffer, bold and italic (separate font files). Done 2026-10-26 (`font.c`, `gfx_glyph`): byte-shift blit, 1/64 px pen with kerning.
- [x] LAY-2 Line breaking v1: greedy, with soft hyphens as break points and justification by distributing spaces. Done 2026-10-26 (`layout.c`): also U+200B, in-word dashes, U+00A0 ties, U+2028, emergency cuts; ~20–30 µs per page on the PC.
- [x] LAY-3 Paginator:
  - lines → pages
  - margins, line spacing, paragraph indent, headings
  - widow/orphan control (should-have)

  Partly done 2026-10-26 (`layout_page`): forward pagination, margins, line and paragraph gaps, indent, headings centred with keep-with-next and chapters on a new page, breaks and ornaments, verse hanging indent, quote margins, footer with chapter and percent. Still open: widows/orphans, going back a page (needs LAY-4's page index).

  Done 2026-10-27: widow and orphan control (`keep_lines`), a heading left at the bottom of a page moves to the next, BREAKs at a page end are skipped so page starts are canonical. Going back a page (`layout_prev`) and finding the page that holds a position (`layout_locate`) lay out forward from the nearest sync point: the book start or a record flagged `PAGE` (new in the format, set on chapter headings by the converter).
- [ ] LAY-4 Pagination cache plus background pagination task, for page numbers and "x of y". Jumping to a chapter goes through the chapter offsets.

  Partly done 2026-10-27 (`pageidx.c`): the page index (8 B per page, built a few pages at a time, binary search) and its `.pg` sidecar (save, load, resume, CRC and layout-hash checks); the simulator footer shows "n / total" once it's complete. Still open: the background task that drives it on the device (with FW-5).
- [ ] LAY-5 Line breaking v2: Knuth–Plass (total-fit) per paragraph, with a memory cap and fallback to greedy for huge paragraphs.
- [x] LAY-6 Simulator: render any book or page to PNG at any setting, a batch mode for experiments, and golden-image tests. Done 2026-10-26 (`sim/`): `pages`, `stats` (whole books: chaining, overhang, timings, page hashes), `compare` (font sheets), `test/run.sh` golden test on a fixture book with every record kind.
- [x] LAY-7 Visual review: I render comparison sheets and you judge them on the real screen. Candidates: Literata, PT Serif, Noto Serif, and Libron (added 2026-10-27: Latin only, so it needs a Cyrillic fallback). Decided 2026-10-27 (you, from the sheets): **Libron for Latin-script books, PT Serif for Cyrillic, picked automatically by the book's language** (`fontsel.c`), five size steps matched across the two (spec/font-format.md, section 4). Still to confirm on the real screen once the prototype runs.

**Done when:** Romanian, Russian and English books lay out with no overflow, missing glyphs or broken diacritics at every font size. Knuth–Plass beats greedy on the E6 metrics and fits the page-turn time budget.

### PCB: Custom board

**Deliverable:** `hardware/pcb/` with the schematic v2, the layout, fab files (Gerbers, BOM, CPL), the assembled boards and a bring-up report.
**Thesis:** Hardware design (custom board) and power design.

Design direction (my call; part choices get final checks against JLCPCB stock when we design):
- **MCU:** ESP32-S3-WROOM-1-N16R8 module, with the antenna keep-out respected.
- **USB-C:** native USB for flashing, logs and mass storage, plus charging input. ESD protection.
- **Charger:** a single-cell charger with a power path, so it can run while charging. Candidates: BQ24074 / MCP73831 + load sharing. Plus battery protection (DW01 + dual MOSFET, or a protected cell).
- **3.3 V regulator:** a low-quiescent buck-boost. Candidates: TPS63802, or TPS63020 (same as your module).
- **Fuel gauge:** MAX17048 (I²C), compared against the ADC divider in the measurements.
- **Display:** v1 keeps the WeAct module on a header, which is low-risk. Stretch: drive the bare panel through a 24-pin FPC with our own boost circuit (Q4).
- microSD socket, 3 buttons, power switch, test points on every rail, a UART header, mounting holes matched to the display's 93×48 mm pattern.
- 4 layers (solid ground plane, good for the antenna and noise) unless cost forces 2.

- [ ] PCB-1 Requirements + parts selection table, with the reason for each part.
- [ ] PCB-2 Schematic v2 + ERC. Power budget calculation (feeds E3/E4 predictions).
- [ ] PCB-3 Board outline from the display dimensions + enclosure, placement, routing, DRC with JLCPCB rules.
- [ ] PCB-4 Design review against a checklist: footprints, polarity, boot/strapping pins, USB pinout, antenna clearance, test points. Read it through with me. Show it to your supervisor if they're willing.
- [ ] PCB-5 Order: 5 boards, JLCPCB assembly of the fine-pitch parts. I prepare the order files and say exactly what to click.
- [ ] PCB-6 Hand-solder the big parts (connectors, buttons, battery plug, display header).
- [ ] PCB-7 Bring-up, step by step:
  1. current-limited first power
  2. rails
  3. charger
  4. flash
  5. peripherals
  6. sleep current

  Log every bug in a bring-up report.
- [ ] PCB-8 Add `board_pcb_v1.h`. The firmware runs unchanged otherwise.
- [ ] PCB-9 If v1 has a blocking bug: a bodge-wire fix, or a v2 respin. There's slack for one respin in April.

**Done when:** the device runs entirely on the custom PCB, and deep-sleep current is measured in microamps.

### ENC: Enclosure

**Deliverable:** printed case holding the PCB, display, battery, buttons; CAD in `enclosure/`.
**Thesis:** Mechanical (short section + photos).

- [ ] ENC-1 Parametric CAD (OpenSCAD or similar). Inputs: display 105.79×53.80 mm with holes 93×48 mm, PCB outline, battery size.
- [ ] ENC-2 Print it: at the faculty, a maker space or an online service (Q3).
- [ ] ENC-3 Fit check → revise → final print.

**Done when:** it's handheld, closed, buttons work, USB-C reachable, battery can't move.

### MEAS: Measurements and evaluation (must-do)

**Deliverable:** `measurements/<exp>/` holds the method, raw CSV data, a script and the generated figures. Every number in the thesis comes from one of these folders.
**Thesis:** Evaluation.

Each experiment's method gets written **before** we run it, so results can't steer it.

| ID | Question | Setup | Metric | When |
|---|---|---|---|---|
| E1 | How long does each refresh mode take? | FW perf log + logic analyzer on BUSY | ms, full/fast/partial, n≥50 | Dec |
| E2 | How long is a page turn end to end, and where does the time go? | button edge → BUSY release. Breakdown: layout, render, SPI, panel. Light vs deep sleep between turns. | ms, mean/p95 | Feb + May |
| E3 | How much current does each state draw? | INA226 + logger board in series with the battery for the mA-level states. Deep sleep on the PCB (µA) is below the INA226's ~25 µA resolution with a 0.1 Ω shunt, so it's measured with the multimeter's µA range, with a jumper to bypass the meter during wake. | mA/µA per state, energy per page turn | Feb (DevKit) + May (PCB) |
| E4 | How long does the battery really last? | auto page-turn mode (1 page/min) until cutoff, both boards | hours/pages measured vs predicted from E3 | Mar (DevKit) + May (PCB) |
| E5 | Is our format worth it? | 20-book corpus | size (EPUB/TXT/ours ± compression), convert time, open time, chapter-jump time, peak RAM | Feb |
| E6 | Does better line breaking actually help? | simulator over the corpus: greedy vs Knuth–Plass × hyphenation on/off | adjustment-ratio stats, % hyphenated lines, consecutive hyphens, layout ms/page on device | Feb–Mar |
| E7 | How often do we need a full refresh? | photos at fixed lighting after N = 1/5/10/20 partial turns | ghosting (visual + contrast from photos) | Jan |
| E8 | Charging behaviour | USB-C tester | charge time, current curve | May (optional) |
| E9 | Do real readers like it? | 3–5 people read 20 min, short questionnaire | ratings + comments | May (optional) |

- [ ] MEAS-1 Test corpus: ~20 public-domain books in EN, RO and RU (Project Gutenberg, Wikisource), short and long, plain and with heavy formatting. List them with sources and licenses. In progress: 15 files / 13 books in `corpus/` (`fetch.sh`, README with sources and results). Missing: a real FB2 and a CP1250 Romanian TXT.
- [ ] MEAS-2 Logger firmware for the INA226 board, plus CSV → figure scripts.
- [ ] MEAS-3 One-page method sheet per experiment.
- [ ] MEAS-4 Run them: wiring photos, the raw data stays raw.
- [ ] MEAS-5 Analysis + figures + a short findings note per experiment. Each note goes straight into the thesis chapter.

**Done when:** E1–E7 have data for both boards where relevant, with figures.

### THESIS: Writing and defense

**Deliverable:** the thesis document, slides, a demo, and a backup video.

In **Romanian**, about **40 pages** (your answer to Q1). The topic is approved. Draft outline with a page budget (appendices on top). It gets adjusted to the faculty template:

1. Introduction: motivation, goal, contributions (2–3 p)
2. State of the art:
   - commercial readers (Kindle, Kobo, PocketBook, which run Linux on application processors)
   - open-source projects (KOReader, Inkplate, ESP32 reader projects)
   - e-paper technology
   - file formats
   - line-breaking algorithms

   (5–6 p)
3. Requirements and system architecture (3–4 p)
4. Hardware: prototype, custom PCB, power design (7–8 p)
5. Firmware platform (5–6 p)
6. Book format and converter (4–5 p)
7. Text layout engine (4–5 p)
8. Evaluation (MEAS) (6–7 p)
9. Conclusions and future work (1–2 p)

40 pages is tight for this much work, so the detail goes into the appendices (spec, schematics, BOM, measurement tables). The chapters keep only the decisions and the results.

Appendices: schematics, format spec, BOM, test corpus.

- [~] TH-1 Faculty requirements. Known: Romanian, ~40 pages, topic approved. Still needed: the template file, citation style, whether appendices count toward the 40 pages, the plagiarism-check rules.
- [ ] TH-2 State-of-the-art survey with real, checked references. I find and verify sources; no made-up citations.
- [ ] TH-3 Write each chapter when its phase closes, not all in June. I draft from the Log, the specs and the measurements. You rewrite it in your own voice and make sure you can explain every paragraph.
- [ ] TH-4 Figures: block diagrams, schematics, measurement plots, layout comparisons.
- [ ] TH-5 Slides plus demo script. Rehearse, and do a mock defense with me asking committee-style questions.
- [ ] TH-6 Record a backup demo video, in case the device misbehaves on the day.

### DOCS: Documentation, running the whole time

- After each work session that matters, add a dated **Log** entry in `docs/README.md`: what was done and why.
- Decisions go in [section 11](#11-decisions) here, with the reason.
- Photos: `docs/photos/YYYY-MM-DD-<what>.jpg`. Take a lot; they're thesis material. Drop them in, and I'll rename and caption them.
- Each workstream's "done when" gets checked off here, with the date.

---

## 7. Timeline and milestones

Today is 2026-10-25. Assumed submission: end of July 2027. Winter exams (Jan–Feb) are lighter months for hands-on work.

| When | Focus | Milestone (done when) |
|---|---|---|
| **Oct 26 – Nov 8** | Phase 0: setup. HW-1…5, FW-1, git repo, TH-1 (faculty rules, supervisor), order the "Now" parts | **M0:** text on the display, a file read from SD, faculty rules known |
| **Nov – Dec** | Prototype soldered (HW-6…9). Driver, storage, input, app manager (FW-2…6). SPEC v0.1. Converter TXT + EPUB (CONV-1…3, 7). Greedy layout + simulator (LAY-1, 2, 6). E1. | **M1 (Dec 20):** a converted book reads page by page on the prototype at one font size |
| **Jan – Feb** | Power manager, settings, apps, USB-MSC (FW-7…13). Hyphenation, fonts, paginator, Knuth–Plass (CONV-4…6, 8; LAY-3…5). E2, E3 (DevKit), E5, E6, E7. PCB-1, PCB-2 start in Feb. Write the chapter 2 draft. | **M2 (Feb 28):** firmware feature-complete on the prototype; DevKit baseline measured |
| **Mar – Apr** | PCB-3…8: order by **~Mar 15**, boards ~Apr 1, bring-up. E4 (DevKit). ENC. Respin slack in April. Draft chapters 3–7. | **M3 (Apr 30):** the device runs on the custom PCB inside the case |
| **May** | E2/E3/E4 on the PCB, E8/E9 optional. Bug fixes only, no new features. Draft chapter 8. | **M4 (May 31):** all measurement data in; feature freeze |
| **Jun – Jul** | Thesis complete, supervisor review rounds, slides, video, mock defense. Buffer weeks. | **M5:** submitted |

**Rule:** if a milestone slips by more than 2 weeks, we cut should-haves (FB2, 2 bpp fonts, E8/E9, widow control) before touching must-haves.

## 8. Priorities

| Must | Should | Stretch |
|---|---|---|
| Prototype, own EPD driver, firmware platform, apps, power manager | FB2 input | Bare-panel drive circuit on PCB v2 |
| Format spec + converter (TXT, EPUB) | USB mass storage | 4-level grayscale / antialiased fonts |
| Layout: reflow, justification, hyphenation, 3+ sizes | Knuth–Plass (greedy is the fallback) | Wi-Fi book upload page |
| Custom PCB v1 + enclosure | Fuel gauge IC vs ADC comparison | Usability study (E9) |
| Measurements E1–E7 | Widow/orphan control | |

## 9. Shopping list

Prices are rough (RON). I'll check current ones before each purchase.

**Now (Phase 0–1)**
- [ ] Multimeter UT33A, unless your soldering kit has one: ~92. It **must have a µA DC range** for the deep-sleep measurements. The UT33A does (200/2000 µA or 400/4000 µA, depending on the version).
- [ ] 3–5 tact buttons, SS12D00 slide switch, perfboard, female headers, JST PH2.0 pigtail: ~40–60
- [ ] Dupont jumper wires, female–female, ~40 pcs, for the smoke test before soldering: ~10–15 (skip if you have them)
- [ ] Flux (pen or paste), unless the kit has it: ~15–25
- [ ] Check you have: 2× 100 kΩ resistors and a 100 nF capacitor (battery sense)
- [ ] USB logic analyzer, 24 MHz 8-channel (Saleae-clone class): ~50–90. Needed for E1/E2 and driver debugging.
- [ ] INA226 current-sensor module: ~25–45 (INA219 works as a fallback). [Sigmanortec](https://sigmanortec.ro/en/current-and-power-monitor-sensor-ina226-i2c-iic-bidirectional-16bit-27-55v)
- [ ] Logger board for the INA226. Your old ESP32-WROOM-32D if you still have it; otherwise an ESP32-C3 SuperMini, ~27 at [Sigmanortec](https://sigmanortec.ro/en/esp32-c3-supermini-development-board-33v-wifi-bluetooth) (2026-10-25).

**Phase 3 (March)**
- [ ] PCB order, 5 boards + assembly + shipping: ~$30–60 (~140–280)
- [ ] Spare LiPo, the same type: ~30–50
- [ ] Enclosure printing: depends on Q3
- [ ] USB-C power tester, for E8: ~40–60 (optional)

**Stretch only:** a bare GDEY037T03 panel, ~$10–15.

## 10. Risks

| Risk | Likelihood | Impact | What we do |
|---|---|---|---|
| EPD driver quirks (ghosting, partial-refresh LUTs) | Medium | Medium | GxEPD2 as a reference, logic analyzer, E7 to pick N |
| Knuth–Plass too slow or too memory-hungry on long paragraphs | Medium | Low | Memory cap + greedy fallback, measured in E6 |
| PCB v1 has a bug | Medium | High | Review checklist, test points, current-limited bring-up, bodge wires, respin slack in April. The prototype stays a working fallback demo. |
| Fine-pitch soldering | High if done by hand | Medium | JLCPCB assembles those parts |
| Your available time (job, exams) | High | High | The simulator lets software move without hardware. Hands-on tasks are batched, and you get a checklist before each one. |
| LiPo damage or fire | Low | High | Protected circuit, polarity check (HW-5), never charge unattended at first, keep the battery secured in the case |
| Faculty rules (format, scope approval) clash with the plan | Unknown | High | TH-1 in Phase 0 |
| Lost work | Low | High | git from day one + remote (Q6) |
| Missing glyphs or broken diacritics | Medium | Medium | CONV-5 coverage check, RO/RU books in the corpus from day one |

## 11. Decisions

**Yours**
- 2026-10-25: Measurements and the text layout engine are must-dos.
- 2026-10-25: Custom OS on the ESP32 (FreeRTOS + our layers), not a Linux distro.
- 2026-10-25: I do the build work; you do the hands-on work and the defense.
- 2026-10-25: Thesis in Romanian, ~40 pages. The topic is approved.
- 2026-10-25: Custom PCB, ordered for delivery in Romania. *(My reading of "how can I get the custom PCB in Romania". Correct me if that's wrong.)*
- 2026-10-26: The book format is named IBV, *Ink Book Volume* (also your initials), extension `.ibv`.

**My calls** (change any of them; each one gets a reason in the thesis)
- C on ESP-IDF; our own UC8253 driver
- Portable C core shared with a PC simulator
- Reflowable binary format, hyphenation done in the converter as soft hyphens, line breaking on the device
- Greedy line breaking first, then Knuth–Plass
- Compression per ≤ 32 KB block (ROM miniz/DEFLATE), measured in E5
- Python converter (CLI only, no GUI)
- PCB v1 keeps the WeAct display module; fuel gauge IC; 4-layer board
- JLCPCB with assembly
- INA226 + logic analyzer for measurements
- Methods written before experiments
- Repository layout in section 5
- Phase 0 smoke test: our own ESP-IDF bring-up app instead of an Arduino + GxEPD2 sketch (2026-10-25). It tests our driver from day one. If the panel stays blank, GxEPD2's example is the fallback, to tell a wiring fault from a driver bug.

## 12. Open questions

These are yours to answer; each one changes the plan:

- ~~Q1~~ Answered: Romanian, ~40 pages, topic approved. Still to get: the template file and citation style (TH-1).
- **Q2** The exact submission date and defense date.
- **Q3** Do you have access to a 3D printer (faculty, maker space, friend), or should we plan an online print service?
- **Q4** Stretch goal: should PCB v2 drive the bare display panel with our own boost circuit? It's more EE depth, but it costs ~3 extra weeks plus the risk.
- ~~Q5~~ Answered: the book format is **IBV** (*Ink Book Volume*), extension `.ibv`.
- **Q6** Should the code go to a private GitHub repo on your account (iliev-blnk) as an off-machine backup?
- **Q7** Roughly how many hours a week can you give to the hands-on tasks?
