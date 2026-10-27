"""IBF bitmap font format v0.1: writer, reader, validator, dumper.

The spec is spec/font-format.md. This module is its reference implementation;
the device reader is firmware/components/core/font.c.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field

MAGIC = b"\x89IBF\r\n\x1a\n"
VERSION = (0, 1)
HEADER = struct.Struct("<8sBBHBBBBHHHHHHIIIIIII32sII")
HEADER_SIZE = 96
RANGE = struct.Struct("<IHH")
GLYPH = struct.Struct("<IHBBbbH")
KERN = struct.Struct("<HHh")
UNIT = 64                     # 1/64 px
BOLD, ITALIC = 1, 2
RENDER_MONO, RENDER_GRAY = 0, 1
REQUIRED = (0x20, 0x2D, 0xFFFD)
assert HEADER.size == HEADER_SIZE and RANGE.size == 8 and GLYPH.size == 12 and KERN.size == 6


class FormatError(ValueError):
    pass


@dataclass
class Glyph:
    cp: int
    advance: int              # u (1/64 px)
    width: int = 0
    height: int = 0
    x_off: int = 0
    y_off: int = 0
    rows: list[bytes] = field(default_factory=list)   # height rows of ceil(width/8) bytes, 1 = ink
    kern_as: int | None = None    # codepoint whose kerning pairs this glyph uses (None: its own)

    def bitmap(self) -> bytes:
        return b"".join(self.rows)


@dataclass
class Font:
    name: str
    pixel_size: int
    line_height: int
    ascent: int
    descent: int
    x_height: int
    glyphs: list[Glyph]                       # any order; written sorted by codepoint
    kerns: dict[tuple[int, int], int] = field(default_factory=dict)   # (cp, cp) -> u
    style: int = 0
    render: int = RENDER_MONO
    threshold: int = 0

    def glyph(self, cp: int) -> Glyph | None:
        for g in self.glyphs:
            if g.cp == cp:
                return g
        return None


def _align(n: int, a: int = 4) -> int:
    return (n + a - 1) // a * a


def _ranges(cps: list[int]) -> list[tuple[int, int, int]]:
    out: list[list[int]] = []
    for i, cp in enumerate(cps):
        if out and cp == out[-1][0] + out[-1][1] and out[-1][1] < 0xFFFF:
            out[-1][1] += 1
        else:
            out.append([cp, 1, i])
    return [tuple(r) for r in out]


def write(font: Font) -> bytes:
    glyphs = sorted(font.glyphs, key=lambda g: g.cp)
    cps = [g.cp for g in glyphs]
    if len(set(cps)) != len(cps):
        raise FormatError("duplicate codepoints")
    if len(glyphs) > 0xFFFF:
        raise FormatError("more than 65535 glyphs")
    for cp in REQUIRED:
        if cp not in cps:
            raise FormatError(f"required glyph U+{cp:04X} missing")
    index = {cp: i for i, cp in enumerate(cps)}
    by_cp = {g.cp: g for g in glyphs}
    ranges = _ranges(cps)

    bitmaps, offsets, seen = bytearray(), [], {}
    for g in glyphs:
        bm = g.bitmap()
        if len(bm) != g.height * ((g.width + 7) // 8):
            raise FormatError(f"U+{g.cp:04X}: bitmap size doesn't match its box")
        if bm not in seen:                 # identical bitmaps are stored once
            seen[bm] = len(bitmaps)
            bitmaps += bm
        offsets.append(seen[bm])

    kern_as = []
    for g in glyphs:
        k = g.cp if g.kern_as is None else g.kern_as
        if k not in index or (k != g.cp and by_cp[k].kern_as not in (None, k)):
            raise FormatError(f"U+{g.cp:04X}: kern_as U+{k:04X} missing or not a kerning glyph")
        kern_as.append(index[k])
    # A pair may be 0: an exception that cancels the pair a glyph would borrow.
    kerns = sorted((index[a], index[b], v) for (a, b), v in font.kerns.items()
                   if a in index and b in index)

    ranges_off = HEADER_SIZE
    glyphs_off = _align(ranges_off + RANGE.size * len(ranges))
    kerns_off = _align(glyphs_off + GLYPH.size * len(glyphs))
    bitmaps_off = _align(kerns_off + KERN.size * len(kerns))
    body = bytearray(bitmaps_off - HEADER_SIZE)
    for i, r in enumerate(ranges):
        RANGE.pack_into(body, ranges_off - HEADER_SIZE + i * RANGE.size, *r)
    for i, (g, off) in enumerate(zip(glyphs, offsets, strict=True)):
        for name, v, lo, hi in (("advance", g.advance, 0, 0xFFFF), ("width", g.width, 0, 255),
                                ("height", g.height, 0, 255), ("x_off", g.x_off, -128, 127),
                                ("y_off", g.y_off, -128, 127)):
            if not lo <= v <= hi:
                raise FormatError(f"U+{g.cp:04X}: {name} {v} out of range")
        GLYPH.pack_into(body, glyphs_off - HEADER_SIZE + i * GLYPH.size,
                        off, g.advance, g.width, g.height, g.x_off, g.y_off, kern_as[i])
    for i, (a, b, v) in enumerate(kerns):
        if not -0x8000 <= v <= 0x7FFF:
            raise FormatError("kerning value out of range")
        KERN.pack_into(body, kerns_off - HEADER_SIZE + i * KERN.size, a, b, v)
    body += bitmaps

    name = font.name.encode("utf-8")[:32]
    head = HEADER.pack(MAGIC, *VERSION, HEADER_SIZE, 1, font.style, font.render, font.threshold,
                       font.pixel_size, font.line_height, font.ascent, font.descent, font.x_height,
                       len(ranges), len(glyphs), len(kerns),
                       ranges_off, glyphs_off, kerns_off, bitmaps_off, len(bitmaps),
                       name.ljust(32, b"\0"), zlib.crc32(body), 0)
    head = head[:92] + struct.pack("<I", zlib.crc32(head[:92]))
    return head + bytes(body)


@dataclass
class Parsed:
    font: Font
    ranges: list[tuple[int, int, int]]
    shared_bitmaps: int
    size: int


def parse(data: bytes) -> Parsed:
    """Reads and fully checks a font file (all validator rules)."""
    if len(data) < HEADER_SIZE:
        raise FormatError("file shorter than the header")
    (magic, vmaj, vmin, hsize, bpp, style, render, threshold, pixel_size, line_height, ascent,
     descent, x_height, nranges, nglyphs, nkerns, ranges_off, glyphs_off, kerns_off, bitmaps_off,
     bitmaps_len, name, data_crc, header_crc) = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise FormatError("not an IBF file (bad magic)")
    if zlib.crc32(data[:92]) != header_crc:
        raise FormatError("header CRC mismatch")
    if vmaj != VERSION[0]:
        raise FormatError(f"unsupported major version {vmaj}")
    if hsize != HEADER_SIZE:
        raise FormatError(f"header_size {hsize}, expected {HEADER_SIZE}")
    if bpp != 1:
        raise FormatError(f"bpp {bpp} not supported")
    if zlib.crc32(data[hsize:]) != data_crc:
        raise FormatError("data CRC mismatch")
    if nglyphs > 0xFFFF:
        raise FormatError("glyph_count over 65535")
    tables = [("ranges", ranges_off, RANGE.size * nranges), ("glyphs", glyphs_off, GLYPH.size * nglyphs),
              ("kerns", kerns_off, KERN.size * nkerns), ("bitmaps", bitmaps_off, bitmaps_len)]
    end = hsize
    for tname, off, length in tables:
        if tname != "bitmaps" and off % 4:
            raise FormatError(f"{tname} offset {off} not aligned")
        if off < end or off + length > len(data):
            raise FormatError(f"{tname} table overlaps or lies outside the file")
        end = off + length
    if bitmaps_off + bitmaps_len != len(data):
        raise FormatError("bitmaps don't end at the end of the file")

    ranges = [RANGE.unpack_from(data, ranges_off + i * RANGE.size) for i in range(nranges)]
    cps, prev_end = [], -1
    for first, count, first_glyph in ranges:
        if count < 1 or first <= prev_end or first + count - 1 > 0x10FFFF:
            raise FormatError(f"range U+{first:04X} unsorted, overlapping or empty")
        if first_glyph != len(cps):
            raise FormatError(f"range U+{first:04X}: first_glyph {first_glyph}, expected {len(cps)}")
        cps.extend(range(first, first + count))
        prev_end = first + count - 1
    if len(cps) != nglyphs:
        raise FormatError(f"ranges cover {len(cps)} glyphs, glyph_count is {nglyphs}")

    glyphs, used, kern_as = [], set(), []
    for i, cp in enumerate(cps):
        off, adv, w, h, xo, yo, ka = GLYPH.unpack_from(data, glyphs_off + i * GLYPH.size)
        if ka >= nglyphs:
            raise FormatError(f"U+{cp:04X}: kern_as {ka} out of range")
        kern_as.append(ka)
        stride = (w + 7) // 8
        if off + stride * h > bitmaps_len:
            raise FormatError(f"U+{cp:04X}: bitmap outside the bitmap area")
        start = bitmaps_off + off
        rows = [bytes(data[start + r * stride:start + (r + 1) * stride]) for r in range(h)]
        if stride * h:
            used.add(off)
        glyphs.append(Glyph(cp, adv, w, h, xo, yo, rows, None if ka == i else cps[ka]))
    for i, ka in enumerate(kern_as):
        if kern_as[ka] != ka:
            raise FormatError(f"U+{cps[i]:04X}: kern_as points at a glyph that kerns as another")
    for cp in REQUIRED:
        if cp not in set(cps):
            raise FormatError(f"required glyph U+{cp:04X} missing")

    kerns, prev = {}, (-1, -1)
    for i in range(nkerns):
        a, b, v = KERN.unpack_from(data, kerns_off + i * KERN.size)
        if (a, b) <= prev:
            raise FormatError(f"kerning pair {i} unsorted or duplicate")
        if a >= nglyphs or b >= nglyphs:
            raise FormatError(f"kerning pair {i} out of range")
        prev = (a, b)
        kerns[(cps[a], cps[b])] = v

    font = Font(name.rstrip(b"\0").decode("utf-8", "replace"), pixel_size, line_height, ascent, descent,
                x_height, glyphs, kerns, style, render, threshold)
    shared = sum(1 for g in glyphs if g.width and g.height) - len(used)
    return Parsed(font, ranges, shared, len(data))


def kerning(font: Font, left: int, right: int) -> int:
    """The kerning between two codepoints, as the device looks it up: the
    glyphs' own pair if there is one, else the pair of the glyphs they kern as."""
    if (left, right) in font.kerns:
        return font.kerns[(left, right)]
    gl, gr = font.glyph(left), font.glyph(right)
    a = gl.kern_as if gl and gl.kern_as is not None else left
    b = gr.kern_as if gr and gr.kern_as is not None else right
    return font.kerns.get((a, b), 0)


def validate(data: bytes) -> Parsed:
    return parse(data)


def dump(p: Parsed, sample: str = "") -> str:
    f = p.font
    style = {0: "regular", BOLD: "bold", ITALIC: "italic", BOLD | ITALIC: "bold italic"}[f.style & 3]
    render = "hinted mono" if f.render == RENDER_MONO else f"grey, threshold {f.threshold}/255"
    bm = sum(len(g.bitmap()) for g in f.glyphs)
    borrowed = sum(1 for g in f.glyphs if g.kern_as is not None)
    lines = [f"{f.name} {style}, {f.pixel_size} px ({render}), {p.size:,} B",
             f"  line height {f.line_height}, ascent {f.ascent}, descent {f.descent}, x-height {f.x_height}",
             f"  {len(f.glyphs)} glyphs in {len(p.ranges)} ranges, {len(f.kerns)} kerning pairs "
             f"({borrowed} glyphs kern as another), "
             f"{bm:,} B of bitmaps ({p.shared_bitmaps} shared)"]
    for first, count, fg in p.ranges:
        lines.append(f"    U+{first:04X}–U+{first + count - 1:04X}  {count:4}  from glyph {fg}")
    for ch in sample:
        g = f.glyph(ord(ch))
        if not g:
            lines.append(f"  {ch!r}: missing")
            continue
        lines.append(f"  {ch!r} U+{g.cp:04X}: advance {g.advance / UNIT:.3f} px, box {g.width}×{g.height} "
                     f"at ({g.x_off}, {g.y_off})" + (f", kerns as U+{g.kern_as:04X}" if g.kern_as else ""))
        for r in g.rows:
            bits = "".join("#" if r[x // 8] & (0x80 >> (x % 8)) else "." for x in range(g.width))
            lines.append("      " + bits)
    return "\n".join(lines)
