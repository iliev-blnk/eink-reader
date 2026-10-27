"""Tests for the reference codec.  python3 -m unittest -v test_ibv  (from tools/converter)"""
import pathlib
import struct
import unittest

import ibv
from ibv import Book, Record, TocEntry, Cover, FormatError

TEXTS = pathlib.Path(__file__).resolve().parents[2] / "docs" / "mockup" / "texts"


def sample() -> Book:
    t = "Ce mai «faci»?"
    i = t.encode().index("«".encode())
    return Book(
        {"title": "Exemplu", "language": "ro", "author": "Anonim"},
        [
            Record(ibv.HEADING, "Capitolul I", ibv.ALIGN_CENTER),
            Record(ibv.PARA, "Stau câ\u00adteo\u00adda\u00adtă și-mi aduc a\u00admin\u00adte.", ibv.NO_INDENT),
            Record(ibv.PARA, t, 0, [(i, len("«faci»".encode()), ibv.ITALIC)]),
            Record(ibv.BREAK, "", ibv.BREAK_ORNAMENT),
            Record(ibv.VERSE, "A fost odată ca-n povești,"),
        ],
        [TocEntry(0, 1, "Capitolul I")],
    )


class RoundTrip(unittest.TestCase):
    def test_records_survive(self):
        b = sample()
        for compress in (False, True):
            p = ibv.validate(ibv.write(b, compress=compress))
            self.assertEqual(list(p.records()), b.records)
            self.assertEqual(p.meta, b.meta)
            self.assertEqual(p.toc, b.toc)

    def test_book_id_ignores_blocking_and_compression(self):
        b = sample()
        ids = {ibv.parse(ibv.write(b, t, c)).book_id for t in (16, 64, 16384) for c in (False, True)}
        self.assertEqual(len(ids), 1)

    def test_book_id_changes_with_content(self):
        a, b = sample(), sample()
        b.records[1].text = b.records[1].text.replace("\u00ad", "")
        self.assertNotEqual(ibv.parse(ibv.write(a)).book_id, ibv.parse(ibv.write(b)).book_id)

    def test_cover(self):
        b = sample()
        b.cover = Cover(10, 3, bytes([0xFF, 0xC0, 0x00, 0x00, 0xAA, 0x80]))
        p = ibv.validate(ibv.write(b))
        self.assertEqual(p.cover, b.cover)

    def test_small_blocks_respect_whole_records(self):
        p = ibv.validate(ibv.write(sample(), block_target=20))
        self.assertGreater(len(p.blocks), 1)
        self.assertEqual(len(list(p.records())), 5)

    def test_varint(self):
        for n in (0, 1, 127, 128, 16383, 16384, 2**32 - 1):
            enc = ibv.uleb(n)
            self.assertEqual(ibv.read_uleb(enc, 0), (n, len(enc)))


class Rejects(unittest.TestCase):
    def test_every_corrupted_byte_is_caught(self):
        data = ibv.write(sample(), compress=True)
        for pos in range(len(data)):
            bad = bytearray(data)
            bad[pos] ^= 0x01
            with self.assertRaises(FormatError, msg=f"flip at byte {pos} not detected"):
                ibv.validate(bytes(bad))

    def test_truncation(self):
        data = ibv.write(sample())
        for cut in (0, 10, 43, 60, len(data) - 1):
            with self.assertRaises(FormatError):
                ibv.validate(data[:cut])

    def test_bad_records(self):
        cases = [
            Record(ibv.PARA, ""),
            Record(ibv.PARA, " leading"),
            Record(ibv.PARA, "two  spaces"),
            Record(ibv.PARA, "tab\there"),
            Record(ibv.BREAK, "text"),
            Record(ibv.PARA, "abc", 0x80),
            Record(ibv.PARA, "ăbc", 0, [(1, 1, ibv.BOLD)]),     # splits "ă"
            Record(ibv.PARA, "abcdef", 0, [(2, 2, ibv.BOLD), (3, 2, ibv.ITALIC)]),
            Record(ibv.PARA, "abc", 0, [(0, 3, 0)]),
            Record(ibv.BREAK, "", ibv.PAGE),
            Record(ibv.PARA, "abc", ibv.PAGE | ibv.CONT),
        ]
        for r in cases:
            b = sample()
            b.records.append(r)
            with self.assertRaises(FormatError, msg=repr(r)):
                ibv.write(b)

    def test_record_too_big_for_a_block(self):
        b = sample()
        b.records.append(Record(ibv.PARA, "a" * (ibv.MAX_BLOCK_RAW + 1)))
        with self.assertRaises(FormatError):
            ibv.write(b)

    def test_meta_needs_title_and_language(self):
        b = sample()
        del b.meta["language"]
        with self.assertRaises(FormatError):
            ibv.write(b)

    def test_newer_major_version_rejected(self):
        data = bytearray(ibv.write(sample()))
        data[8] = 1
        data[40:44] = struct.pack("<I", ibv.crc32(bytes(data[:40])))
        with self.assertRaises(FormatError):
            ibv.parse(bytes(data))


class LineInput(unittest.TestCase):
    def test_from_lines(self):
        b = ibv.from_lines(["# I", "P Unu doi.", "P Trei.", "S", "V Vers", "S"], {"title": "t", "language": "ro"})
        kinds = [r.kind for r in b.records]
        self.assertEqual(kinds, [ibv.HEADING, ibv.PARA, ibv.PARA, ibv.BREAK, ibv.VERSE])
        self.assertTrue(b.records[1].flags & ibv.NO_INDENT)
        self.assertFalse(b.records[2].flags & ibv.NO_INDENT)

    def test_long_paragraph_splits_with_cont(self):
        b = ibv.from_lines(["P " + " ".join(["cuvânt"] * 3000)], {"title": "t", "language": "ro"})
        self.assertGreater(len(b.records), 1)
        self.assertTrue(all(r.flags & ibv.CONT for r in b.records[1:]))
        self.assertEqual(" ".join(r.text for r in b.records), " ".join(["cuvânt"] * 3000))

    @unittest.skipUnless(TEXTS.exists(), "mockup texts not present")
    def test_mockup_books(self):
        for path in sorted(TEXTS.glob("*.hy.txt")):
            with self.subTest(book=path.name):
                b = ibv.from_lines(path.read_text(encoding="utf-8").splitlines(),
                                   {"title": path.stem, "language": "ro"})
                p = ibv.validate(ibv.write(b))
                self.assertEqual(list(p.records()), b.records)


if __name__ == "__main__":
    unittest.main()
