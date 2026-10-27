"""Testele codecului IBF (ibf.py) și ale constructorului de fonturi (fonttool.py).

    cd tools/fonttool && python3 -m unittest test_fonttool

Testele constructorului folosesc Noto Serif din fonts/ (fetch-fonts.sh) sau din
sistem și se sar când nu e în niciunul.
"""
import pathlib
import re
import struct
import unittest
import zlib

import fonttool
import ibf

HERE = pathlib.Path(__file__).resolve().parent
NOTO = next((p for p in (HERE / "fonts/notoserif/NotoSerif-Regular.ttf",
                         pathlib.Path("/usr/share/fonts/noto/NotoSerif-Regular.ttf")) if p.exists()), None)


def glyph(cp, adv=512, w=3, h=2, kern_as=None):
    rows = [bytes([0b10100000 | (cp & 0x1F)])] * h if w else []
    return ibf.Glyph(cp, adv, w if h else 0, h if w else 0, 0, -h, rows, kern_as)


def tiny_font(**kw):
    cps = [0x20, 0x2D, 0x41, 0x56, 0x61, 0x103, 0xFFFD]
    gl = [glyph(cp, w=0 if cp == 0x20 else 3) for cp in cps]
    gl[cps.index(0x103)].kern_as = 0x61                       # ă are kerningul lui a
    kerns = {(0x41, 0x56): -80, (0x56, 0x61): -60, (0x56, 0x103): -20}
    args = dict(name="Tiny", pixel_size=10, line_height=13, ascent=10, descent=3, x_height=5,
                glyphs=gl, kerns=kerns)
    args.update(kw)
    return ibf.Font(**args)


def recrc(data: bytearray) -> bytes:
    """Repară ambele CRC-uri după o modificare intenționată, ca validatorul să ajungă la celelalte reguli."""
    struct.pack_into("<I", data, 88, zlib.crc32(data[96:]))
    struct.pack_into("<I", data, 92, zlib.crc32(data[:92]))
    return bytes(data)


class TestCodec(unittest.TestCase):
    def test_round_trip(self):
        f = tiny_font()
        p = ibf.parse(ibf.write(f))
        self.assertEqual(p.font.name, "Tiny")
        self.assertEqual([g.cp for g in p.font.glyphs], sorted(g.cp for g in f.glyphs))
        for a, b in zip(sorted(f.glyphs, key=lambda g: g.cp), p.font.glyphs):
            self.assertEqual((a.advance, a.width, a.height, a.x_off, a.y_off, a.rows, a.kern_as),
                             (b.advance, b.width, b.height, b.x_off, b.y_off, b.rows, b.kern_as))
        self.assertEqual(p.font.kerns, f.kerns)
        self.assertEqual(ibf.write(p.font), ibf.write(f))

    def test_layout(self):
        data = ibf.write(tiny_font())
        self.assertEqual(data[:8], ibf.MAGIC)
        h = ibf.HEADER.unpack_from(data)
        ranges_off, glyphs_off, kerns_off = h[16], h[17], h[18]
        self.assertEqual(ranges_off, 96)
        for off in (ranges_off, glyphs_off, kerns_off):
            self.assertEqual(off % 4, 0)
        # 0x20, 0x2D, 0x41, 0x56, 0x61, 0x103, 0xFFFD: fiecare cu intervalul lui
        self.assertEqual(h[13], 7)

    def test_ranges_merge(self):
        self.assertEqual(ibf._ranges([0x20, 0x21, 0x22, 0x30, 0x31]), [(0x20, 3, 0), (0x30, 2, 3)])

    def test_shared_bitmaps(self):
        f = tiny_font()
        f.glyphs.append(glyph(0x410))                        # А chirilic, același bitmap ca A (0x41 & 0x1F == 0x410 & 0x1F)
        p = ibf.parse(ibf.write(f))
        self.assertGreaterEqual(p.shared_bitmaps, 1)

    def test_required(self):
        for cp in ibf.REQUIRED:
            f = tiny_font()
            f.glyphs = [g for g in f.glyphs if g.cp != cp]
            with self.assertRaises(ibf.FormatError):
                ibf.write(f)

    def test_kern_lookup(self):
        f = ibf.parse(ibf.write(tiny_font())).font
        self.assertEqual(ibf.kerning(f, 0x41, 0x56), -80)
        self.assertEqual(ibf.kerning(f, 0x56, 0x61), -60)
        self.assertEqual(ibf.kerning(f, 0x56, 0x103), -20)    # excepția bate -60 împrumutat
        self.assertEqual(ibf.kerning(f, 0x41, 0x103), 0)
        self.assertEqual(ibf.kerning(f, 0x56, 0x41), 0)

    def test_kerns_sorted(self):
        data = ibf.write(tiny_font())
        n, off = struct.unpack_from("<I", data, 32)[0], struct.unpack_from("<I", data, 44)[0]
        pairs = [ibf.KERN.unpack_from(data, off + i * 6)[:2] for i in range(n)]
        self.assertEqual(pairs, sorted(pairs))

    def test_kern_as_chain_rejected(self):
        f = tiny_font()
        f.glyphs.append(glyph(0xE2, kern_as=0x103))           # â -> ă -> a: doi pași
        with self.assertRaises(ibf.FormatError):
            ibf.write(f)

    def test_every_bit_flip_detected(self):
        data = ibf.write(tiny_font())
        for i in range(0, len(data), 3):
            for bit in (0, 5):
                bad = bytearray(data)
                bad[i] ^= 1 << bit
                with self.assertRaises(ibf.FormatError, msg=f"octetul {i}, bitul {bit}"):
                    ibf.parse(bytes(bad))

    def test_truncated(self):
        data = ibf.write(tiny_font())
        for n in (0, 10, 95, len(data) - 1):
            with self.assertRaises(ibf.FormatError):
                ibf.parse(data[:n])

    def test_rules_behind_crc(self):
        data = ibf.write(tiny_font())
        h = ibf.HEADER.unpack_from(data)
        glyphs_off, kerns_off = h[17], h[18]

        bad = bytearray(data)                                 # tabel nealiniat
        struct.pack_into("<I", bad, 40, glyphs_off + 2)
        with self.assertRaisesRegex(ibf.FormatError, "nu e aliniat"):
            ibf.parse(recrc(bad))

        bad = bytearray(data)                                 # perechi de kerning nesortate
        a = bytes(bad[kerns_off:kerns_off + 6])
        bad[kerns_off:kerns_off + 6] = bad[kerns_off + 6:kerns_off + 12]
        bad[kerns_off + 6:kerns_off + 12] = a
        with self.assertRaisesRegex(ibf.FormatError, "kerning .* nesortată"):
            ibf.parse(recrc(bad))

        bad = bytearray(data)                                 # bitmap în afara zonei bitmapurilor
        struct.pack_into("<I", bad, glyphs_off + 12 * 2, 10_000)
        with self.assertRaisesRegex(ibf.FormatError, "iese din zona bitmapurilor"):
            ibf.parse(recrc(bad))

        bad = bytearray(data)                                 # kern_as în afara limitelor
        struct.pack_into("<H", bad, glyphs_off + 10, 99)
        with self.assertRaisesRegex(ibf.FormatError, "kern_as .* în afara limitelor"):
            ibf.parse(recrc(bad))

        bad = bytearray(data)                                 # bpp 2
        bad[12] = 2
        with self.assertRaisesRegex(ibf.FormatError, "bpp 2 nu e suportat"):
            ibf.parse(recrc(bad))

    def test_dump(self):
        text = ibf.dump(ibf.parse(ibf.write(tiny_font())), "Aă")
        self.assertIn("Tiny normal, 10 px", text)
        self.assertIn("kerning ca U+0061", text)


class TestRanges(unittest.TestCase):
    def test_converter_reads_ranges(self):
        # glyphs.py din convertor citește DEFAULT_RANGES din fonttool.py cu expresia asta regulată
        src = (HERE / "fonttool.py").read_text(encoding="utf-8")
        m = re.search(r"DEFAULT_RANGES = \((.*?)\)", src, re.S)
        spec = "".join(re.findall(r'"([^"]*)"', m.group(1)))
        self.assertEqual(spec, fonttool.DEFAULT_RANGES)

    def test_parse_ranges(self):
        self.assertEqual(fonttool.parse_ranges("0x41-0x43, 0x20,0x41"), [0x20, 0x41, 0x42, 0x43])


LITERATA_I = HERE / "fonts/literata/Literata-Italic[opsz,wght].ttf"


@unittest.skipUnless(LITERATA_I.exists(), "Literata nu a fost găsită (rulați fetch-fonts.sh)")
class TestFeatureVariations(unittest.TestCase):
    def test_bitmap_matches_advance(self):
        # Literata Italic înlocuiește 'w' cu o glifă mai lată la mărimile optice mici.
        # Și bitmapul trebuie să fie al acelei glife, nu al celei implicite cu avansul
        # glifei late (ceea ce lăsa un gol după fiecare 'w').
        f = fonttool.build(LITERATA_I, 18, axes={"wght": 400}, opsz=True, mode="gray",
                           ranges="0x20,0x2D,0x77,0xFFFD", kern=False, log=lambda *_: None)
        w = next(g for g in f.glyphs if g.cp == ord("w"))
        # Spațiul lateral drept: cam 1,4 px cu glifa bună, 2,4 px cu cea greșită.
        self.assertLess(w.advance - (w.x_off + w.width) * ibf.UNIT, ibf.UNIT * 3 // 2)


@unittest.skipUnless(NOTO, "Noto Serif nu a fost găsit (rulați fetch-fonts.sh)")
class TestBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.font = fonttool.build(NOTO, 18, log=lambda *_: None)
        cls.data = ibf.write(cls.font)
        cls.by_cp = {g.cp: g for g in cls.font.glyphs}

    def test_valid(self):
        ibf.validate(self.data)

    def test_charset(self):
        want = [cp for cp in fonttool.parse_ranges(fonttool.DEFAULT_RANGES) if cp not in fonttool.SKIP]
        for cp in (0x20, 0x2D, 0xFFFD, 0x218, 0x219, 0x21A, 0x21B, 0x103, 0xE2, 0xEE, 0x44F, 0x2014, 0x201E):
            self.assertIn(cp, self.by_cp, f"U+{cp:04X}")
        self.assertGreaterEqual(len(self.font.glyphs), len(want) - 3)
        self.assertNotIn(0xAD, self.by_cp)

    def test_metrics(self):
        f = self.font
        self.assertEqual(f.line_height, f.ascent + f.descent)
        self.assertTrue(8 <= f.x_height <= 12)
        self.assertEqual(self.by_cp[0x20].width, 0)
        self.assertEqual(self.by_cp[0xA0].advance, self.by_cp[0x20].advance)
        # avansurile sunt fracționare: nu toate sunt pixeli întregi
        self.assertTrue(any(g.advance % ibf.UNIT for g in f.glyphs))
        m = self.by_cp[ord("m")]
        self.assertTrue(12 * ibf.UNIT < m.advance < 18 * ibf.UNIT)
        self.assertLess(m.y_off, 0)
        self.assertGreaterEqual(m.y_off, -f.x_height - 1)

    def test_kerning(self):
        f = self.font
        self.assertLess(ibf.kerning(f, ord("A"), ord("V")), -ibf.UNIT // 4)
        self.assertLess(ibf.kerning(f, ord("T"), ord("a")), 0)
        self.assertEqual(self.by_cp[0x103].kern_as, ord("a"))     # ă are kerningul lui a
        self.assertEqual(self.by_cp[0x219].kern_as, ord("s"))     # ș are kerningul lui s
        self.assertEqual(self.by_cp[0x419].kern_as, 0x418)        # Й are kerningul lui И
        self.assertTrue(all(abs(v) >= fonttool.KERN_MIN for (a, b), v in f.kerns.items()
                            if self.by_cp[a].kern_as is None and self.by_cp[b].kern_as is None))

    def test_kerning_close_to_font(self):
        m = fonttool.open_font(NOTO, 18)
        for pair in ("AV", "Vă", "Ta", "Ță", "ТА", "Гл", "“A", "r."):
            a, b = (ord(c) for c in pair)
            exact = fonttool.measure(m, pair) - self.by_cp[a].advance - self.by_cp[b].advance
            self.assertLess(abs(ibf.kerning(self.font, a, b) - exact), fonttool.KERN_MIN, pair)

    def test_stand_in(self):
        f = fonttool.build(NOTO, 12, ranges="0x20,0x2D,0x41,0xFFFD,0x10FFF0", kern=False,
                           log=lambda *_: None)
        self.assertNotIn(0x10FFF0, {g.cp for g in f.glyphs})     # fără înlocuitor: lăsat pe dinafară
        self.assertEqual(fonttool.STAND_INS[0x2010], 0x2D)

    def test_gray(self):
        f = fonttool.build(NOTO, 18, mode="gray", threshold=140, ranges="0x20-0x7E,0xFFFD", kern=False,
                           log=lambda *_: None)
        p = ibf.parse(ibf.write(f))
        self.assertEqual((p.font.render, p.font.threshold), (ibf.RENDER_GRAY, 140))

    def test_c_output(self):
        f = fonttool.build(NOTO, 12, ranges="0x20-0x7E,0xFFFD", log=lambda *_: None)
        src = fonttool.write_c(f, "font_test", "NotoSerif-Regular.ttf")
        self.assertIn("const font_t font_test = {", src)
        self.assertIn(f".glyph_count = {len(f.glyphs)},", src)
        self.assertEqual(src.count("// U+"), len(f.glyphs))


if __name__ == "__main__":
    unittest.main()
