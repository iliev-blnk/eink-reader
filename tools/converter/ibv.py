"""Cititorul și scriitorul de referință pentru formatul de carte (spec/book-format.md, v0.1).

Modulul ăsta e forma executabilă a specificației: convertorul scrie cărțile cu
el, validatorul le verifică cu el, iar exemplul lucrat din specificație e generat
de el. Când cele două nu sunt de acord, se repară cel greșit și se notează în
jurnalul de schimbări al specificației.

    python3 ibv.py from-lines <in.txt> <out.ibv> --title T --lang ro [--author A]
    python3 ibv.py validate <book.ibv>...
    python3 ibv.py dump <book.ibv> [--records N]

`from-lines` citește formatul pe rânduri al machetei ("# titlu", "P text", "V vers",
"S" separator de strofă). E un înlocuitor până la intrările reale TXT/EPUB (CONV-1/2).
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import unicodedata
import zlib
from dataclasses import dataclass, field

MAGIC = b"\x89IBV\r\n\x1a\n"
VERSION = (0, 1)
HEADER_SIZE = 44
DIR_ENTRY_SIZE = 16
BLOCK_ENTRY_SIZE = 24
MAX_BLOCK_RAW = 32768          # = fereastra DEFLATE, deci ajunge un singur buffer
DEFAULT_BLOCK_TARGET = 16384

# Tipurile înregistrărilor
PARA, HEADING, VERSE, BREAK, QUOTE = range(5)
KIND_NAMES = {PARA: "PARA", HEADING: "HEADING", VERSE: "VERSE", BREAK: "BREAK", QUOTE: "QUOTE"}

# Octetul de indicatori
ALIGN_MASK = 0x03              # 0 = implicit pentru tip, 1 stânga, 2 centru, 3 dreapta
ALIGN_DEFAULT, ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT = range(4)
NO_INDENT = 0x04
CONT = 0x08                    # continuă înregistrarea precedentă (împărțită de convertor)
LEVEL_SHIFT = 4                # HEADING: biții 4-5 = nivelul - 1
LEVEL_MASK = 0x30
BREAK_ORNAMENT = 0x04          # BREAK refolosește bitul 2: se desenează "* * *" în loc de un rând gol
PAGE = 0x40                    # începe o pagină nouă (un capitol); niciodată pe BREAK
FLAGS_RESERVED = 0x80

# Biții de stil dintr-un interval
BOLD, ITALIC = 0x01, 0x02
STYLE_KNOWN = BOLD | ITALIC

# Cheile META
META_KEYS = {1: "title", 2: "author", 3: "language", 4: "source", 5: "converter",
             6: "converted", 7: "year", 8: "description"}
META_IDS = {v: k for k, v in META_KEYS.items()}

SHY, NBSP, LSEP, ZWSP = "\u00ad", "\u00a0", "\u2028", "\u200b"


class FormatError(Exception):
    pass


@dataclass
class Record:
    kind: int
    text: str = ""
    flags: int = 0
    runs: list[tuple[int, int, int]] = field(default_factory=list)  # (octetul de început, lungimea în octeți, stilul)

    @property
    def level(self) -> int:
        return ((self.flags & LEVEL_MASK) >> LEVEL_SHIFT) + 1


@dataclass
class TocEntry:
    para: int
    level: int
    title: str


@dataclass
class Cover:
    width: int
    height: int
    bits: bytes                 # rânduri completate la octeți întregi, MSB = cel mai din stânga, 1 = alb


@dataclass
class Book:
    meta: dict[str, str]
    records: list[Record]
    toc: list[TocEntry] = field(default_factory=list)
    cover: Cover | None = None


# ---------------------------------------------------------------- primitive

def uleb(n: int) -> bytes:
    if n < 0:
        raise ValueError("varint-ul trebuie să fie fără semn")
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def read_uleb(buf: bytes, pos: int) -> tuple[int, int]:
    n = shift = 0
    for i in range(5):                       # valorile sunt < 2^32, deci cel mult 5 octeți
        if pos >= len(buf):
            raise FormatError("varint-ul trece de sfârșitul blocului")
        b = buf[pos]
        pos += 1
        n |= (b & 0x7F) << shift
        if not b & 0x80:
            return n, pos
        shift += 7
    raise FormatError("varint mai lung de 5 octeți")


def crc32(b: bytes) -> int:
    return zlib.crc32(b) & 0xFFFFFFFF


def deflate_raw(b: bytes) -> bytes:
    c = zlib.compressobj(9, zlib.DEFLATED, -15)   # DEFLATE brut, fără antet zlib
    return c.compress(b) + c.flush()


def inflate_raw(b: bytes, raw_len: int) -> bytes:
    d = zlib.decompressobj(-15)
    out = d.decompress(b, raw_len + 1)
    if len(out) != raw_len or not d.eof or d.unused_data:
        raise FormatError(f"blocul se decomprimă la {len(out)} octeți, se așteptau {raw_len}")
    return out


# ---------------------------------------------------------------- înregistrări

def check_text(text: str, where: str) -> None:
    for ch in text:
        o = ord(ch)
        if o < 0x20 or 0x7F <= o < 0xA0:
            raise FormatError(f"{where}: caracter de control U+{o:04X} în text")
    if text != text.strip(" ") or "  " in text:
        raise FormatError(f"{where}: spații la început, la sfârșit sau dublate")
    if not unicodedata.is_normalized("NFC", text):
        raise FormatError(f"{where}: textul nu e normalizat NFC")


def encode_record(r: Record) -> bytes:
    text = r.text.encode("utf-8")
    out = bytearray([r.kind, r.flags])
    out += uleb(len(text)) + uleb(len(r.runs))
    for start, length, style in r.runs:
        out += uleb(start) + uleb(length) + bytes([style])
    return bytes(out + text)


def decode_record(buf: bytes, pos: int) -> tuple[Record, int]:
    if pos + 2 > len(buf):
        raise FormatError("antetul înregistrării trece de sfârșitul blocului")
    kind, flags = buf[pos], buf[pos + 1]
    pos += 2
    tlen, pos = read_uleb(buf, pos)
    nruns, pos = read_uleb(buf, pos)
    runs = []
    for _ in range(nruns):
        s, pos = read_uleb(buf, pos)
        n, pos = read_uleb(buf, pos)
        if pos >= len(buf):
            raise FormatError("un run trece de sfârșitul blocului")
        runs.append((s, n, buf[pos]))
        pos += 1
    if pos + tlen > len(buf):
        raise FormatError("textul trece de sfârșitul blocului")
    try:
        text = buf[pos:pos + tlen].decode("utf-8")
    except UnicodeDecodeError as e:
        raise FormatError(f"UTF-8 invalid în text: {e}") from None
    return Record(kind, text, flags, runs), pos + tlen


def validate_record(r: Record, where: str) -> None:
    if r.flags & FLAGS_RESERVED:
        raise FormatError(f"{where}: bitul rezervat 7 al indicatorilor e setat")
    if r.kind == BREAK and (r.text or r.runs):
        raise FormatError(f"{where}: BREAK are text")
    if r.flags & PAGE and (r.kind == BREAK or r.flags & CONT):
        raise FormatError(f"{where}: PAGE pe o înregistrare BREAK sau CONT")
    if r.kind != BREAK and not r.text:
        raise FormatError(f"{where}: text gol")
    check_text(r.text, where)
    tb = r.text.encode("utf-8")
    end = 0
    for s, n, style in r.runs:
        if n == 0 or s < end or s + n > len(tb):
            raise FormatError(f"{where}: run-ul ({s},{n}) e gol, se suprapune, nu e sortat sau iese din text")
        for edge in (s, s + n):
            if edge < len(tb) and (tb[edge] & 0xC0) == 0x80:
                raise FormatError(f"{where}: marginea de run {edge} taie o secvență UTF-8")
        if style == 0:
            raise FormatError(f"{where}: run fără stil (textul obișnuit rămâne neacoperit)")
        end = s + n


# ---------------------------------------------------------------- scriitorul

def plan_blocks(encoded: list[bytes], target: int) -> list[tuple[int, int]]:
    """Greedy: fiecare bloc se umple până la `target` octeți bruți cu înregistrări întregi."""
    if not 0 < target <= MAX_BLOCK_RAW:
        raise ValueError(f"mărimea țintă a blocului trebuie să fie între 1 și {MAX_BLOCK_RAW}")
    blocks, first, size = [], 0, 0
    for i, rec in enumerate(encoded):
        if len(rec) > MAX_BLOCK_RAW:
            raise FormatError(f"înregistrarea {i} are {len(rec)} octeți; mai întâi trebuie împărțită cu CONT")
        if size and size + len(rec) > target:
            blocks.append((first, i))
            first, size = i, 0
        size += len(rec)
    if size or not blocks:
        blocks.append((first, len(encoded)))
    return blocks


def section_meta(meta: dict[str, str]) -> bytes:
    for req in ("title", "language"):
        if not meta.get(req):
            raise FormatError(f"META trebuie să aibă '{req}'")
    out = bytearray(struct.pack("<H", len(meta)))
    for key, val in meta.items():
        if key not in META_IDS:
            raise FormatError(f"cheie META necunoscută '{key}'")
        b = val.encode("utf-8")
        out += struct.pack("<BH", META_IDS[key], len(b)) + b
    return bytes(out)


def section_toc(toc: list[TocEntry]) -> bytes:
    out = bytearray(struct.pack("<H", len(toc)))
    for e in toc:
        b = e.title.encode("utf-8")
        out += struct.pack("<IBBH", e.para, e.level, 0, len(b)) + b
    return bytes(out)


def section_cover(c: Cover) -> bytes:
    stride = (c.width + 7) // 8
    if len(c.bits) != stride * c.height:
        raise FormatError("mărimea bitmapului copertei nu se potrivește cu lățime x înălțime")
    packed = deflate_raw(c.bits)
    method, data = (1, packed) if len(packed) < len(c.bits) else (0, c.bits)
    return struct.pack("<HHBBI", c.width, c.height, method, 0, len(c.bits)) + data


def book_id(meta_payload: bytes, encoded: list[bytes]) -> bytes:
    h = hashlib.sha256(meta_payload)
    for rec in encoded:
        h.update(rec)
    return h.digest()[:8]


def write(book: Book, block_target: int = DEFAULT_BLOCK_TARGET, compress: bool = True) -> bytes:
    for i, r in enumerate(book.records):
        validate_record(r, f"înregistrarea {i}")
    encoded = [encode_record(r) for r in book.records]
    meta = section_meta(book.meta)

    # Blocurile: brut = înregistrările lipite; stocat = DEFLATE dacă e mai mic.
    blocks, stored_blocks, text_start = [], [], 0
    for first, end in plan_blocks(encoded, block_target):
        raw = b"".join(encoded[first:end])
        packed = deflate_raw(raw) if compress else raw
        method, stored = (1, packed) if compress and len(packed) < len(raw) else (0, raw)
        blocks.append((len(stored), len(raw), method, first, text_start, crc32(raw)))
        stored_blocks.append(stored)
        text_start += sum(len(r.text.encode("utf-8")) for r in book.records[first:end])

    sections = [(b"META", meta), (b"TOC ", section_toc(book.toc))]
    blks_index = len(sections)
    sections.append((b"BLKS", b""))                      # completat când se știu deplasamentele
    if book.cover:
        sections.append((b"COVR", section_cover(book.cover)))
    sections.append((b"TEXT", b"".join(stored_blocks)))

    # Se știe lungimea fiecărei secțiuni în afară de BLKS, a cărei mărime depinde doar de număr.
    blks_len = 4 + BLOCK_ENTRY_SIZE * len(blocks)
    offset = HEADER_SIZE + DIR_ENTRY_SIZE * len(sections)
    offsets = []
    for i, (tag, payload) in enumerate(sections):
        offsets.append(offset)
        offset += blks_len if i == blks_index else len(payload)

    text_off = offsets[-1]
    blks = bytearray(struct.pack("<I", len(blocks)))
    for stored_len, raw_len, method, first, tstart, crc in blocks:
        blks += struct.pack("<IIHBBIII", text_off, stored_len, raw_len, method, 0, first, tstart, crc)
        text_off += stored_len
    sections[blks_index] = (b"BLKS", bytes(blks))

    directory = b"".join(struct.pack("<4sIII", tag, off, len(p), crc32(p))
                         for (tag, p), off in zip(sections, offsets))
    head = MAGIC + struct.pack("<BBHI8sIIHHI", VERSION[0], VERSION[1], HEADER_SIZE, 0,
                               book_id(meta, encoded), len(book.records), text_start,
                               len(sections), 0, crc32(directory))
    head += struct.pack("<I", crc32(head))
    return head + directory + b"".join(p for _, p in sections)


# ---------------------------------------------------------------- cititorul

@dataclass
class BlockEntry:
    offset: int
    stored_len: int
    raw_len: int
    method: int
    first_para: int
    text_start: int
    crc: int


@dataclass
class Parsed:
    version: tuple[int, int]
    book_id: bytes
    paragraph_count: int
    text_bytes: int
    sections: dict[str, tuple[int, int, int]]   # etichetă -> (deplasament, lungime, crc)
    meta: dict[str, str]
    toc: list[TocEntry]
    blocks: list[BlockEntry]
    cover: Cover | None
    data: bytes

    def block_raw(self, i: int) -> bytes:
        b = self.blocks[i]
        stored = self.data[b.offset:b.offset + b.stored_len]
        if b.method == 0:
            raw = stored
        elif b.method == 1:
            raw = inflate_raw(stored, b.raw_len)
        else:
            raise FormatError(f"blocul {i}: metodă necunoscută {b.method}")
        if len(raw) != b.raw_len:
            raise FormatError(f"blocul {i}: lungimea decomprimată {len(raw)}, se aștepta {b.raw_len}")
        if crc32(raw) != b.crc:
            raise FormatError(f"blocul {i}: CRC greșit")
        return raw

    def block_records(self, i: int) -> list[Record]:
        raw = self.block_raw(i)
        end = self.blocks[i + 1].first_para if i + 1 < len(self.blocks) else self.paragraph_count
        out, pos = [], 0
        for _ in range(end - self.blocks[i].first_para):
            r, pos = decode_record(raw, pos)
            out.append(r)
        if pos != len(raw):
            raise FormatError(f"blocul {i}: rămân {len(raw) - pos} octeți după ultima înregistrare")
        return out

    def records(self):
        for i in range(len(self.blocks)):
            yield from self.block_records(i)


def parse(data: bytes) -> Parsed:
    """Citește antetul, directorul și secțiunile mici. Blocurile se citesc la cerere."""
    if len(data) < HEADER_SIZE or data[:8] != MAGIC:
        raise FormatError("nu e un fișier de carte (cod magic greșit)")
    major, minor, hsize, flags, bid, npara, tbytes, nsec, _res, dcrc = \
        struct.unpack_from("<BBHI8sIIHHI", data, 8)
    if major != VERSION[0]:
        raise FormatError(f"versiunea {major}.{minor} nu e suportată")
    if hsize < HEADER_SIZE or hsize > len(data):
        raise FormatError("mărime de antet greșită")
    (hcrc,) = struct.unpack_from("<I", data, 40)
    if crc32(data[:40]) != hcrc:
        raise FormatError("CRC-ul antetului e greșit")
    if hsize + nsec * DIR_ENTRY_SIZE > len(data):
        raise FormatError("directorul trece de sfârșitul fișierului")
    if crc32(data[hsize:hsize + nsec * DIR_ENTRY_SIZE]) != dcrc:
        raise FormatError("CRC-ul directorului e greșit")

    sections = {}
    for i in range(nsec):
        pos = hsize + i * DIR_ENTRY_SIZE
        if pos + DIR_ENTRY_SIZE > len(data):
            raise FormatError("directorul trece de sfârșitul fișierului")
        tag, off, length, crc = struct.unpack_from("<4sIII", data, pos)
        name = tag.decode("ascii", "replace")
        if name in sections:
            raise FormatError(f"secțiunea {name} apare de două ori")
        if off + length > len(data):
            raise FormatError(f"secțiunea {name} trece de sfârșitul fișierului")
        sections[name] = (off, length, crc)
    for req in ("META", "BLKS", "TEXT"):
        if req not in sections:
            raise FormatError(f"lipsește secțiunea {req}")

    def payload(name: str, check: bool = True) -> bytes:
        off, length, crc = sections[name]
        p = data[off:off + length]
        if check and crc32(p) != crc:
            raise FormatError(f"secțiunea {name}: CRC greșit")
        return p

    m = payload("META")
    meta, seen, pos = {}, set(), 2
    for _ in range(struct.unpack_from("<H", m)[0]):
        key, length = struct.unpack_from("<BH", m, pos)
        pos += 3
        if key in seen:
            raise FormatError(f"cheia META {key} apare de două ori")
        seen.add(key)
        val = m[pos:pos + length].decode("utf-8")
        pos += length
        if key in META_KEYS:                      # cheile necunoscute se sar
            meta[META_KEYS[key]] = val

    toc = []
    if "TOC " in sections:
        t, pos = payload("TOC "), 2
        for _ in range(struct.unpack_from("<H", t)[0]):
            para, level, _r, length = struct.unpack_from("<IBBH", t, pos)
            pos += 8
            toc.append(TocEntry(para, level, t[pos:pos + length].decode("utf-8")))
            pos += length

    b = payload("BLKS")
    blocks = [BlockEntry(*(lambda v: (v[0], v[1], v[2], v[3], v[5], v[6], v[7]))(
                  struct.unpack_from("<IIHBBIII", b, 4 + i * BLOCK_ENTRY_SIZE)))
              for i in range(struct.unpack_from("<I", b)[0])]

    cover = None
    if "COVR" in sections:
        c = payload("COVR")
        w, h, method, _r, raw_len = struct.unpack_from("<HHBBI", c)
        bits = c[10:] if method == 0 else inflate_raw(c[10:], raw_len)
        cover = Cover(w, h, bits)

    return Parsed((major, minor), bid, npara, tbytes, sections, meta, toc, blocks, cover, data)


def validate(data: bytes) -> Parsed:
    """Tot ce verifică parse(), plus fiecare regulă din secțiunea despre validator a specificației."""
    p = parse(data)
    off, length, crc = p.sections["TEXT"]
    if crc32(data[off:off + length]) != crc:
        raise FormatError("secțiunea TEXT: CRC greșit")
    for need in ("title", "language"):
        if not p.meta.get(need):
            raise FormatError(f"META nu are {need}")

    expect_off, expect_para, expect_text = off, 0, 0
    if not p.blocks:
        raise FormatError("niciun bloc")
    meta_payload = data[p.sections["META"][0]:p.sections["META"][0] + p.sections["META"][1]]
    h = hashlib.sha256(meta_payload)
    for i, b in enumerate(p.blocks):
        if b.offset != expect_off:
            raise FormatError(f"blocul {i}: offset {b.offset}, se aștepta {expect_off} (gol sau suprapunere)")
        if b.first_para != expect_para:
            raise FormatError(f"blocul {i}: first_para {b.first_para}, se aștepta {expect_para}")
        if b.text_start != expect_text:
            raise FormatError(f"blocul {i}: text_start {b.text_start}, se aștepta {expect_text}")
        if b.raw_len > MAX_BLOCK_RAW:
            raise FormatError(f"blocul {i}: raw_len {b.raw_len} > {MAX_BLOCK_RAW}")
        recs = p.block_records(i)
        if not recs:
            raise FormatError(f"blocul {i}: nicio înregistrare")
        for j, r in enumerate(recs):
            validate_record(r, f"paragraful {b.first_para + j}")
            h.update(encode_record(r))
            expect_text += len(r.text.encode("utf-8"))
        expect_off += b.stored_len
        expect_para += len(recs)
    if expect_off != off + length:
        raise FormatError("TEXT are octeți spre care nu arată niciun bloc")
    if expect_para != p.paragraph_count:
        raise FormatError(f"blocurile au {expect_para} paragrafe, antetul spune {p.paragraph_count}")
    if expect_text != p.text_bytes:
        raise FormatError(f"blocurile au {expect_text} octeți de text, antetul spune {p.text_bytes}")
    if h.digest()[:8] != p.book_id:
        raise FormatError("book_id nu se potrivește cu conținutul")
    last = -1
    for e in p.toc:
        if e.para <= last or e.para >= p.paragraph_count or not 1 <= e.level <= 4:
            raise FormatError(f"intrarea din cuprins '{e.title}' nu e în ordine, iese din carte sau are un nivel greșit")
        last = e.para
    if p.cover and (p.cover.width > 240 or p.cover.height > 416):
        raise FormatError("coperta e mai mare de 240x416")
    return p


# ---------------------------------------------------------------- intrarea pe rânduri (înlocuitor)

def split_long(text: str, limit: int) -> list[str]:
    """Împarte la spații un paragraf al cărui UTF-8 e mai lung decât `limit`."""
    parts, cur = [], ""
    for word in text.split(" "):
        cand = f"{cur} {word}" if cur else word
        if cur and len(cand.encode("utf-8")) > limit:
            parts.append(cur)
            cur = word
        else:
            cur = cand
    parts.append(cur)
    return parts


def from_lines(lines: list[str], meta: dict[str, str]) -> Book:
    records, toc = [], []
    after_heading = False
    for line in lines:
        line = line.rstrip("\n")
        if not line:
            continue
        tag, text = line[0], line[2:].strip()
        if tag == "#":
            toc.append(TocEntry(len(records), 1, text.replace(SHY, "")))
            records.append(Record(HEADING, text, ALIGN_CENTER))
            after_heading = True
        elif tag == "P":
            for k, part in enumerate(split_long(text, 8192)):
                flags = (NO_INDENT if after_heading and k == 0 else 0) | (CONT if k else 0)
                records.append(Record(PARA, part, flags))
            after_heading = False
        elif tag == "V":
            records.append(Record(VERSE, text))
            after_heading = False
        elif tag == "S":
            if records and records[-1].kind not in (BREAK, HEADING):
                records.append(Record(BREAK))
        else:
            raise ValueError(f"etichetă de rând necunoscută {tag!r}")
    while records and records[-1].kind == BREAK:
        records.pop()
    return Book(meta, records, toc)


# ---------------------------------------------------------------- linia de comandă

def dump(p: Parsed, n_records: int) -> str:
    out = [f"versiunea {p.version[0]}.{p.version[1]}  book_id {p.book_id.hex()}  "
           f"paragrafe {p.paragraph_count}  text {p.text_bytes} B  fișier {len(p.data)} B"]
    out.append("secțiuni:")
    for name, (off, length, crc) in p.sections.items():
        out.append(f"  {name}  @{off:<8} {length:>8} B  crc {crc:08x}")
    out.append("meta: " + ", ".join(f"{k}={v!r}" for k, v in p.meta.items()))
    out.append(f"cuprins: {len(p.toc)} intrări")
    for e in p.toc[:10]:
        out.append(f"  [{e.para:>5}] {'  ' * (e.level - 1)}{e.title}")
    raw = sum(b.raw_len for b in p.blocks)
    stored = sum(b.stored_len for b in p.blocks)
    out.append(f"blocuri: {len(p.blocks)}  decomprimat {raw} B  stocat {stored} B  ({stored / raw:.1%})")
    for i, b in enumerate(p.blocks[:8]):
        out.append(f"  #{i:<3} @{b.offset:<8} stocat {b.stored_len:>6}  decomprimat {b.raw_len:>6}  "
                   f"m{b.method}  parag {b.first_para:<5} text {b.text_start:<8} crc {b.crc:08x}")
    if p.cover:
        out.append(f"coperta: {p.cover.width}x{p.cover.height}")
    for i, r in enumerate(p.records()):
        if i >= n_records:
            break
        flags = f"{r.flags:08b}"
        text = r.text.replace(SHY, "·").replace(NBSP, "⍽")
        out.append(f"  {i:>5} {KIND_NAMES.get(r.kind, r.kind):<7} {flags} runs={r.runs} {text[:70]!r}")
    return "\n".join(out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="ibv", description="Codecul de referință IBV: scriere din formatul pe rânduri, validare, structură.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("from-lines", help="scrie un .ibv din formatul pe rânduri (vezi from_lines)")
    a.add_argument("src")
    a.add_argument("dst")
    a.add_argument("--title", required=True)
    a.add_argument("--author")
    a.add_argument("--lang", required=True)
    a.add_argument("--block", type=int, default=DEFAULT_BLOCK_TARGET)
    a.add_argument("--stored", action="store_true", help="fără compresie")
    v = sub.add_parser("validate", help="verifică fișiere .ibv")
    v.add_argument("files", nargs="+")
    d = sub.add_parser("dump", help="afișează structura unui fișier .ibv")
    d.add_argument("file")
    d.add_argument("--records", type=int, default=5)
    args = ap.parse_args(argv)

    if args.cmd == "from-lines":
        meta = {"title": args.title, "language": args.lang, "converter": "ibv.py 0.1"}
        if args.author:
            meta["author"] = args.author
        with open(args.src, encoding="utf-8") as f:
            book = from_lines(f.readlines(), meta)
        data = write(book, args.block, not args.stored)
        validate(data)
        with open(args.dst, "wb") as f:
            f.write(data)
        print(f"{args.dst}: {len(book.records)} paragrafe, {len(data)} B")
    elif args.cmd == "validate":
        bad = 0
        for path in args.files:
            try:
                p = validate(open(path, "rb").read())
                print(f"{path}: OK ({p.paragraph_count} paragrafe, {len(p.blocks)} blocuri)")
            except FormatError as e:
                print(f"{path}: INVALID: {e}")
                bad += 1
        return 1 if bad else 0
    elif args.cmd == "dump":
        print(dump(parse(open(args.file, "rb").read()), args.records))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
