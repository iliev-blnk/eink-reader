# IBF font format, v0.1

Status: **draft for review.** Written 2026-10-26.
Reference implementation: `tools/fonttool/ibf.py` (writer, reader, validator). Font builder: `tools/fonttool/fonttool.py`. Tests: `tools/fonttool/test_fonttool.py`. Device reader: `firmware/components/core/font.c` (`font_open`).

**Name.** IBF, *Ink Bitmap Font*, the companion of IBV. Extension `.ibf`.

When this document and `ibf.py` disagree, one of them has a bug. Fix it, then add a line to the changelog at the end.

---

## 1. What the format is for

The device draws text from **pre-rendered bitmaps**. It has no TrueType rasterizer: FreeType would cost about 300 KB of flash and several milliseconds per glyph, and on a 1-bit screen the result has to be thresholded anyway. The PC does the rendering once, with full hinting or careful thresholding, and writes one file per face (regular, bold, italic, bold italic) and pixel size.

What the device needs from a font:

1. **Find a glyph fast.** It looks up every character on the page, so lookup has to be a few comparisons.
2. **Measure exactly.** Line breaking and justification add up advance widths. Integer pixel advances spread rounding errors across a line, so advances and kerning are stored in **1/64 px**, the same 26.6 fixed point FreeType uses.
3. **Use the file in place.** The tables are laid out so that a C struct pointer can point straight into the loaded file (or into a memory-mapped flash partition). Opening a font is a header check, never a parse.
4. **Stay small.** The default character set (about 435 glyphs) at 18 px takes 20–38 KB per face, so all four faces of the body font fit in RAM together.

## 2. Conventions

- Little-endian, like IBV. `u8/u16/u32` unsigned, `i8/i16` two's complement.
- All offsets are absolute from the start of the file.
- Tables start at offsets that are multiples of 4. Every record has natural alignment (a `u32` at a multiple of 4, a `u16` at a multiple of 2), so on the ESP32 and on the PC the C structs in `font.h` have exactly the sizes and layouts given here, without `packed`. `font.h` checks the sizes with `_Static_assert`.
- `CRC32` is the zlib/PNG CRC, as in IBV.
- Units: **px** are screen pixels. **u** is 1/64 px (so 64 u = 1 px).
- y grows downwards, as on the screen. The baseline is y = 0. Negative y is above the baseline.

## 3. File layout

```
offset 0   ┌────────────────────────────────┐
           │ Header                  96 B   │  magic, version, metrics, table offsets, CRCs
       96  ├────────────────────────────────┤
           │ Ranges            8 B × ranges │  codepoint ranges → glyph indices
           │ Glyphs           12 B × glyphs │  bitmap offset, box, advance
           │ Kerning pairs     6 B × pairs  │  optional (count may be 0)
           │ Bitmaps                        │  1 bpp, rows padded to bytes
           └────────────────────────────────┘
```

The writer uses this order. Readers find the tables only through the offsets in the header.

### 3.1 Header (96 bytes)

| Offset | Size | Field | Value / meaning |
|---:|---:|---|---|
| 0 | 8 | `magic` | `89 49 42 46 0D 0A 1A 0A` = `\x89IBF\r\n\x1a\n` (same idea as IBV's) |
| 8 | 1 | `version_major` | 0 |
| 9 | 1 | `version_minor` | 1 |
| 10 | 2 | `header_size` | 96 |
| 12 | 1 | `bpp` | 1. (2 is reserved for 4-level grey; v0.1 readers refuse it.) |
| 13 | 1 | `style` | bit 0 bold, bit 1 italic: the same bits as IBV style runs |
| 14 | 1 | `render` | how the PC made the bitmaps, for the record: 0 = hinted monochrome, 1 = greyscale + threshold. Readers ignore it. |
| 15 | 1 | `threshold` | for `render` = 1: ink where coverage ≥ threshold/255. Else 0. |
| 16 | 2 | `pixel_size` | em size in px |
| 18 | 2 | `line_height` | recommended baseline-to-baseline distance, px (ascent + descent + line gap of the source font) |
| 20 | 2 | `ascent` | px above the baseline |
| 22 | 2 | `descent` | px below the baseline |
| 24 | 2 | `x_height` | px; used to centre ornaments and rules on the text |
| 26 | 2 | `range_count` | |
| 28 | 4 | `glyph_count` | ≤ 65535 |
| 32 | 4 | `kern_count` | may be 0 |
| 36 | 4 | `ranges_offset` | multiple of 4 |
| 40 | 4 | `glyphs_offset` | multiple of 4 |
| 44 | 4 | `kerns_offset` | multiple of 4 (also when `kern_count` = 0) |
| 48 | 4 | `bitmaps_offset` | |
| 52 | 4 | `bitmaps_length` | bytes; the bitmaps end the file |
| 56 | 32 | `name` | UTF-8 family name, NUL-padded, e.g. `PT Serif` |
| 88 | 4 | `data_crc` | CRC32 of every byte from `header_size` to the end of the file |
| 92 | 4 | `header_crc` | CRC32 of bytes 0–91 |

### 3.2 Ranges (8 bytes each)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `first_cp`: first codepoint of the range |
| 4 | 2 | `count`: codepoints in the range, ≥ 1 |
| 6 | 2 | `first_glyph`: glyph index of `first_cp` |

Ranges are sorted by `first_cp` and don't overlap. Codepoint *c* in range *r* has glyph index `first_glyph + (c − first_cp)`. Glyphs are numbered in codepoint order, so `first_glyph` of each range is the sum of the counts before it.

Lookup is a binary search over the ranges, then one addition. The default character set (section 5) is 12 ranges when the font has every character (PT Serif, which lacks some rare letters, has 17), so that's at most 4–5 comparisons, against 9 for a binary search over 435 glyphs, and the table needs no codepoint per glyph.

### 3.3 Glyphs (12 bytes each)

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `bitmap` | offset of the bitmap from `bitmaps_offset` |
| 4 | 2 | `advance` | pen advance, **u** (1/64 px) |
| 6 | 1 | `width` | bitmap box width, px (0 for blank glyphs such as the space) |
| 7 | 1 | `height` | bitmap box height, px |
| 8 | 1 | `x_off` (i8) | left edge of the box relative to the pen, px |
| 9 | 1 | `y_off` (i8) | top edge of the box relative to the baseline, px (negative = above) |
| 10 | 2 | `kern_as` | index of the glyph whose kerning pairs this glyph uses; its own index if it has its own (section 3.4) |

A glyph's bitmap is `height` rows of `ceil(width / 8)` bytes, MSB = leftmost pixel, **bit 1 = ink**. That is the opposite of the framebuffer (bit 1 = white): a set bit means "clear this framebuffer bit", so drawing is an AND-NOT per byte.

A codepoint inside a range that the source font doesn't have isn't stored as a glyph. The writer splits the range around it.

### 3.4 Kerning pairs (6 bytes each)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `left`: glyph index |
| 2 | 2 | `right`: glyph index |
| 4 | 2 | `value` (i16): added to the left glyph's advance when `right` follows it, **u**. May be 0. |

Sorted by (`left`, `right`), no duplicates. Kerning applies only between neighbouring glyphs of the **same face**. A style change in the middle of a word gets no kerning.

**Borrowed kerning.** An accented letter usually kerns like its base letter: *Àv* like *Av*, *Ță* like *Ta*. Storing those pairs again for every accented letter multiplied the table by 3 (PT Serif at 18 px: 7,596 pairs instead of 2,344). So a glyph's `kern_as` names the glyph whose pairs it uses (À → A, ă → a, Й → И). Every `kern_as` points at a glyph that kerns as itself, so there is never more than one step.

Borrowing is not always right: fonts kern *Vă* less than *Va*, so that the breve clears the V. Where the real value differs from the borrowed one by ¼ px or more, the builder stores the pair under the glyphs' own indices (an **exception**, value 0 allowed). The lookup for glyphs *a*, *b* is:

1. the pair (*a*, *b*), if present;
2. else, if `kern_as[a]` ≠ *a* or `kern_as[b]` ≠ *b*, the pair (`kern_as[a]`, `kern_as[b]`), if present;
3. else 0.

Each step is a binary search, about 12 comparisons for 3,000 pairs. The builder keeps a pair only if |value| ≥ 16 u (¼ px) at that size; smaller values don't move a 1-bit glyph often enough to matter. With exceptions, every pair the builder measures comes out within ¼ px of the font's own kerning. PT Serif 18 px: 2,344 pairs + 604 exceptions.

Pairs are measured within one script plus punctuation: Latin with Latin, Cyrillic with Cyrillic. Mixed-script pairs don't occur inside words.

### 3.5 Bitmaps

Concatenated glyph bitmaps. Glyphs with identical bitmaps (for example U+00A0 and the space, or Latin A and Cyrillic А) may share one.

### 3.6 Stand-ins

When the source font lacks a character of the default set, the builder stores a **stand-in**: a copy of a similar glyph's metrics, sharing its bitmap. U+2010 HYPHEN becomes `-`, U+2012 FIGURE DASH becomes `–`, U+201B becomes `’`, and an accented letter becomes its base letter (PT Serif has no *Ċ*, so it gets *C*). The builder lists the stand-ins. A character with no stand-in is left out, and the device draws U+FFFD for it.

## 4. How the device uses a font

**Open.** Read (or map) the file and check the magic, the version, `bpp` = 1 and both CRCs. Check every table lies inside the file and every offset is aligned. Then set pointers into the file. That's the whole of `font_open()`. Nothing is copied.

**Draw a glyph.** Find it (section 3.2). If it's missing, use U+FFFD, which every font must have. Copy its bitmap at (`round(pen) + x_off`, `baseline + y_off`). Then pen += `advance` + the kerning with the next glyph.

**Pen arithmetic.** The pen is kept in u. Each glyph is drawn at the pen rounded to the nearest pixel, so rounding errors don't add up along the line. Justified lines add their extra space in u too.

**Faces.** On the card, fonts live in `/fonts/` as `<family>-<size>-<style>.ibf`. Style is `r`, `b`, `i` or `bi`, for example `ptserif-18-bi.ibf`. A reader setting picks a family and a size. A missing bold or italic face falls back to the regular face (bold is then drawn twice, 1 px apart).

**RAM.** One face at 18 px with the default character set is 20–38 KB including kerning (section 7). The four body faces plus a heading face come to about 150–190 KB, out of the S3's 512 KB SRAM. If that turns out too much next to the book buffers, the files can go in a flash partition and be memory-mapped instead, with no format change.

## 5. Character set

`tools/fonttool/fonttool.py` `DEFAULT_RANGES`. The converter (`glyphs.py`) reads the same list, so a book never needs a character the font lacks.

- Basic Latin, Latin-1, Latin Extended-A
- Romanian ș ț with comma below (U+0218–U+021B)
- Cyrillic U+0400–U+045F (Russian, Ukrainian, Belarusian, Bulgarian, Serbian)
- typographic punctuation: dashes, quotes, bullet, ellipsis, guillemets, euro sign
- U+FFFD, the replacement character: **required**. The space (U+0020) and the hyphen (U+002D) are required too.

The device draws U+00A0 (no-break space) with the space's width, and draws U+002D at a soft-hyphen break. It never looks up U+00AD, U+200B or U+2028, which are layout controls.

## 6. Validator rules

`fonttool.py validate` (in `ibf.py`) checks:

1. Magic, major version, `header_size`, `bpp` = 1, `header_crc`, `data_crc`.
2. Every table lies inside the file, at an aligned offset, and the tables don't overlap. The bitmaps end exactly at the end of the file.
3. Ranges are sorted and non-overlapping, with count ≥ 1. `first_glyph` values are consecutive, and they add up to `glyph_count`.
4. Every glyph's bitmap lies inside the bitmap area. Every `kern_as` is < `glyph_count` and points at a glyph whose `kern_as` is itself.
5. Kerning pairs are sorted, have no duplicates, and both indices are < `glyph_count`.
6. U+0020, U+002D and U+FFFD are present.

## 7. Design choices, and the alternatives turned down

| Choice | Alternative | Why |
|---|---|---|
| Pre-rendered bitmaps | TrueType + FreeType on the device | No rasterizer in flash, constant draw time, and the 1-bit rendering is tuned once on the PC (hinted mono vs threshold, chosen per font). Cost: one file per size, so sizes are fixed (3+ is the requirement). |
| One file per face and size | One file with every size and style | The device loads only what the reader setting needs. Files stay small enough to load whole. |
| Advances and kerning in 1/64 px | Whole pixels | A justified 30-character line with whole-pixel advances is off by up to ±15 px in total, which is visible as uneven word spacing. In 26.6 fixed point the error stays under 1 px per glyph and doesn't accumulate. |
| Range table | A codepoint in every glyph record (as `font_ui16` had in v0) | 4 bytes less per glyph and fewer comparisons per lookup. |
| Pair kerning table with `kern_as` | No kerning, a full pair table, or class-based kerning (as in OpenType) | Pairs are the simplest to look up. A full table repeats every base letter's pairs for its accented forms (45 KB for PT Serif at 18 px). OpenType classes are smaller but need a class table per side. `kern_as` is a one-sided class that costs no extra bytes (it uses the glyph record's spare field), with exception pairs where it's wrong. With the ¼ px cut-off a face has 1,000–4,000 pairs (6–23 KB). |
| Rows padded to whole bytes | One continuous bitstream per glyph | Drawing works on whole bytes. The padding costs about 28 % of the bitmap bytes (about 2.5 KB per face at 18 px), which is small next to the kerning table. |
| In-place structs, 4-byte aligned | A compact encoding parsed at load time | Opening a font costs nothing, and the same file works from RAM, SD or a memory-mapped flash partition. |

## 8. Versioning

The same as IBV: a reader refuses a major version it doesn't know. During 0.x anything may change. The format freezes as 1.0 together with IBV (SPEC-4).

## Changelog

- **0.1** (2026-10-26) First draft, with `ibf.py`, `fonttool.py build` and the device reader. Borrowed kerning (`kern_as` in the glyph record's spare field, plus exception pairs) and stand-ins added the same day, after the first PT Serif build had 7,596 pairs.
