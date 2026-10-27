#!/usr/bin/env python3
"""fonttool: TTF/OTF -> fonturi bitmap IBF pentru cititorul e-ink (CONV-8).

    fonttool.py build --ttf FONT.ttf --size 18 [--style b] [--axes wght=700]
                      [--render mono|gray] [--threshold 128] [--name "PT Serif"] -o out.ibf
    fonttool.py family ptserif --sizes 16,18,20 -o DIR     # toate cele patru stiluri pe mărime, din FAMILIES
    fonttool.py c --ttf FONT.ttf --size 16 --name font_ui16 --out font_ui16.c   # font compilat în firmware
    fonttool.py validate FILE.ibf [...]
    fonttool.py dump FILE.ibf [--sample "Șa"]
    fonttool.py sheet FILE.ibf [...] -o sheet.png [--text "..."] [--width 240]

Formatul e spec/font-format.md, implementat de ibf.py.

Bitmapurile vin de la FreeType prin Pillow, în unul din două feluri:
  mono  randarea monocromă cu hinting a lui FreeType. Cea mai bună pentru
        fonturile cu hinting TrueType bun (PT Serif, Noto Serif).
  gray  randare cu antialiasing, apoi cerneală unde acoperirea >= prag. Mai
        bună pentru fonturile fără hinting, ca fonturile variabile (Literata).
Avansurile și kerningul sunt valorile exacte (fără hinting) ale fontului în
1/64 px, de la HarfBuzz (paginarea raqm din Pillow), cu ligaturile oprite.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont

import ibf

# Latin de bază + Latin-1 + Latin Extended-A (ă â î ş ţ ...), ș ț românești cu
# virgulă dedesubt (U+0218–U+021B), chirilice (rusă + ucraineană), punctuație tipografică.
DEFAULT_RANGES = (
    "0x20-0x7E,0xA0-0xFF,0x100-0x17F,0x218-0x21B,0x400-0x45F,"
    "0x2010-0x2014,0x2018-0x201E,0x2022,0x2026,0x2039-0x203A,0x20AC,0xFFFD"
)
# Nedesenate din font: caracterele de control ale paginării și U+00AD (paginarea desenează '-').
SKIP = {0xAD}
# Punctuația lipsă se desenează cu un semn asemănător. Literele accentuate lipsă folosesc litera de bază.
STAND_INS = {0x2010: 0x2D, 0x2011: 0x2D, 0x2012: 0x2013, 0x201B: 0x2019, 0x201F: 0x201D}
KERN_MIN = 16                 # u; perechile mai mici se scot (specificația, 3.4)
SCREEN_DPI = 130              # panoul de 3,7", 240 x 416: dă mărimea optică a Literatei
HERE = pathlib.Path(__file__).resolve().parent
STYLES = {"r": 0, "b": ibf.BOLD, "i": ibf.ITALIC, "bi": ibf.BOLD | ibf.ITALIC}

# Fonturile candidate pentru cărți (le descarcă fetch-fonts.sh). Căile sunt sub fonts/.
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
    # Doar latin: o carte chirilică în Libron ar primi căsuțe U+FFFD.
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
    """Fontul la `size` px, cu axele de variație fixate. Și bitmapurile, și avansurile
    trec prin HarfBuzz (RAQM): fonturi ca Literata schimbă glifele după mărimea
    optică (feature variations), pe care FreeType singur nu le aplică, deci
    bitmapurile și avansurile ar veni de la glife diferite."""
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
    """Întoarce (rows, w, h, x_off, y_off): cutia cu cerneala glifei, 1 = cerneală."""
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls", mode="1" if mode == "mono" else "L")
    x0, y0, x1, y1 = x0 - 1, y0 - 1, x1 + 1, y1 + 1        # loc pentru ce iese din antialiasing
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
    """Grupuri de coduri Unicode pentru care merită kerning între ele: literele unei
    scrieri plus toată punctuația. Perechile latin–chirilic nu apar în text."""
    punct = [c for c in cps if unicodedata.category(chr(c))[0] in "PS" and c < 0x2100]
    latin = [c for c in cps if unicodedata.category(chr(c))[0] == "L" and c < 0x250]
    cyr = [c for c in cps if unicodedata.category(chr(c))[0] == "L" and 0x400 <= c < 0x500]
    return [latin + punct, cyr + punct]


def build(path, size: int, *, style: int = 0, axes=None, opsz=False, mode="mono", threshold=128,
          name: str | None = None, ranges: str = DEFAULT_RANGES, kern: bool = True,
          log=print) -> ibf.Font:
    font = open_font(path, size, axes, opsz)
    notdef = render(font, chr(0x10FFFD), mode, threshold)   # un cod Unicode pe care nu-l are niciun font
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
    for cp in missing[:]:                 # înlocuitori: aceeași glifă (U+2010 -> '-') sau litera de bază
        sub = STAND_INS.get(cp) or ord(unicodedata.normalize("NFD", chr(cp))[0])
        if sub != cp and sub in have:
            s = have[sub]
            have[cp] = ibf.Glyph(cp, s.advance, s.width, s.height, s.x_off, s.y_off, s.rows, sub)
            glyphs.append(have[cp])
            missing.remove(cp)
            substituted.append(cp)
    if 0xFFFD in have and not have[0xFFFD].rows:
        missing.append(0xFFFD)
    if 0xFFFD not in have or not have[0xFFFD].rows:     # fontul nu are U+FFFD: se desenează o căsuță
        adv = have[0x20].advance * 2 if 0x20 in have else size * ibf.UNIT // 2
        w, h = max(adv // ibf.UNIT - 2, 3), max(size * 7 // 10, 4)
        have[0xFFFD] = ibf.Glyph(0xFFFD, adv, w, h, 1, -h, _box(w, h))
        glyphs = [g for g in glyphs if g.cp != 0xFFFD] + [have[0xFFFD]]

    kerns: dict[tuple[int, int], int] = {}
    if kern:
        for g in glyphs:                  # literele accentuate au kerningul literei de bază
            base = ord(unicodedata.normalize("NFD", chr(g.cp))[0])
            if g.kern_as is None and base != g.cp and base in have:
                g.kern_as = base
        for g in glyphs:                  # arată direct spre glifa care are perechile
            while g.kern_as is not None and have[g.kern_as].kern_as is not None:
                g.kern_as = have[g.kern_as].kern_as
        ka = {g.cp: g.cp if g.kern_as is None else g.kern_as for g in glyphs}
        exact = {}                        # înlocuitorii nu sunt în fontul sursă: doar împrumută
        for group in _kern_set([cp for cp in have if cp not in substituted]):
            for a in group:
                for b in group:
                    exact[(a, b)] = measure(font, chr(a) + chr(b)) - have[a].advance - have[b].advance
        for (a, b), v in exact.items():           # perechi de glife cu kerningul lor propriu
            if ka[a] == a and ka[b] == b and abs(v) >= KERN_MIN:
                kerns[(a, b)] = v
        for (a, b), v in exact.items():           # excepțiile unde împrumutul greșește cu >= 1/4 px
            if (ka[a] != a or ka[b] != b) and abs(v - kerns.get((ka[a], ka[b]), 0)) >= KERN_MIN:
                kerns[(a, b)] = v
    ascent, descent = font.getmetrics()
    xb = font.getbbox("x", anchor="ls")
    fam = name or font.getname()[0]
    font = ibf.Font(fam, size, ascent + descent, ascent, descent, -xb[1], glyphs, kerns, style,
                    ibf.RENDER_GRAY if mode == "gray" else ibf.RENDER_MONO,
                    threshold if mode == "gray" else 0)
    if substituted:
        log(f"  {len(substituted)} lipsesc din fontul sursă, desenate prin înlocuitor: "
            + "".join(chr(cp) for cp in substituted))
    if missing:
        log("  lipsesc din fontul sursă (desenate ca U+FFFD): " + " ".join(f"U+{cp:04X}" for cp in missing))
    return font


def _box(w: int, h: int) -> list[bytes]:
    """Un dreptunghi conturat, înlocuitorul pentru U+FFFD."""
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
    """Fontul ca tabele C, pentru fonturile compilate în firmware (fontul interfeței)."""
    data = ibf.write(font)            # verifică și fontul
    p = ibf.parse(data)
    f = p.font
    cps = [g.cp for g in f.glyphs]
    index = {cp: i for i, cp in enumerate(cps)}
    glyph_off, bitmaps_off = struct.unpack_from("<I", data, 40)[0], struct.unpack_from("<I", data, 48)[0]
    bitmaps = data[bitmaps_off:]
    out = [f"// Generat de tools/fonttool/fonttool.py, nu se editează de mână.",
           f"// Sursa: {source} la {f.pixel_size} px (SIL Open Font License).",
           f"// {len(cps)} glife, {len(f.kerns)} perechi de kerning, {len(bitmaps)} octeți de bitmapuri.",
           f"// Regenerare: python3 tools/fonttool/fonttool.py c --ttf {source} --size {f.pixel_size} "
           f"--name {symbol} --out <acest fișier>",
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
    """Desenează cu bitmapurile, avansurile și kerningul fontului, ca dispozitivul:
    penița în 1/64 px, fiecare glifă la poziția rotunjită a peniței. Întoarce x-ul peniței."""
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
        p.add_argument("--ttf", required=True, type=pathlib.Path, help="fontul sursă (TTF/OTF)")
        p.add_argument("--size", type=int, required=True, help="mărimea în pixeli (em)")
        p.add_argument("--axes", default="", help="axele fontului variabil, de ex. wght=700,opsz=10")
        p.add_argument("--opsz", action="store_true", help="axa de mărime optică după mărime")
        p.add_argument("--render", choices=("mono", "gray"), default="mono", help="mono cu hinting sau gri cu prag")
        p.add_argument("--threshold", type=int, default=128, help="gri: cerneală la acoperire >= T/255")
        p.add_argument("--ranges", default=DEFAULT_RANGES, help="intervalele de caractere incluse")
        p.add_argument("--no-kern", action="store_true", help="fără perechi de kerning")
        p.add_argument("--family-name", help="numele scris în fișier (implicit: al fontului)")
    b = sub.add_parser("build", help="un stil la o mărime -> .ibf")
    face_args(b)
    b.add_argument("--style", choices=STYLES, default="r", help="r normal, b aldin, i cursiv, bi aldin cursiv")
    b.add_argument("-o", "--out", type=pathlib.Path, required=True, help="fișierul .ibf scris")
    fam = sub.add_parser("family", help="o familie din FAMILIES la mai multe mărimi -> DIR/<familie>-<mărime>-<stil>.ibf")
    fam.add_argument("family", choices=FAMILIES, help="familia de fonturi")
    fam.add_argument("--sizes", default="16,18,20", help="mărimile, separate prin virgulă")
    fam.add_argument("--styles", default="r,b,i,bi", help="stilurile, separate prin virgulă")
    fam.add_argument("--render", choices=("mono", "gray"), help="în locul celui implicit al familiei")
    fam.add_argument("--threshold", type=int, help="gri: pragul, în locul celui al familiei")
    fam.add_argument("-o", "--out", type=pathlib.Path, required=True, help="dosarul de ieșire")
    c = sub.add_parser("c", help="un stil -> sursă C, pentru fonturile compilate în firmware")
    face_args(c)
    c.add_argument("--name", required=True, help="simbolul C, de ex. font_ui16")
    c.add_argument("--out", required=True, type=pathlib.Path, help="fișierul .c scris")
    v = sub.add_parser("validate", help="verifică fișiere .ibf după toate regulile")
    v.add_argument("files", nargs="+", type=pathlib.Path, help="fișierele .ibf")
    d = sub.add_parser("dump", help="afișează antetul, intervalele și glifele unui .ibf")
    d.add_argument("file", type=pathlib.Path, help="fișierul .ibf")
    d.add_argument("--sample", default="", help="caractere de desenat, cu metricile lor")
    s = sub.add_parser("sheet", help="desenează o mostră cu metricile fonturilor -> PNG")
    s.add_argument("files", nargs="+", type=pathlib.Path, help="fișierele .ibf")
    s.add_argument("-o", "--out", type=pathlib.Path, required=True, help="imaginea PNG scrisă")
    s.add_argument("--text", default=SHEET_TEXT, help="\\n desparte rândurile")
    s.add_argument("--width", type=int, default=240, help="lățimea în pixeli")
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
            print(f"{a.name}: {len(font.glyphs)} glife, {len(font.kerns)} perechi de kerning -> {a.out}")
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
                    print(f"{path}: lipsește, rulați fetch-fonts.sh", file=sys.stderr)
                    return 1
                font = build(path, size, style=STYLES[st], axes=ax, opsz=cfg.get("opsz", False),
                             mode=mode, threshold=thr, name=cfg["name"])
                data = ibf.write(font)
                dst = a.out / f"{a.family}-{size}-{st}.ibf"
                dst.write_bytes(data)
                print(f"{dst.name}: {len(data):,} B, {len(font.glyphs)} glife, {len(font.kerns)} perechi de kerning")
        return 0
    if a.cmd == "validate":
        bad = 0
        for f in a.files:
            try:
                p = ibf.validate(f.read_bytes())
                print(f"{f}: OK ({len(p.font.glyphs)} glife, {len(p.font.kerns)} perechi de kerning)")
            except ibf.FormatError as e:
                print(f"{f}: NEVALID: {e}")
                bad += 1
        return 1 if bad else 0
    if a.cmd == "dump":
        print(ibf.dump(ibf.parse(a.file.read_bytes()), a.sample))
        return 0
    if a.cmd == "sheet":
        sheet(a.files, a.out, a.text.replace("\\n", "\n"), a.width)
        print(f"s-a scris {a.out}")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
