# fonttool

Turns TTF/OTF fonts into IBF bitmap fonts for the reader (format: `../../spec/font-format.md`). Python 3 with Pillow built with libraqm (HarfBuzz), which ships in the standard Pillow wheels.

    ./fetch-fonts.sh                                     # Literata, PT Serif, Noto Serif -> fonts/ (OFL, not tracked)
    python3 fonttool.py family literata --sizes 16,18,20 -o out/fam    # -> out/fam/literata-18-r.ibf, -b, -i, -bi
    # the device set (spec/font-format.md, section 4): copy out/fam/libron-* and ptserif-* to /fonts/ on the card
    python3 fonttool.py family libron --sizes 15,16,17,18,20 -o out/fam
    python3 fonttool.py family ptserif --sizes 16,17,18,19,21 -o out/fam
    python3 fonttool.py build --ttf X.ttf --size 18 --style i -o x.ibf # one face
    python3 fonttool.py c --ttf NotoSans-Regular.ttf --size 16 --name font_ui16 --out ../../firmware/components/core/fonts/font_ui16.c
    python3 fonttool.py validate out/fam/*.ibf
    python3 fonttool.py dump out/fam/literata-18-r.ibf --sample "Șa Va"
    python3 fonttool.py sheet out/fam/*-18-r.ibf -o sheet.png          # specimen sheet, drawn as the device draws
    python3 -m unittest test_fonttool                                  # 23 tests, ~4 s

`build` options: `--render mono|gray` (gray = antialiased and thresholded at `--threshold`, smoother for thin serifs), `--axes wght=700`, `--opsz` (optical size from the pixel size at 130 dpi), `--ranges`, `--no-kern`.

## What a build does

1. Renders every codepoint in the ranges (Latin, Romanian, Cyrillic, punctuation; the same list the converter checks books against) through HarfBuzz, so the bitmap and the advance come from the same glyph. Literata swaps glyphs by optical size, and FreeType alone doesn't, which once left a gap after every italic "w".
2. Codepoints the font lacks get a stand-in (U+2011 → '-', an accented letter → its base letter) or are left out and reported.
3. Kerning: the font's own pair adjustments, measured through HarfBuzz. Accented letters kern as their base letter (`kern_as`), and only pairs where that would be wrong are stored as exceptions. This cut Literata from 7,596 pairs to 3,844.
4. Writes and re-reads the file through the reference codec `ibf.py`, which checks every rule in the spec.

## Candidates (body text, regular face)

| Family | Render | 16 px | 18 px | 20 px | Lines per page at 18 px |
|---|---|---|---|---|---|
| Literata | gray, opsz | 35.7 KB | 38.2 KB | 41.2 KB | 12 |
| PT Serif | mono | 28.3 KB | 30.5 KB | 32.1 KB | 14 |
| Noto Serif | mono | 19.1 KB | 20.7 KB | 22.7 KB | 13 |

The same pixel size isn't the same visual size: Literata 16 is about as big as Noto Serif 18. Compare in the simulator: `sim compare BOOK.ibv -o cmp.png out/fam/literata-16 out/fam/ptserif-18 …`.
