#!/usr/bin/env python3
"""Testul diferențial al formatului IBV: cititorul din firmware (ibv.c) față de
validatorul de referință (tools/converter/ibv.py).

Fuzzerele verifică doar că firmware-ul nu citește în afara memoriei. Aici se
verifică un contract între cele două implementări:

1. Orice carte pe care validatorul o acceptă, dispozitivul o deschide și o
   citește întreagă, cu exact aceleași înregistrări, stiluri, metadate, cuprins
   și copertă.
2. Validatorul nu cade: orice fișier e acceptat sau refuzat cu FormatError.
3. Cititorul nu cade (ibv_dump e compilat cu ASan și UBSan, cu CRC-urile
   verificate, ca pe dispozitiv).

Dispozitivul are voie să fie mai îngăduitor decât validatorul: nu verifică tot
ce verifică validatorul (NFC, spațiile, ordinea cuprinsului). Cazurile acestea
se numără și se afișează grupate după motivul refuzului, ca să se vadă ce lasă
dispozitivul în seama convertorului.

Fișierele de test sunt cărți valide stricate anume, apoi „resigilate”: CRC-urile
blocurilor, ale secțiunilor, al directorului și al antetului (uneori și book_id
și numerele din antet) se recalculează, deci stricăciunea trece de verificările
CRC și ajunge la regulile de structură. Fără resigilare, aproape orice
stricăciune s-ar opri la primul CRC.

    tools/fuzz/conformanta.py [N] [--samanta S]     # implicit 5000 de cazuri, ~2 min

Cazurile care încalcă contractul se scriu în build/conformanta/. Iese cu 1 dacă
există vreunul.
"""
import hashlib
import os
import random
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools/converter"))
import ibv  # noqa: E402

BUILD = HERE / "build"
DUMP = BUILD / "ibv_dump"
CORE = ROOT / "firmware/components/core"


def compileaza():
    BUILD.mkdir(exist_ok=True)
    src = [HERE / "ibv_dump.c", CORE / "ibv.c", CORE / "crc32.c", ROOT / "sim/inflate_zlib.c"]
    if DUMP.exists() and all(DUMP.stat().st_mtime > p.stat().st_mtime for p in src + [CORE / "include/ibv.h"]):
        return
    # pe 32 de biți, ca pe ESP32-S3 și ca fuzzerele
    subprocess.run(["clang", os.environ.get("FUZZ_M32", "-m32"), "-g", "-O1", "-fno-omit-frame-pointer",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{CORE}/include",
                    "-o", str(DUMP), *map(str, src), "-lz"], check=True)


# ---------------------------------------------------------------- ce ar trebui să vadă dispozitivul

def asteptat(p: ibv.Parsed) -> str:
    """Rândurile pe care ibv_dump trebuie să le scrie pentru o carte validă."""
    out = [f"H {p.paragraph_count} {p.text_bytes} {p.book_id.hex()}"]
    for key, name in ibv.META_KEYS.items():
        if name in p.meta:
            out.append(f"M {key} {p.meta[name].encode().hex() or '-'}")
    for e in p.toc:
        out.append(f"T {e.para} {e.level} {e.title.encode().hex() or '-'}")
    if p.cover:
        out.append(f"C {p.cover.width} {p.cover.height} {ibv.crc32(p.cover.bits):08x}")
    off = 0
    for r in p.records():
        tb = r.text.encode()
        seg = ",".join(f"{s}-{s + n}:{st}" for s, n, st in r.runs) or "-"
        out.append(f"R {r.kind} {r.flags} {off} {tb.hex() or '-'} {seg}")
        off += len(tb)
    return "\n".join(out)


# ---------------------------------------------------------------- semințele

def seminte() -> list[tuple[str, bytes]]:
    SHY, NBSP, LSEP = ibv.SHY, ibv.NBSP, ibv.LSEP
    lamp = "The lamp was lit at dusk and the street went quiet, and nobody said anything at all."
    recs = [
        ibv.Record(ibv.HEADING, "Chapter One", ibv.PAGE),
        ibv.Record(ibv.PARA, "Bold and italic runs, in" + SHY + "com" + SHY + "pre" + SHY + "hen" + SHY
                   + "si" + SHY + "ble, AV Ta.", 0, [(0, 4, 1), (9, 6, 2)]),
        ibv.Record(ibv.VERSE, "one" + LSEP + "two" + LSEP + NBSP + "three"),
        ibv.Record(ibv.BREAK, flags=ibv.BREAK_ORNAMENT),
        ibv.Record(ibv.QUOTE, lamp, 0, [(4, 4, 3)]),
        ibv.Record(ibv.PARA, lamp + " " + lamp),
        ibv.Record(ibv.PARA, "continued " + lamp, ibv.CONT),
        ibv.Record(ibv.HEADING, "Глава вторая", ibv.PAGE | 1 << ibv.LEVEL_SHIFT),
        ibv.Record(ibv.PARA, "Ро" + SHY + "ман și țara " + "x" * 300, 0, [(0, 6, 2)]),
        ibv.Record(ibv.BREAK),
    ]
    toc = [ibv.TocEntry(i, r.level, r.text) for i, r in enumerate(recs) if r.kind == ibv.HEADING]
    cover = ibv.Cover(16, 8, bytes([0xF0, 0x0F] * 8))
    meta = {"title": "Sămânță", "author": "test", "language": "ro", "year": "1890"}
    book = ibv.Book(meta, recs, toc, cover)
    out = [("exemplu", (ROOT / "spec/examples/example.ibv").read_bytes()),
           ("stocat", ibv.write(book, block_target=200, compress=False)),
           ("comprimat", ibv.write(book, block_target=300)),
           ("un-bloc", ibv.write(ibv.Book({"title": "x", "language": "en"}, recs[:4]), compress=False))]
    txt = ROOT / "docs/mockup/texts/luceafarul.hy.txt"
    if txt.exists():
        lb = ibv.from_lines(txt.read_text(encoding="utf-8").splitlines()[:120], {"title": "L", "language": "ro"})
        out.append(("luceafarul", ibv.write(lb, block_target=1024, compress=False)))
    # ce permite specificația și o stricăciune întâmplătoare n-ar nimeri: secțiuni
    # necunoscute (mai multe decât încap într-un director „obișnuit”, puse înaintea
    # celor cunoscute), un antet mai lung, chei META și tipuri de înregistrări
    # necunoscute, biți de stil rezervați
    out.append(("sectiuni-extra", reasaza(out[1][1], extra=[(b"X%03d" % i, bytes([i]) * i) for i in range(14)])))
    out.append(("antet-lung", reasaza(out[2][1], hsize=60)))
    out.append(("chei-necunoscute", reasaza(out[0][1], meta_in_plus=[(9, b"nou"), (200, "ă".encode())])))
    viitor = [ibv.Record(ibv.PARA, "Înainte."), ibv.Record(7, "Un tip nou, cu text.", 0, [(0, 2, 0x05)]),
              ibv.Record(9), ibv.Record(ibv.PARA, "După.", 0, [(0, 3, 0x04)])]
    out.append(("tipuri-necunoscute", ibv.write(ibv.Book({"title": "v", "language": "ro"}, viitor), compress=False)))
    for name, data in out:
        ibv.validate(data)   # semințele trebuie să fie valide
    return out


def reasaza(data: bytes, extra=(), hsize=ibv.HEADER_SIZE, meta_in_plus=()) -> bytes:
    """Rescrie o carte validă cu secțiuni în plus (înaintea celorlalte), alt
    header_size sau intrări META în plus; book_id și CRC-urile se refac."""
    p = ibv.parse(data)

    def pl(t):
        o, n, _ = p.sections[t]
        return data[o:o + n]
    meta = pl("META")
    if meta_in_plus:
        n = struct.unpack_from("<H", meta)[0] + len(meta_in_plus)
        meta = struct.pack("<H", n) + meta[2:] + b"".join(struct.pack("<BH", k, len(v)) + v for k, v in meta_in_plus)
    secs = list(extra) + [(t.encode("latin-1"), meta if t == "META" else pl(t)) for t in p.sections]
    off, offs = hsize + ibv.DIR_ENTRY_SIZE * len(secs), {}
    for tag, payload in secs:
        offs[tag] = off
        off += len(payload)
    delta = offs[b"TEXT"] - p.sections["TEXT"][0]
    blks = bytearray(pl("BLKS"))
    for i in range(len(p.blocks)):
        e = 4 + i * ibv.BLOCK_ENTRY_SIZE
        struct.pack_into("<I", blks, e, struct.unpack_from("<I", blks, e)[0] + delta)
    secs = [(t, bytes(blks) if t == b"BLKS" else pl_) for t, pl_ in secs]
    raw = b"".join(p.block_raw(i) for i in range(len(p.blocks)))
    bid = hashlib.sha256(meta + raw).digest()[:8]
    directory = b"".join(struct.pack("<4sIII", t, offs[t], len(x), ibv.crc32(x)) for t, x in secs)
    head = ibv.MAGIC + struct.pack("<BBHI8sIIHHI", *ibv.VERSION, hsize, 0, bid, p.paragraph_count,
                                   p.text_bytes, len(secs), 0, ibv.crc32(directory))
    head += struct.pack("<I", ibv.crc32(head)) + b"\xa5" * (hsize - ibv.HEADER_SIZE)
    return head + directory + b"".join(x for _, x in secs)


# ---------------------------------------------------------------- resigilarea

def u16(d, o): return struct.unpack_from("<H", d, o)[0] if o + 2 <= len(d) else None
def u32(d, o): return struct.unpack_from("<I", d, o)[0] if o + 4 <= len(d) else None


def directorul(d: bytearray):
    hs, n = u16(d, 10), u16(d, 32)
    if hs is None or n is None:
        return 0, []
    ent = []
    for i in range(min(n, 64)):
        o = hs + 16 * i
        if o + 16 > len(d):
            break
        ent.append((o, bytes(d[o:o + 4]), u32(d, o + 4), u32(d, o + 8)))
    return hs, ent


def bloc_brut(d, off, slen, rlen, method):
    if off + slen > len(d):
        return None
    if method == 0:
        return bytes(d[off:off + slen]) if slen == rlen else None
    if method == 1:
        try:
            z = zlib.decompressobj(-15)
            raw = z.decompress(bytes(d[off:off + slen]), rlen + 1)
            return raw if len(raw) == rlen else None
        except zlib.error:
            return None
    return None


def resigileaza(d: bytearray, rnd: random.Random) -> bytearray:
    hs, ent = directorul(d)
    sec = {}
    for o, tag, off, ln in ent:
        sec.setdefault(tag, (o, off, ln))
    blks = sec.get(b"BLKS")
    raws = []
    if blks and blks[1] + 4 <= len(d):
        _, boff, bln = blks
        n = u32(d, boff)
        for i in range(min(n, (min(bln, len(d) - boff) - 4) // 24)):
            e = boff + 4 + 24 * i
            off, slen, rlen, method = u32(d, e), u32(d, e + 4), u16(d, e + 8), d[e + 10]
            raw = bloc_brut(d, off, slen, rlen, method)
            if raw is not None:
                struct.pack_into("<I", d, e + 20, zlib.crc32(raw))
                raws.append((raw, u32(d, e + 12)))
    # book_id și numerele din antet, uneori: altfel orice stricăciune din text s-ar
    # opri la ele și regulile înregistrărilor n-ar fi încercate niciodată
    meta = sec.get(b"META")
    if meta and rnd.random() < 0.7 and len(d) >= 44:
        h = hashlib.sha256(bytes(d[meta[1]:meta[1] + meta[2]]))
        npara = tbytes = 0
        ok = True
        for raw, _ in raws:
            pos = 0
            try:
                while pos < len(raw):
                    r, pos = ibv.decode_record(raw, pos)
                    h.update(ibv.encode_record(r))
                    npara += 1
                    tbytes += len(r.text.encode())
            except (ibv.FormatError, ValueError):
                ok = False
        if ok:
            d[16:24] = h.digest()[:8]
            if rnd.random() < 0.8:
                struct.pack_into("<II", d, 24, npara, tbytes)
    for o, tag, off, ln in ent:
        if off + ln <= len(d):
            struct.pack_into("<I", d, o + 12, zlib.crc32(bytes(d[off:off + ln])))
    if ent and len(d) >= 44:
        a, b = hs, hs + 16 * len(ent)
        struct.pack_into("<I", d, 36, zlib.crc32(bytes(d[a:b])))
    if len(d) >= 44:
        struct.pack_into("<I", d, 40, zlib.crc32(bytes(d[:40])))
    return d


# ---------------------------------------------------------------- stricăciunile

INTERESANTE = [0, 1, 2, 3, 4, 5, 7, 8, 0x0F, 0x10, 0x1F, 0x20, 0x3F, 0x40, 0x7F, 0x80, 0x81, 0xBF, 0xC0, 0xC2,
               0xE2, 0xF0, 0xFE, 0xFF]


def campuri(d: bytes):
    """Deplasamentele și lățimile câmpurilor de structură cunoscute."""
    f = [(8, 1), (9, 1), (10, 2), (12, 4), (24, 4), (28, 4), (32, 2), (34, 2)]
    hs, ent = directorul(bytearray(d))
    for o, tag, off, ln in ent:
        f += [(o, 4), (o + 4, 4), (o + 8, 4)]
        if off + min(ln, 8) > len(d):
            continue
        if tag in (b"META", b"TOC ", b"BLKS", b"COVR"):
            f += [(off, 2 if tag != b"BLKS" else 4)]
        if tag == b"BLKS":
            for i in range(min(u32(d, off) or 0, 64)):
                e = off + 4 + 24 * i
                if e + 24 <= len(d):
                    f += [(e, 4), (e + 4, 4), (e + 8, 2), (e + 10, 1), (e + 11, 1), (e + 12, 4), (e + 16, 4)]
        if tag == b"COVR":
            f += [(off + 2, 2), (off + 4, 1), (off + 5, 1), (off + 6, 4)]
        if tag == b"META":
            pos = off + 2
            for _ in range(min(u16(d, off) or 0, 16)):
                if pos + 3 > len(d):
                    break
                f += [(pos, 1), (pos + 1, 2)]
                pos += 3 + (u16(d, pos + 1) or 0)
        if tag == b"TOC ":
            pos = off + 2
            for _ in range(min(u16(d, off) or 0, 16)):
                if pos + 8 > len(d):
                    break
                f += [(pos, 4), (pos + 4, 1), (pos + 5, 1), (pos + 6, 2)]
                pos += 8 + (u16(d, pos + 6) or 0)
    return [(o, w) for o, w in f if o + w <= len(d)]


def zona_text(d: bytes):
    _, ent = directorul(bytearray(d))
    for _, tag, off, ln in ent:
        if tag == b"TEXT" and off + ln <= len(d):
            return off, ln
    return 0, len(d)


def strica(d: bytes, rnd: random.Random) -> bytearray:
    d = bytearray(d)
    for _ in range(rnd.choice((1, 1, 1, 2, 3))):
        op = rnd.random()
        if op < 0.35:                                   # un câmp de structură
            f = campuri(bytes(d))
            if not f:
                continue
            o, w = rnd.choice(f)
            old = int.from_bytes(d[o:o + w], "little")
            v = rnd.choice([0, 1, old + 1, old - 1, old + rnd.randint(2, 64), old * 2, (1 << (8 * w)) - 1,
                            1 << (8 * w - 1), rnd.getrandbits(8 * w), old ^ (1 << rnd.randrange(8 * w))])
            d[o:o + w] = (v % (1 << (8 * w))).to_bytes(w, "little")
        elif op < 0.8:                                  # un octet din text (blocurile stocate)
            off, ln = zona_text(bytes(d))
            if not ln:
                continue
            i = off + rnd.randrange(ln)
            d[i] = rnd.choice(INTERESANTE) if rnd.random() < 0.5 else d[i] ^ (1 << rnd.randrange(8))
        elif op < 0.92:                                 # un octet oarecare
            i = rnd.randrange(len(d))
            d[i] = rnd.choice(INTERESANTE + [rnd.getrandbits(8)])
        else:                                           # octeți inserați sau scoși
            i = rnd.randrange(len(d))
            if rnd.random() < 0.5:
                d[i:i] = bytes(rnd.choice(INTERESANTE) for _ in range(rnd.randint(1, 4)))
            else:
                del d[i:i + rnd.randint(1, 4)]
    return resigileaza(d, rnd)


# ---------------------------------------------------------------- rularea

def motiv(e: Exception) -> str:
    """Mesajul validatorului fără numere și citate, ca refuzurile să se poată grupa."""
    s = re.sub(r"'[^']*'", "'…'", str(e))
    return re.sub(r"\d+", "N", s)[:90]


def main():
    args = sys.argv[1:]
    seed = 1
    if "--samanta" in args:
        i = args.index("--samanta")
        seed = int(args[i + 1])
        del args[i:i + 2]
    total = int(args[0]) if args else 5000
    compileaza()
    rnd = random.Random(seed)
    sem = seminte()
    fail = BUILD / "conformanta"
    fail.mkdir(exist_ok=True)
    stats, ingaduit, probleme = Counter(), Counter(), []
    with tempfile.TemporaryDirectory() as tmp:
        cale = Path(tmp) / "c.ibv"
        for n in range(total):
            name, base = sem[n % len(sem)]
            data = bytes(base) if n < len(sem) else bytes(strica(base, rnd))
            cale.write_bytes(data)
            try:
                p = ibv.validate(data)
                v_ok, v_err, exp = True, None, asteptat(p)
            except ibv.FormatError as e:
                v_ok, v_err, exp = False, e, None
            except Exception as e:                      # noqa: BLE001  contractul 2
                probleme.append((n, name, f"validatorul cade: {type(e).__name__}: {e}", data))
                stats["validatorul cade"] += 1
                continue
            r = subprocess.run([str(DUMP), str(cale)], capture_output=True, text=True)
            if r.returncode != 0:
                probleme.append((n, name, "cititorul cade:\n" + r.stderr[-1500:], data))
                stats["cititorul cade"] += 1
                continue
            got = r.stdout.strip()
            c_ok = not got.startswith("X ") and "\nE " not in "\n" + got
            if v_ok:
                if got == exp:
                    stats["acceptat de amândouă, identic"] += 1
                else:
                    diff = next((f"ibv.py: {a}\nibv.c:  {b}" for a, b in zip(exp.split("\n"), got.split("\n")) if a != b),
                                f"ibv.py: {len(exp.split(chr(10)))} rânduri, ibv.c: {len(got.split(chr(10)))}")
                    probleme.append((n, name, "validatorul acceptă, dispozitivul vede altceva:\n" + diff, data))
                    stats["NEPOTRIVIRE"] += 1
            elif c_ok:
                stats["refuzat de validator, deschis de dispozitiv"] += 1
                ingaduit[motiv(v_err)] += 1
            else:
                stats["refuzat de amândouă"] += 1
    for k, v in stats.most_common():
        print(f"{v:7}  {k}")
    if ingaduit:
        print("\nce refuză validatorul și dispozitivul lasă în seama convertorului:")
        for k, v in ingaduit.most_common():
            print(f"{v:7}  {k}")
    if probleme:
        print("\nîncălcările contractului, grupate:")
        for k, v in Counter(re.sub(r"\d+", "N", why.split("\n")[0])[:100] for _, _, why, _ in probleme).most_common():
            print(f"{v:7}  {k}")
    for n, name, why, data in probleme[:20]:
        f = fail / f"{n:06d}-{name}.ibv"
        f.write_bytes(data)
        print(f"\n{f.relative_to(ROOT)}: {why}")
    if len(probleme) > 20:
        print(f"\n… și încă {len(probleme) - 20}")
    sys.exit(1 if probleme else 0)


if __name__ == "__main__":
    main()
