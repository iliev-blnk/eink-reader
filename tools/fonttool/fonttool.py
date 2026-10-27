#!/usr/bin/env python3
"""fonttool: TTF/OTF -> IBF bitmap fonts for the e-reader (CONV-8).

    fonttool.py build --ttf FONT.ttf --size 18 [--style b] [--axes wght=700]
                      [--render mono|gray] [--threshold 128] [--name "PT Serif"] -o out.ibf
    fonttool.py family ptserif --sizes 16,18,20 -o DIR     # all four faces per size, from FAMILIES
    fonttool.py c --ttf FONT.ttf --size 16 --name font_ui16 --out font_ui16.c   # compiled-in font
    fonttool.py validate FILE.ibf [...]
    fonttool.py dump FILE.ibf [--sample "Șa"]
    fonttool.py sheet FILE.ibf [...] -o sheet.png [--text "..."] [--width 240]

The format is spec/font-format.md, implemented by ibf.py.

Bitmaps come from FreeType through Pillow, one of two ways:
  mono  FreeType's hinted monochrome rendering. Best for fonts with good
        TrueType hinting (PT Serif, Noto Serif).
  gray  antialiased rendering, then ink where coverage >= threshold. Better
        for unhinted fonts, such as variable fonts (Literata).
Advances and kerning are the font's exact (unhinted) values in 1/64 px, from
HarfBuzz (Pillow's raqm layout) with ligatures off.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont

import ibf

# Latin basic + Latin-1 + Latin Extended-A (ă â î ş ţ ...), Romanian comma-below
# ș ț (U+0218–U+021B), Cyrillic (Russian + Ukrainian), typographic punctuation.
DEFAULT_RANGES = (
    "0x20-0x7E,0xA0-0xFF,0x100-0x17F,0x218-0x21B,0x400-0x45F,"
    "0x2010-0x2014,0x2018-0x201E,0x2022,0x2026,0x2039-0x203A,0x20AC,0xFFFD"
)
# Not drawn from the font: layout controls, and U+00AD (the layout draws '-').
SKIP = {0xAD}
# Missing punctuation drawn as a look-alike. Missing accented letters use their base letter.
STAND_INS = {0x2010: 0x2D, 0x2011: 0x2D, 0x2012: 0x2013, 0x201B: 0x2019, 0x201F: 0x201D}
KERN_MIN = 16                 # u; smaller pairs are dropped (spec 3.4)
SCREEN_DPI = 130              # 3.7" panel, 240 x 416: sets Literata's optical size
HERE = pathlib.Path(__file__).resolve().parent
STYLES = {"r": 0, "b": ibf.BOLD, "i": ibf.ITALIC, "bi": ibf.BOLD | ibf.ITALIC}

# Candidate book fonts (fetch-fonts.sh downloads them). Paths are under fonts/.
FAMILIES = {
    "literata": {
        "name": "Literata", "render": "gray", "threshold": 128, "opsz": True,
        "faces": {"r": ("literata/Literata[opsz,wght].ttf", {"wght": 400}),
                  "b": ("literata/Literata[opsz,wght].ttf", {"wght": 700}),
                  "i": ("literata/Literata-Italic[opsz,wght].ttf", {"wght": 400}),
                  "bi": ("literata/Literata-Italic[opsz,wght].ttf", {"wght": 700})},
    },
    "ptserif": {
        "name": "PT Serif", "render": "mono",
        "faces": {s: (f"ptserif/PT_Serif-Web-{n}.ttf", {}) for s, n in
                  (("r", "Regular"), ("b", "Bold"), ("i", "Italic"), ("bi", "BoldItalic"))},
    },
    "notoserif": {
        "name": "Noto Serif", "render": "mono",
        "faces": {s: (f"notoserif/NotoSerif-{n}.ttf", {}) for s, n in
                  (("r", "Regular"), ("b", "Bold"), ("i", "Italic"), ("bi", "BoldItalic"))},
    },
    # Latin only: a Cyrillic book in Libron gets U+FFFD boxes.
    "libron": {
        "name": "Libron", "render": "gray", "threshold": 128,
        "faces": {s: (f"libron/Libron_R-{n}.ttf", {}) for s, n in
                  (("r", "Regular"), ("b", "Bold"), ("i", "Italic"), ("bi", "BoldItalic"))},
    },
}


def parse_ranges(spec: str) -> list[int]:
    cps = set()
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        lo, _, hi = part.partition("-")
        lo = int(lo, 0)
        hi = int(hi, 0) if hi else lo
        cps.update(range(lo, hi + 1))
    return sorted(cps)


def open_font(path, size: int, axes: dict[str, float] | None = None, opsz: bool = False):
    """The font at `size` px with its variation axes set. Bitmaps and advances
    both go through HarfBuzz (RAQM): fonts like Literata swap glyphs by optical
    size (feature variations), which FreeType alone doesn't apply, so the
    bitmaps and advances would come from different glyphs."""
    f = ImageFont.truetype(str(path), size, layout_engine=ImageFont.Layout.RAQM)
    if axes or opsz:
        names = [a["name"].decode() if isinstance(a["name"], bytes) else a["name"]
                 for a in f.get_variation_axes()]
        values = [a["default"] for a in f.get_variation_axes()]
        want = dict(axes or {})
        if opsz:
            want.setdefault("opsz", round(size * 72 / SCREEN_DPI, 1))
        for i, n in enumerate(names):
            key = {"Weight": "wght", "Optical size": "opsz", "Width": "wdth"}.get(n, n)
            if key in want:
                lo, hi = f.get_variation_axes()[i]["minimum"], f.get_variation_axes()[i]["maximum"]
                values[i] = min(max(want[key], lo), hi)
        f.set_variation_by_axes(values)
    return f


def render(font, ch: str, mode: str, threshold: int):
    """Returns (rows, w, h, x_off, y_off): the glyph's ink box, 1 = ink."""
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls", mode="1" if mode == "mono" else "L")
    x0, y0, x1, y1 = x0 - 1, y0 - 1, x1 + 1, y1 + 1        # room for antialiasing spill
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return [], 0, 0, 0, 0
    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    if mode == "mono":
        draw.fontmode = "1"
    draw.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    img = img.point(lambda v: 255 if v >= (threshold if mode == "gray" else 128) else 0).convert("1")
    box = img.getbbox()
    if box is None:
        return [], 0, 0, 0, 0
    img = img.crop(box)
    w, h = img.size
    px = img.load()
    rows = []
    for y in range(h):
        row = bytearray((w + 7) // 8)
        for x in range(w):
            if px[x, y]:
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(bytes(row))
    return rows, w, h, x0 + box[0], y0 + box[1]


def measure(f, text: str) -> int:
    return round(f.getlength(text, features=["-liga", "-clig", "-dlig"]) * ibf.UNIT)


def _kern_set(cps: list[int]) -> list[list[int]]:
    """Groups of codepoints worth kerning against each other: one script's
    letters plus all punctuation. Latin–Cyrillic pairs don't occur in text."""
    punct = [c for c in cps if unicodedata.category(chr(c))[0] in "PS" and c < 0x2100]
    latin = [c for c in cps if unicodedata.category(chr(c))[0] == "L" and c < 0x250]
    cyr = [c for c in cps if unicodedata.category(chr(c))[0] == "L" and 0x400 <= c < 0x500]
    return [latin + punct, cyr + punct]


def build(path, size: int, *, style: int = 0, axes=None, opsz=False, mode="mono", threshold=128,
          name: str | None = None, ranges: str = DEFAULT_RANGES, kern: bool = True,
          log=print) -> ibf.Font:
    font = open_font(path, size, axes, opsz)
    notdef = render(font, chr(0x10FFFD), mode, threshold)   # a codepoint no font has
    glyphs, missing = [], []
    for cp in parse_ranges(ranges):
        if cp in SKIP:
            continue
        rows, w, h, xo, yo = render(font, chr(cp), mode, threshold)
        if cp != 0xFFFD and rows and (rows, w, h) == notdef[:3]:
            missing.append(cp)
            continue
        glyphs.append(ibf.Glyph(cp, measure(font, chr(cp)), w, h, xo, yo, rows))
    have = {g.cp: g for g in glyphs}
    substituted = []
    for cp in missing[:]:                 # stand-ins: the same glyph (U+2010 -> '-') or the base letter
        sub = STAND_INS.get(cp) or ord(unicodedata.normalize("NFD", chr(cp))[0])
        if sub != cp and sub in have:
            s = have[sub]
            have[cp] = ibf.Glyph(cp, s.advance, s.width, s.height, s.x_off, s.y_off, s.rows, sub)
            glyphs.append(have[cp])
            missing.remove(cp)
            substituted.append(cp)
    if 0xFFFD in have and not have[0xFFFD].rows:
        missing.append(0xFFFD)
    if 0xFFFD not in have or not have[0xFFFD].rows:     # the font has no U+FFFD: draw a box
        adv = have[0x20].advance * 2 if 0x20 in have else size * ibf.UNIT // 2
        w, h = max(adv // ibf.UNIT - 2, 3), max(size * 7 // 10, 4)
        have[0xFFFD] = ibf.Glyph(0xFFFD, adv, w, h, 1, -h, _box(w, h))
        glyphs = [g for g in glyphs if g.cp != 0xFFFD] + [have[0xFFFD]]

    kerns: dict[tuple[int, int], int] = {}
    if kern:
        for g in glyphs:                  # accented letters kern like their base letter
            base = ord(unicodedata.normalize("NFD", chr(g.cp))[0])
            if g.kern_as is None and base != g.cp and base in have:
                g.kern_as = base
        for g in glyphs:                  # point straight at the glyph that has the pairs
            while g.kern_as is not None and have[g.kern_as].kern_as is not None:
                g.kern_as = have[g.kern_as].kern_as
        ka = {g.cp: g.cp if g.kern_as is None else g.kern_as for g in glyphs}
        exact = {}                        # stand-ins aren't in the source font: they only borrow
        for group in _kern_set([cp for cp in have if cp not in substituted]):
            for a in group:
                for b in group:
                    exact[(a, b)] = measure(font, chr(a) + chr(b)) - have[a].advance - have[b].advance
        for (a, b), v in exact.items():           # pairs of glyphs that kern as themselves
            if ka[a] == a and ka[b] == b and abs(v) >= KERN_MIN:
                kerns[(a, b)] = v
        for (a, b), v in exact.items():           # exceptions where borrowing is off by >= 1/4 px
            if (ka[a] != a or ka[b] != b) and abs(v - kerns.get((ka[a], ka[b]), 0)) >= KERN_MIN:
                kerns[(a, b)] = v
    ascent, descent = font.getmetrics()
    xb = font.getbbox("x", anchor="ls")
    fam = name or font.getname()[0]
    font = ibf.Font(fam, size, ascent + descent, ascent, descent, -xb[1], glyphs, kerns, style,
                    ibf.RENDER_GRAY if mode == "gray" else ibf.RENDER_MONO,
                    threshold if mode == "gray" else 0)
    if substituted:
        log(f"  {len(substituted)} not in the source font, drawn as a stand-in: "
            + "".join(chr(cp) for cp in substituted))
    if missing:
        log("  missing in the source font (drawn as U+FFFD): " + " ".join(f"U+{cp:04X}" for cp in missing))
    return font


def _box(w: int, h: int) -> list[bytes]:
    """An outlined rectangle, the stand-in for U+FFFD."""
    stride = (w + 7) // 8
    rows = []
    for y in range(h):
        row = bytearray(stride)
        for x in range(w):
            if y in (0, h - 1) or x in (0, w - 1):
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(bytes(row))
    return rows


def write_c(font: ibf.Font, symbol: str, source: str) -> str:
    """The font as C tables, for fonts compiled into the firmware (the UI font)."""
    data = ibf.write(font)            # also checks the font
    p = ibf.parse(data)
    f = p.font
    cps = [g.cp for g in f.glyphs]
    index = {cp: i for i, cp in enumerate(cps)}
    glyph_off, bitmaps_off = struct.unpack_from("<I", data, 40)[0], struct.unpack_from("<I", data, 48)[0]
    bitmaps = data[bitmaps_off:]
    out = [f"// Generated by tools/fonttool/fonttool.py, do not edit.",
           f"// Source: {source} at {f.pixel_size} px (SIL Open Font License).",
           f"// {len(cps)} glyphs, {len(f.kerns)} kerning pairs, {len(bitmaps)} bytes of bitmaps.",
           f"// Regenerate: python3 tools/fonttool/fonttool.py c --ttf {source} --size {f.pixel_size} "
           f"--name {symbol} --out <this file>",
           "", '#include "font.h"', "",
           f"static const font_range_t ranges[{len(p.ranges)}] = {{"]
    out += [f"    {{0x{a:04x}, {n}, {g}}}," for a, n, g in p.ranges]
    out += ["};", "", f"static const font_glyph_t glyphs[{len(cps)}] = {{"]
    for i, g in enumerate(f.glyphs):
        off, adv, w, h, xo, yo, ka = ibf.GLYPH.unpack_from(data, glyph_off + i * ibf.GLYPH.size)
        out.append(f"    {{{off}, {adv}, {w}, {h}, {xo}, {yo}, {ka}}},  // U+{g.cp:04X}")
    out += ["};", ""]
    kern_off = struct.unpack_from("<I", data, 44)[0]
    kerns = [ibf.KERN.unpack_from(data, kern_off + i * ibf.KERN.size) for i in range(len(f.kerns))]
    if kerns:
        out.append(f"static const font_kern_t kerns[{len(kerns)}] = {{")
        for i in range(0, len(kerns), 6):
            out.append("    " + " ".join(f"{{{a}, {b}, {v}}}," for a, b, v in kerns[i:i + 6]))
        out += ["};", ""]
    out.append(f"static const uint8_t bitmaps[{max(len(bitmaps), 1)}] = {{")
    for i in range(0, len(bitmaps), 16):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in bitmaps[i:i + 16]) + ",")
    out += ["};", "", f"const font_t {symbol} = {{",
            f'    .name = "{f.name}",',
            f"    .style = {f.style},",
            f"    .pixel_size = {f.pixel_size},",
            f"    .line_height = {f.line_height},",
            f"    .ascent = {f.ascent},",
            f"    .descent = {f.descent},",
            f"    .x_height = {f.x_height},",
            f"    .range_count = {len(p.ranges)},",
            f"    .glyph_count = {len(cps)},",
            f"    .kern_count = {len(kerns)},",
            "    .ranges = ranges,",
            "    .glyphs = glyphs,",
            f"    .kerns = {'kerns' if kerns else 'NULL'},",
            "    .bitmaps = bitmaps,",
            f"    .fallback = {index[0xFFFD]},",
            "};", ""]
    return "\n".join(out)


def draw_text(img: Image.Image, font: ibf.Font, x: int, y: int, text: str) -> int:
    """Draws with the font's own bitmaps, advances and kerning, as the device
    does: the pen in 1/64 px, each glyph at the pen rounded. Returns the pen x."""
    by_cp = {g.cp: g for g in font.glyphs}
    pen, prev = x * ibf.UNIT, None
    px = img.load()
    for ch in text:
        g = by_cp.get(ord(ch)) or by_cp[0xFFFD]
        if prev is not None:
            pen += font.kerns.get((prev, g.cp), 0)
        gx = (pen + ibf.UNIT // 2) // ibf.UNIT + g.x_off
        for r, row in enumerate(g.rows):
            for c in range(g.width):
                if row[c // 8] & (0x80 >> (c % 8)):
                    xx, yy = gx + c, y + g.y_off + r
                    if 0 <= xx < img.width and 0 <= yy < img.height:
                        px[xx, yy] = 0
        pen += g.advance
        prev = g.cp
    return pen // ibf.UNIT


SHEET_TEXT = ("Ședința a început târziu; își amintește.\n"
              "Мой дядя самых честных правил, когда\n"
              "The quick brown fox — “jumps” over 1984.\n"
              "AV To Ta „Ce?” «Нет» … ŞŢ→ȘȚ")


def sheet(paths: list[pathlib.Path], out: pathlib.Path, text: str = SHEET_TEXT, width: int = 240,
          scale: int = 3) -> None:
    lab = ImageFont.load_default()
    parts = []
    for path in paths:
        f = ibf.parse(path.read_bytes()).font
        lines = text.split("\n")
        h = 12 + f.line_height * len(lines) + 4
        img = Image.new("L", (width, h), 255)
        ImageDraw.Draw(img).text((2, 0), f"{path.name}  ({f.name} {f.pixel_size}px)", font=lab, fill=0)
        for i, line in enumerate(lines):
            draw_text(img, f, 4, 12 + f.ascent + i * f.line_height, line)
        parts.append(img)
    sheet_img = Image.new("L", (width, sum(p.height for p in parts)), 255)
    y = 0
    for p in parts:
        sheet_img.paste(p, (0, y))
        y += p.height
    sheet_img.resize((width * scale, sheet_img.height * scale), Image.NEAREST).save(out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="fonttool.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    def face_args(p):
        p.add_argument("--ttf", required=True, type=pathlib.Path)
        p.add_argument("--size", type=int, required=True, help="pixel size (em)")
        p.add_argument("--axes", default="", help="variable font axes, e.g. wght=700,opsz=10")
        p.add_argument("--opsz", action="store_true", help="set the optical size axis from the size")
        p.add_argument("--render", choices=("mono", "gray"), default="mono")
        p.add_argument("--threshold", type=int, default=128, help="gray: ink at coverage >= T/255")
        p.add_argument("--ranges", default=DEFAULT_RANGES)
        p.add_argument("--no-kern", action="store_true")
        p.add_argument("--family-name", help="name stored in the file (default: the font's)")
    b = sub.add_parser("build", help="one face at one size -> .ibf")
    face_args(b)
    b.add_argument("--style", choices=STYLES, default="r")
    b.add_argument("-o", "--out", type=pathlib.Path, required=True)
    fam = sub.add_parser("family", help="a family from FAMILIES at several sizes -> DIR/<family>-<size>-<style>.ibf")
    fam.add_argument("family", choices=FAMILIES)
    fam.add_argument("--sizes", default="16,18,20")
    fam.add_argument("--styles", default="r,b,i,bi")
    fam.add_argument("--render", choices=("mono", "gray"), help="override the family's default")
    fam.add_argument("--threshold", type=int)
    fam.add_argument("-o", "--out", type=pathlib.Path, required=True)
    c = sub.add_parser("c", help="one face -> C source, for fonts compiled into the firmware")
    face_args(c)
    c.add_argument("--name", required=True, help="C symbol, e.g. font_ui16")
    c.add_argument("--out", required=True, type=pathlib.Path)
    v = sub.add_parser("validate")
    v.add_argument("files", nargs="+", type=pathlib.Path)
    d = sub.add_parser("dump")
    d.add_argument("file", type=pathlib.Path)
    d.add_argument("--sample", default="")
    s = sub.add_parser("sheet", help="render a specimen with the fonts' own metrics -> PNG")
    s.add_argument("files", nargs="+", type=pathlib.Path)
    s.add_argument("-o", "--out", type=pathlib.Path, required=True)
    s.add_argument("--text", default=SHEET_TEXT, help="\\n separates lines")
    s.add_argument("--width", type=int, default=240)
    a = ap.parse_args(argv)

    def axes(spec):
        return {k: float(v) for k, v in (p.split("=") for p in spec.split(",") if p)}

    if a.cmd in ("build", "c"):
        font = build(a.ttf, a.size, style=STYLES.get(getattr(a, "style", "r"), 0), axes=axes(a.axes),
                     opsz=a.opsz, mode=a.render, threshold=a.threshold, name=a.family_name,
                     ranges=a.ranges, kern=not a.no_kern)
        if a.cmd == "build":
            data = ibf.write(font)
            a.out.write_bytes(data)
            print(ibf.dump(ibf.parse(data)).split("\n    U+")[0])
        else:
            a.out.write_text(write_c(font, a.name, a.ttf.name), encoding="utf-8")
            print(f"{a.name}: {len(font.glyphs)} glyphs, {len(font.kerns)} kerning pairs -> {a.out}")
        return 0
    if a.cmd == "family":
        cfg = FAMILIES[a.family]
        a.out.mkdir(parents=True, exist_ok=True)
        mode = a.render or cfg["render"]
        thr = a.threshold or cfg.get("threshold", 128)
        for size in (int(x) for x in a.sizes.split(",")):
            for st in a.styles.split(","):
                rel, ax = cfg["faces"][st]
                path = HERE / "fonts" / rel
                if not path.exists():
                    print(f"{path}: missing, run fetch-fonts.sh", file=sys.stderr)
                    return 1
                font = build(path, size, style=STYLES[st], axes=ax, opsz=cfg.get("opsz", False),
                             mode=mode, threshold=thr, name=cfg["name"])
                data = ibf.write(font)
                dst = a.out / f"{a.family}-{size}-{st}.ibf"
                dst.write_bytes(data)
                print(f"{dst.name}: {len(data):,} B, {len(font.glyphs)} glyphs, {len(font.kerns)} kerning pairs")
        return 0
    if a.cmd == "validate":
        bad = 0
        for f in a.files:
            try:
                p = ibf.validate(f.read_bytes())
                print(f"{f}: OK ({len(p.font.glyphs)} glyphs, {len(p.font.kerns)} kerning pairs)")
            except ibf.FormatError as e:
                print(f"{f}: INVALID: {e}")
                bad += 1
        return 1 if bad else 0
    if a.cmd == "dump":
        print(ibf.dump(ibf.parse(a.file.read_bytes()), a.sample))
        return 0
    if a.cmd == "sheet":
        sheet(a.files, a.out, a.text.replace("\\n", "\n"), a.width)
        print(f"wrote {a.out}")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
