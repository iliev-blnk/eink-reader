# Diploma documentation material

Raw material for writing up the e-ink reader diploma.

- `MASTER-PLAN.md`: the master plan. Workstreams, tasks, timeline and milestones, the measurement plan, shopping list, risks, decisions and open questions. The single source of truth.
- `project-brief.md`: what the project is, the parts list, open purchases, display/power notes, firmware scope and the book-format plan.
- `../hardware/`: the KiCad schematic (`eink-reader.kicad_sch`), its PDF/SVG exports and `gen.py`, which generates them.
- `../firmware/`: the ESP-IDF firmware. Its README covers build, flash and wiring.
- `../sim/`: the PC simulator. It builds the firmware's portable core (fonts, IBV reader, layout) and renders screens and book pages to PNG, with whole-book stats and golden-image tests. See its README.
- `../tools/fonttool/`: TTF/OTF → IBF bitmap fonts, the font format's reference codec (`ibf.py`) and tests. See its README.
- `../tools/converter/`: the book converter (EPUB / FB2 / TXT → `.ibv`), the format's reference reader, writer and validator (`ibv.py`), and their tests. See its README.
- `../corpus/`: the test books (`fetch.sh` downloads them; not tracked) and their conversion results.
- `../spec/`: the book format spec (`book-format.md`), the font format spec (`font-format.md`) and a worked example file (`examples/`).

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

### 2026-10-26: book format spec v0.1 (SPEC-1)
- `spec/book-format.md`: byte-level layout of the header, section directory, META, TOC, block index, records and cover; device reading algorithm and RAM budget; sidecar files; validator rules; versioning; a table of design choices with the alternatives turned down; preliminary sizes. Named IBV (Q5), extension `.ibv`.
- Reference codec `tools/converter/ibv.py` (write, parse, validate, dump, and `from-lines` for the mockup texts) with 15 tests. One test flips every bit of a sample file; it found that a damaged directory tag went unnoticed, so the header gained a directory CRC.
- Worked example `spec/examples/example.ibv` (240 B, uncompressed), annotated byte by byte in the spec. Rebuild with `python3 spec/examples/make_example.py`.
- Two changes to the plan: compression per ≤ 32 KB block instead of per chapter (Moara's longest chapter is 37 KB, a TXT book is one chapter), and sidecars keyed by a content hash in `/.ibv/` instead of `<book>.pos`.
- Preliminary sizes on the four mockup books: 35–50 % of the plain text at 16 KB blocks, about 1.3× whole-file gzip. Soft hyphens add 18.5 % to the raw text of Moara. Not E5 yet (needs the 20-book corpus and on-device timings).
- Q5 decided: the format is **IBV**, *Ink Book Volume* (also the author's initials), extension `.ibv`. Renamed the codec to `tools/converter/ibv.py`.
- Next: SPEC-2 review.

### 2026-10-26: book converter (CONV-1…7, 9, 10)
- `tools/converter/convert.py`: `convert` (EPUB 2/3, FB2, `.fb2.zip`, TXT; single book or batch), `validate`, `dump`, `preview-cover`. Every output is validated before it is written.
- Readers `txt.py`, `epub.py`, `fb2.py` fill a shared `doc.Builder`. `doc.finish()` normalizes (`textnorm.py`), maps missing glyphs (`glyphs.py`), turns `<br>`-joined short lines into verse, tidies breaks, hyphenates prose (`hyphenate.py`, own Liang + TeX patterns), splits long paragraphs and resolves the TOC. Covers: `cover.py`, serpentine Floyd–Steinberg to 1 bpp.
- TXT: encodings scored per word (both Dubrovsky files, KOI8-R and CP1251, give byte-identical books); Gutenberg and lib.ru headers become metadata; printed contents lists aren't turned into TOC entries.
- Validator: text must also be NFC (added to spec rule 5).
- Tests: `test_convert.py`, 51 tests on synthetic EPUB/FB2/TXT inputs plus `TestCorpus`; 66 with `test_ibv.py`, all passing.
- Corpus (MEAS-1, partly): `corpus/fetch.sh` downloads 15 files / 13 books (EN 6, RO 5, RU 4, counting the TXT copies). All convert and validate in 0.4–3.2 s each; table in `corpus/README.md`. Moby-Dick: 7 Greek/Hebrew letters missing from the font.
- Still open: a real FB2 book and a CP1250 Romanian TXT for the corpus; ~5 more books to reach 20; CONV-8 (fonttool) separately; opening the books on the device needs LAY/FW.

### 2026-10-27: book fonts decided, picked by language (LAY-7)
- Decision (yours, from the comparison sheets): Libron for Latin-script books, PT Serif for Cyrillic ones. The reader never picks a family: the device takes it from the book's META `language`.
- `fontsel.c` (portable core): `fontsel_cyrillic(lang)` reads the BCP 47 tag (Cyrillic languages ru, uk, be, bg, sr, mk, kk and others; a `Cyrl` or `Latn` script subtag wins, so `sr-Latn` is Libron), `fontsel_pick(lang, step)` gives the family and size, `fontsel_path` the file name in `/fonts/`.
- One size setting with five steps, each a pair of sizes matched on x-height and on page counts (Pride and Prejudice: Libron 15/16/17/18/20 = 1,541/1,845/2,081/2,370/2,844 pages, PT Serif 16/17/18/19/21 = 1,601/1,803/2,042/2,302/2,753). Default is step 3, Libron 17 / PT Serif 18, so a reader who switches between a Romanian and a Russian book sees the same text size.
- The converter now checks each book against its own font's characters (`glyphs.charset_for`): a Latin-script book gets no Cyrillic and none of Ĳ ĳ ŉ ſ (ſ → s). Before this, a Cyrillic word in an English book would have passed the converter and shown as boxes on the device. A test checks that the converter's language list matches `fontsel.c`. 69 converter tests pass.
- `sim --fonts DIR [--size N]` picks the font per book as the device does; `stats` has a new `miss` column (characters with no glyph in the book's font). `run.sh` paginates the corpus a second time in the device fonts and fails if any book has a missing glyph. All 15 pass with 0 (forcing Libron onto *Dubrovsky* gives 108,792, so the check works). Firmware builds.

### 2026-10-27: Libron as a fourth font candidate (LAY-7)
- Libron (Nico Verbruggen, OFL, from Readerly / Newsreader) is made for e-readers. It covers Romanian fully, ș ț and the cedilla forms included, but has **no Cyrillic**, so a Russian book set in it would show U+FFFD boxes. Using it means a second font for Cyrillic books (picked from the book's language), or Libron only for Latin-script books.
- The plain release has very tight line spacing (19 px lines at 18 px), so the accents on ș and ț nearly touch the line above. The "R" release has relaxed spacing and is the one we use: at 16 px it has a 23 px line and a 9 px x-height, about the same as Literata 16.
- It's narrow. Pages for Pride and Prejudice / Slavici / Frankenstein: Literata 16 2,105 / 767 / 1,214; PT Serif 18 2,042 / 747 / 1,170; Libron 16 1,845 / 671 / 1,056; Libron 17 2,081 / 756 / 1,198; Libron 18 2,370 / 857 / 1,365. Libron 17 is about as dense as the current two favourites, and its narrower letters leave fewer wide gaps in justified lines.
- `fetch-fonts.sh` downloads Libron_R v0.32, and `fonttool.py family libron` builds it (unhinted, so grey rendering at threshold 128), 31–36 KB per face. Sheets are in `tools/fonttool/out/sheets/libron-vs-{en,ro}.png`.
- Like PT Serif, Libron has a Reserved Font Name. Our IBF bitmaps are a converted version, so before release the name shown on the device needs checking against the OFL (or permission from the author).

### 2026-10-27: widows/orphans, paging back, page index (LAY-3, most of LAY-4)
- Widow and orphan control: when a page fills mid-paragraph, lines move so no paragraph leaves a single line at the bottom or top of a page (a 2-line paragraph moves whole). A heading left at the bottom of a page moves with what follows. Over the corpus it takes widows and orphans from up to 667 / 687 (pg1322, poetry) to 0 in every book, for 0.5–4.7 % more pages. Off with `--no-keep-lines`; it's part of `layout_hash`.
- Going back a page: page breaks depend on the pages before, so the layout lays out forward from the nearest *sync point* until it reaches the current page. At first the only sync points were level-1 headings, but in an EPUB level 1 is usually just the book title, so going back cost the whole book.
- Fix: new format flag `PAGE` (bit 6, spec 0.1 changelog): "this record starts a page when chapters start pages". The converter sets it with a heuristic: a heading level is a chapter level if its sections' median length is ≥ 3,000 B, and a heading on it gets `PAGE` if its own section is ≥ 750 B (so short poems that share a level with chapters don't) and it doesn't directly follow another heading. Spine files were rejected as chapter marks: Gutenberg splits them by size, mid-text. Test `test_chapters_start_pages`; 67 converter tests pass.
- Going back now lays out 13–56 pages on average (max 191) in books with chapters. Books without (pg1322, pg14741, pg16527, pg35323) still cost about half the book, so they rely on the index.
- Page index (`pageidx.c`): every page start, 8 B per page, built incrementally, found by binary search, saved as the `.pg` sidecar. FATFS can't rename over a file, so a save deletes the old one first (spec section 6 corrected). Whole corpus at the default UI font: 7–68 ms to index a book on the PC, 16–70 µs per page.
- `sim stats` checks per book: pages chain, every `layout_prev` lands on the previous start, `layout_locate` and `pageidx_find` agree, prev of the first page is RANGE, the sidecar round-trips and rejects a different layout or a flipped byte. All 15 books pass. Goldens updated after review (orphans moved, a late heading moved, "n / 10" footers).
- Firmware still builds. Still open: the background pagination task on the device (with FW-5), Knuth–Plass (LAY-5), LAY-7 font pick.

### 2026-10-26: fonts, layout engine and book simulator (SPEC-3, CONV-8, LAY-1, 2, 6, part of 3)
- Font format IBF v0.1, `spec/font-format.md`: 96-byte header, Unicode ranges, glyph table, bitmaps, kerning in 1/64 px. Accented letters borrow their base letter's kerning (`kern_as`) and only the pairs where that's wrong are stored, which took Literata from 7,596 pairs (45 KB) to 3,844. Missing glyphs get stand-ins (U+2011 → '-', an accented letter → its base).
- `tools/fonttool`: `build`, `family`, `c` (C tables for the built-in UI fonts), `validate`, `dump`, `sheet`; reference codec `ibf.py`; 23 tests. Literata, PT Serif and Noto Serif build in 4 styles at 16/18/20 px, 19–41 KB per face.
- A bug the first rendered pages showed: Literata swaps glyphs by optical size (OpenType feature variations). Advances were measured through HarfBuzz but bitmaps rendered by FreeType alone, so they came from different glyphs and every italic "w" had a gap after it. Bitmaps now go through HarfBuzz too; a test covers it.
- Firmware core (portable C, also built by the simulator): `font.c` (loads an IBF in place and checks every spec rule), `gfx_glyph` (byte-shift blit), `crc32.c`, `ibv.c` (IBV reader: header, sections, two-block cache, CRC per block, style runs), `inflate_rom.c` (ESP32-S3 ROM miniz; the simulator uses zlib), `layout.c` (greedy line breaking with soft hyphens and justification in 1/64 px, pagination). The firmware still builds: 346 KB app.
- Layout rules: PARA justified with an indent; QUOTE with both margins; VERSE left with a hanging indent; HEADING centred, kept with two lines after it, level 1 on a new page; BREAK as a blank line or "* * *", dropped at the top of a page; CONT continues without indent or gap. Breaks at spaces, soft hyphens, U+200B and dashes inside words; U+00A0 stretches but doesn't break.
- `sim`: `pages` (book → PNGs, with cover and footer: chapter + percent), `stats` (paginates whole books, checks that pages chain and advance and how far ink leaves the text area, times it, hashes every page), `compare` (one page side by side in several fonts). Golden test `sim/test/run.sh` on `sim/test/layout.ibv`, a fixture with every record kind and special character.
- All 15 corpus books paginate in every font: 20–50 µs per page on the PC, ink at most 1–4 px outside the text area (italic overhang). Pride and Prejudice: 2,057 pages in Literata 16, 1,994 in PT Serif 18.
- Ready for LAY-7: comparison sheets of the three fonts (EN and RU) and of Literata at 16/18/20.
- Still open: LAY-3 widows/orphans and paging backwards (LAY-4), Knuth–Plass (LAY-5, greedy leaves wide gaps on 216 px lines), running the layout on the device (needs the prototype).
