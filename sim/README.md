# Simulator

Builds the firmware's portable core (`firmware/components/core`: fonts, graphics, the IBV reader, layout) for the PC and writes what the 240×416 panel would show as PNG files. Needs CMake, a C compiler and zlib.

    cmake -S sim -B sim/build && cmake --build sim/build
    sim/build/sim screens -o sim/out                                       # boot and sleep screens
    sim/build/sim pages corpus/out/pg1342.ibv -o sim/out --cover -n 20 -f tools/fonttool/out/fam/literata-16
    sim/build/sim stats corpus/out/*.ibv -f tools/fonttool/out/fam/ptserif-18   # paginate whole books
    sim/build/sim stats corpus/out/*.ibv --fonts tools/fonttool/out/fam          # each book in its device font (fontsel.c); --size 0-4 = steps 1-5
    sim/build/sim compare corpus/out/pg1342.ibv --page 30 -o cmp.png tools/fonttool/out/fam/literata-16 tools/fonttool/out/fam/ptserif-18
    sim/test/run.sh                                                        # golden-image test

Without `-f` the built-in UI font is used. Layout options: `--margin`, `--indent`, `--line-gap`, `--para-gap`, `--ragged`, `--no-hyphen`, `--no-chapter-page`.

`stats` checks, for every page of every book, that pages chain and advance and how far ink goes outside the text area (side bearings and italics: 1–4 px; more fails). It also prints a hash of every page's pixels.

`test/run.sh` renders `test/layout.ibv` in two configurations and compares them with `test/golden/`. That book is made by `test/make_fixture.py` and has every record kind, flag and special character. After a deliberate layout change, look at the new pages and run `test/run.sh --update`. The script also paginates the corpus if it has been converted.

On the device, the only difference is `inflate_rom.c` (the ESP32-S3 ROM's miniz) instead of `inflate_zlib.c`.
