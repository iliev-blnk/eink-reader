#!/usr/bin/env python3
"""Scrie corpusurile de pornire ale fuzzerelor în build/seeds/{ibv,font,state}:
cărți mici (comprimate și nu, cu copertă și cuprins, mai multe blocuri), un font
mic cu kerning și fișierele de stare (.pg, .pos, .bm, recent, setări) cu CRC
corect. Fuzzerul pornește de la fișiere valide și le strică el."""
import pathlib
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools/converter"))
sys.path.insert(0, str(ROOT / "tools/fonttool"))
import ibf  # noqa: E402
import ibv  # noqa: E402

OUT = HERE / "build/seeds"
SHY, NBSP, LSEP = "­", " ", " "


def books():
    lamp = "The lamp was lit at dusk and the street went quiet, and nobody said anything at all."
    recs = [
        ibv.Record(ibv.HEADING, "Chapter One", ibv.PAGE),
        ibv.Record(ibv.PARA, "Bold and italic runs, in" + SHY + "com" + SHY + "pre" + SHY + "hen" + SHY + "si" + SHY
                   + "ble, AV Ta.", 0, [(0, 4, 1), (9, 6, 2)]),
        ibv.Record(ibv.VERSE, "one" + LSEP + LSEP + "two" + LSEP + NBSP + "three"),
        ibv.Record(ibv.BREAK, flags=ibv.BREAK_ORNAMENT),
        ibv.Record(ibv.QUOTE, lamp),
        ibv.Record(ibv.PARA, lamp + " " + lamp),
        ibv.Record(ibv.PARA, "continued " + lamp, ibv.CONT),
        ibv.Record(ibv.HEADING, "Глава вторая", ibv.PAGE | 1 << ibv.LEVEL_SHIFT),
        ibv.Record(ibv.PARA, "Ро" + SHY + "ман и țara " + "x" * 300),
        ibv.Record(ibv.BREAK),
    ]
    toc = [ibv.TocEntry(i, r.level, r.text) for i, r in enumerate(recs) if r.kind == ibv.HEADING]
    cover = ibv.Cover(16, 8, bytes([0xF0, 0x0F] * 8))
    meta = {"title": "Seed", "author": "fuzz", "language": "en"}
    yield "mic", ibv.write(ibv.Book(meta, recs, toc, cover), block_target=200)
    yield "stocat", ibv.write(ibv.Book(meta, recs, toc, cover), block_target=300, compress=False)
    yield "ru", ibv.write(ibv.Book(dict(meta, language="ru"), recs[:3]), compress=True)
    yield "gol", ibv.write(ibv.Book({"title": "x", "language": "ro"}, [ibv.Record(ibv.PARA, "x")]))


def font():
    gl = [ibf.Glyph(cp, 640, 6, 8, 1, -8, [b"\xfc"] * 8) for cp in (0x20, 0x2D, 0x41, 0x56, 0x61, 0xFFFD)]
    gl[0] = ibf.Glyph(0x20, 320)
    gl.append(ibf.Glyph(0xC0, 640, 6, 10, 1, -10, [b"\xfc"] * 10, kern_as=0x41))
    f = ibf.Font("seed", 10, 12, 9, 3, 5, gl, {(0x41, 0x56): -64, (0x56, 0x41): -48})
    yield "mic", ibf.write(f)


def crc(b):
    return struct.pack("<I", zlib.crc32(b))


def state():
    bid = bytes(range(1, 9))
    pg = b"IPGC" + bid + struct.pack("<II", 0x1234, 3) + bytes([1, 0, 0, 0])
    pg += b"".join(struct.pack("<II", p, b) for p, b in ((0, 0), (0, 500), (4, 12)))
    yield "pg", b"\0" + pg + crc(pg)
    pos = b"IPOS" + bid + struct.pack("<II", 7, 30)
    yield "pos", b"\1" + pos + crc(pos)
    bm = b"IBMK" + bid + struct.pack("<H", 2) + struct.pack("<IIII", 1, 2, 3, 4)
    yield "bm", b"\2" + bm + crc(bm)
    rec = b"IREC" + bytes([1, 1, 2, 0])
    for p in (b"carti/a.ibv", b"b.ibv"):
        rec += bid + bytes([len(p)]) + p
    yield "recent", b"\3" + rec + crc(rec)
    st = bytes([1, 2, 1, 1, 1, 1, 2, 1])
    yield "setari", b"\4" + st + crc(st)


def main():
    for kind, gen in (("ibv", books), ("font", font), ("state", state)):
        d = OUT / kind
        d.mkdir(parents=True, exist_ok=True)
        for name, data in gen():
            (d / name).write_bytes(data)
    if (ROOT / "sim/test/layout.ibv").exists():
        (OUT / "ibv/layout").write_bytes((ROOT / "sim/test/layout.ibv").read_bytes())
    for p in sorted((ROOT / "tools/fonttool/out/fam").glob("*-1[56]-r.ibf"))[:4]:
        (OUT / "font" / p.name).write_bytes(p.read_bytes())
    print("semințe în", OUT)


main()
