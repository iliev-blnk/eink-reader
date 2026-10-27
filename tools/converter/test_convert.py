"""Testele convertorului (CONV-1..7, 9, 10).

    cd tools/converter && python3 -m unittest test_convert

Intrările EPUB și FB2 se construiesc aici, în memorie, deci fiecare caz spune
exact ce primește. Testul pe corpus de la final convertește ce se află în
corpus/files/ și se sare când dosarul e gol.
"""
from __future__ import annotations

import base64
import contextlib
import io
import pathlib
import re
import tempfile
import unittest
import zipfile

import numpy as np
from PIL import Image

import convert
import cover
import doc
import epub
import fb2
import glyphs
import ibv
import textnorm
import txt
from hyphenate import Hyphenator, should_hyphenate
from textnorm import LSEP, NBSP, SHY, Styled

CORPUS = pathlib.Path(__file__).resolve().parents[2] / "corpus" / "files"


def styled(*parts: tuple[str, int]) -> Styled:
    st = Styled()
    for text, style in parts:
        st.append(text, style)
    return st


def build(b: doc.Builder, lang: str = "ro", **kw) -> tuple[ibv.Book, ibv.Parsed]:
    """finish() + write + validate: fiecare carte de test trebuie să fie și un fișier valid."""
    book, rep = doc.finish(b, {"title": "T", "language": lang}, **kw)
    return book, ibv.validate(ibv.write(book))


def texts(book: ibv.Book, kind: int | None = None) -> list[str]:
    return [r.text.replace(SHY, "") for r in book.records if kind is None or r.kind == kind]


# ---------------------------------------------------------------- normalizarea

class TestNormalize(unittest.TestCase):
    def test_quotes_per_language(self):
        self.assertEqual(textnorm.plain('El zise: "Vino"', "ro"), "El zise: „Vino”")
        self.assertEqual(textnorm.plain('"Привет"', "ru"), "«Привет»")
        self.assertEqual(textnorm.plain('He said "it\'s fine"', "en"), "He said “it’s fine”")
        self.assertEqual(textnorm.plain(",,Bine\" a zis", "ro"), "„Bine” a zis")
        self.assertEqual(textnorm.plain("the '90s", "en"), "the ’90s")

    def test_dashes_ellipsis_and_ties(self):
        self.assertEqual(textnorm.plain("- Ce faci? - intreba el...", "ro"),
                         f"— Ce faci?{NBSP}— intreba el…")
        self.assertEqual(textnorm.plain("well -- really", "en"), f"well{NBSP}— really")
        self.assertEqual(textnorm.plain("yes - no", "en"), f"yes{NBSP}– no")

    def test_spaces(self):
        self.assertEqual(textnorm.plain("  ceva ,\t te   rog .", "ro"), "ceva, te rog.")
        self.assertEqual(textnorm.plain("100 000", "ro"), f"100{NBSP}000")
        self.assertEqual(textnorm.plain("of .5 inch", "en"), "of .5 inch")
        self.assertEqual(textnorm.plain("Si ... apoi", "ro"), "Si … apoi")

    def test_romanian_letters(self):
        self.assertEqual(textnorm.plain("Ştiinţă şi ţară", "ro"), "Știință și țară")
        self.assertEqual(textnorm.plain("ªtiinþã ºi þarã", "ro", ro_legacy=True), "Știință și țară")
        sample = "ºi þarã " * 30
        self.assertTrue(textnorm.detect_ro_legacy(sample))
        self.assertFalse(textnorm.detect_ro_legacy("și țară " * 30))

    def test_nfc_and_dropped_characters(self):
        self.assertEqual(textnorm.plain("café", "en"), "café")
        self.assertEqual(textnorm.plain("a­b‍c﻿d", "en"), "abcd")

    def test_styles_follow_the_text(self):
        st = textnorm.normalize(styled(("Zise ", 0), ('"da"...', ibv.ITALIC), (" apoi", 0)), "ro")
        self.assertEqual(st.text, "Zise „da”… apoi")
        self.assertEqual(len(st.styles), len(st.text))
        italic = "".join(c for c, s in zip(st.text, st.styles) if s & ibv.ITALIC)
        self.assertEqual(italic, "„da”…")


# ---------------------------------------------------------------- despărțirea în silabe

class TestHyphenate(unittest.TestCase):
    def check(self, lang, cases):
        hy = Hyphenator.for_language(lang)
        for word, want in cases:
            self.assertEqual(hy.hyphenate(word, "-"), want, word)

    def test_romanian(self):
        self.check("ro", [("copilărie", "co-pi-lă-rie"), ("înțelepciune", "în-țe-lep-ciu-ne")])

    def test_russian(self):
        self.check("ru", [("мечтатель", "меч-та-тель"), ("здравствуйте", "здрав-ствуй-те")])

    def test_english_and_exceptions(self):
        self.check("en", [("hyphenation", "hy-phen-ation"), ("associate", "as-so-ciate")])

    def test_minimums(self):
        for lang in ("ro", "ru", "en"):
            hy = Hyphenator.for_language(lang)
            for word in ("abecedar", "алфавит", "alphabet", "ochi", "iar"):
                pts = hy.points(word)
                self.assertTrue(all(hy.left <= p <= len(word) - hy.right for p in pts), (lang, word, pts))

    def test_skips(self):
        self.assertFalse(should_hyphenate("NATO"))
        self.assertFalse(should_hyphenate("iPhone"))
        self.assertTrue(should_hyphenate("Bucureşti"))
        self.assertIsNone(Hyphenator.for_language("xx"))
        self.assertIsNotNone(Hyphenator.for_language("ro-MD"))


# ---------------------------------------------------------------- codificări + TXT

RO = ("Stau câteodată și-mi aduc aminte ce vremi și ce oameni mai erau în părțile noastre "
      "pe când începusem și eu, drăgăliță-Doamne, a mă ridica băiețaș la casa părinților mei. ") * 8
RU = ("Несколько лет тому назад в одном из своих поместий жил старинный русский барин, "
      "Кирила Петрович Троекуров. Его богатство, знатный род и связи давали ему большой вес. ") * 8


class TestEncoding(unittest.TestCase):
    def test_utf8_and_bom(self):
        self.assertEqual(txt.decode(RO.encode()), (RO, "utf-8"))
        self.assertEqual(txt.decode(b"\xef\xbb\xbf" + RO.encode())[0], RO)

    def test_russian_code_pages(self):
        for enc in ("cp1251", "koi8_r"):
            text, got = txt.decode(RU.encode(enc))
            self.assertEqual((text, got), (RU, enc))

    def test_romanian_code_pages(self):
        cedilla = RO.replace("ș", "ş").replace("ț", "ţ")      # CP1250 nu are litere cu virgulă dedesubt
        text, got = txt.decode(cedilla.encode("cp1250"))
        self.assertEqual(text, cedilla)
        self.assertIn(got, ("cp1250", "iso8859_2"))
        # ș/ț din ISO-8859-16 sunt octet cu octet ş/ţ din CP1250, iar restul românei
        # e la fel în amândouă: oricare citire se normalizează în același text
        text, got = txt.decode(RO.encode("iso8859_16"))
        self.assertIn(got, ("iso8859_16", "cp1250"))
        self.assertEqual(textnorm.plain(text, "ro"), textnorm.plain(RO, "ro"))


def txt_book(text: str, lang: str = "ro") -> tuple[ibv.Book, dict]:
    b, meta, info = txt.parse(text.encode(), "t.txt")
    meta["language"] = lang
    book, _ = doc.finish(b, meta)
    ibv.validate(ibv.write(book))
    return book, info


class TestTxt(unittest.TestCase):
    PROSE = "Prima frază a paragrafului, destul de lungă pentru a fi proză adevărată și nu vers."

    def test_blank_lines(self):
        text = "\n\n".join(f"{self.PROSE}\ncontinuă pe rândul doi. {i}" for i in range(30))
        book, info = txt_book(text)
        self.assertEqual(info["paragraphs"], "blank")
        self.assertEqual(len(book.records), 30)
        self.assertIn("proză adevărată și nu vers. continuă", texts(book)[0])

    def test_indented_first_lines(self):
        text = "\n".join(f"    {self.PROSE}\ncontinuă aici {i}." for i in range(30))
        book, info = txt_book(text)
        self.assertEqual(info["paragraphs"], "indent")
        self.assertEqual(len(book.records), 30)

    def test_one_line_per_paragraph(self):
        text = "\n".join(self.PROSE * 3 for _ in range(30))
        book, info = txt_book(text)
        self.assertEqual(info["paragraphs"], "line")
        self.assertEqual(len(book.records), 30)

    def test_word_split_at_line_end_rejoins(self):
        self.assertEqual(txt.join(["s-a dus dintr-", "o casă"]), "s-a dus dintr-o casă")
        self.assertEqual(txt.join(["un rând", "alt rând"]), "un rând alt rând")

    def test_headings_breaks_and_toc(self):
        para = "\n\n".join([self.PROSE] * 12)
        text = f"PARTEA ÎNTÂI\n\nCAPITOLUL I\n\n{para}\n\n* * *\n\n{para}\n\nCAPITOLUL II\n\n{para}"
        book, _ = txt_book(text)
        kinds = [r.kind for r in book.records]
        self.assertEqual(kinds[:3], [ibv.HEADING, ibv.HEADING, ibv.PARA])
        self.assertIn(ibv.BREAK, kinds)
        brk = book.records[kinds.index(ibv.BREAK)]
        self.assertTrue(brk.flags & ibv.BREAK_ORNAMENT)
        self.assertEqual([(t.level, t.title) for t in book.toc],
                         [(1, "PARTEA ÎNTÂI"), (2, "CAPITOLUL I"), (2, "CAPITOLUL II")])
        self.assertEqual(book.toc[0].para, 0)
        self.assertEqual(book.toc[1].para, 1)

    def test_verse_block(self):
        prose = "\n\n".join("\n".join([self.PROSE[:70]] * 4) for _ in range(10))
        poem = "Pe lângă plopii fără soț\nAdesea am trecut;\nMă cunoșteau vecinii toți -\nTu nu m-ai cunoscut."
        book, _ = txt_book(f"{prose}\n\n{poem}\n\n{prose}")
        verse = texts(book, ibv.VERSE)
        self.assertEqual(verse[0], "Pe lângă plopii fără soț")
        self.assertEqual(len(verse), 4)

    def test_gutenberg_header(self):
        text = ("The Project Gutenberg eBook of Nuvele\n\nTitle: Nuvele\n\nAuthor: I. L. Caragiale\n\n"
                "Language: Romanian\n\n*** START OF THE PROJECT GUTENBERG EBOOK NUVELE ***\n\n"
                + "\n\n".join([self.PROSE] * 10)
                + "\n\n*** END OF THE PROJECT GUTENBERG EBOOK NUVELE ***\n\nlicence text")
        b, meta, _ = txt.parse(text.encode(), "x.txt")
        self.assertEqual((meta["title"], meta["author"], meta["language"]), ("Nuvele", "I. L. Caragiale", "ro"))
        self.assertNotIn("licence", "".join(it.st.text for it in b.items))

    def test_libru_header_and_marked_headings(self):
        text = (" Александр Сергеевич Пушкин. Дубровский\x15\n " + "-" * 60
                + "\n Текст с собрания сочинений\n " + "-" * 60
                + "\n\n \x14 * ТОМ ПЕРВЫЙ * \x15\n\n \x14ГЛАВА 1.\x15\n\n " + RU + "\n")
        b, meta, info = txt.parse(text.encode("koi8_r"), "d.txt")
        self.assertEqual((meta["author"], meta["source"]), ("Александр Сергеевич Пушкин", "lib.ru"))
        self.assertEqual(textnorm.plain(meta["title"], "ru"), "Дубровский")
        self.assertEqual(info["encoding"], "koi8_r")
        heads = [(it.st.text, it.flags) for it in b.items if it.kind == ibv.HEADING]
        self.assertEqual([h[0] for h in heads], ["ТОМ ПЕРВЫЙ", "ГЛАВА 1."])
        self.assertEqual([t.level for t in b.toc], [1, 2])

    def test_language_guess(self):
        self.assertEqual(txt.guess_language(RO), "ro")
        self.assertEqual(txt.guess_language(RU), "ru")
        self.assertEqual(txt.guess_language("The quick brown fox jumps over the lazy dog. " * 20), "en")


# ---------------------------------------------------------------- EPUB

def png(w: int, h: int, colour="grey", mode="RGB") -> bytes:
    out = io.BytesIO()
    Image.new(mode, (w, h), colour).save(out, "PNG")
    return out.getvalue()


def make_epub(files: dict[str, str | bytes], opf_extra_meta: str = "", spine: str = "",
              manifest: str = "") -> bytes:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml",
                   '<?xml version="1.0"?><container version="1.0" '
                   'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
                   '<rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                   '</rootfiles></container>')
        z.writestr("OEBPS/content.opf", f'''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
 <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
  <dc:title>Amintiri din copilărie</dc:title><dc:creator>Ion Creangă</dc:creator>
  <dc:language>ro</dc:language><dc:date>1892-01-01</dc:date>{opf_extra_meta}
 </metadata>
 <manifest>{manifest}</manifest>
 <spine>{spine}</spine>
</package>''')
        for name, data in files.items():
            z.writestr("OEBPS/" + name, data)
    return out.getvalue()


XHTML = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>x</title><link rel="stylesheet" href="style.css"/></head><body>{}</body></html>'''

CH1 = XHTML.format('''
<h1>Partea I</h1>
<p class="first">Stau câteodată și-mi aduc aminte ce vremi și ce oameni mai erau
în părțile noastre<a epub:type="noteref" href="#n1">1</a>, pe când <span class="it">începusem</span>
și eu a mă ridica băiețaș.</p>
<aside epub:type="footnote" id="n1"><p>Nota de subsol care nu intră în carte.</p></aside>
<hr/>
<div class="poem"><p>Pe lângă plopii fără soț<br/>Adesea am trecut;<br/><br/>Mă cunoșteau vecinii toți<br/>
Tu nu m-ai cunoscut.</p></div>
<p style="display:none">ascuns</p>
<ul><li>unu</li><li>doi</li></ul>
''')
CH2 = XHTML.format('''
<h2 id="cap2">Capitolul II</h2>
<blockquote><p>Un citat cu <b>bold</b> în el.</p></blockquote>
<p>Și iarăși proză, cu un <em>cuvânt</em> înclinat și <i>încă unul</i>.</p>
<p>Strofa fără clasă,<br/>dar cu rânduri scurte,<br/>trei la număr</p>
''')
NAV = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body>
<nav epub:type="toc"><ol>
 <li><a href="ch1.xhtml">Partea I</a><ol><li><a href="ch2.xhtml#cap2">Capitolul II</a></li></ol></li>
</ol></nav></body></html>'''


def sample_epub(cover_png: bytes | None = None) -> bytes:
    manifest = ('<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
                '<item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>'
                '<item id="c2" href="ch2.xhtml" media-type="application/xhtml+xml"/>'
                '<item id="extra" href="extra.xhtml" media-type="application/xhtml+xml"/>'
                '<item id="css" href="style.css" media-type="text/css"/>')
    files = {"nav.xhtml": NAV, "ch1.xhtml": CH1, "ch2.xhtml": CH2, "style.css": ".it { font-style: italic }",
             "extra.xhtml": XHTML.format("<p>Pagina din afara firului.</p>")}
    meta = ""
    if cover_png:
        manifest += '<item id="cov" href="cover.png" media-type="image/png" properties="cover-image"/>'
        files["cover.png"] = cover_png
        meta = '<meta name="cover" content="cov"/>'
    spine = '<itemref idref="c1"/><itemref idref="c2"/><itemref idref="extra" linear="no"/>'
    return make_epub(files, meta, spine, manifest)


class TestEpub(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        b, cls.meta, cls.info = epub.parse(sample_epub(png(300, 500)), "a.epub")
        cls.book, cls.parsed = build(b)

    def test_metadata(self):
        self.assertEqual(self.meta["title"], "Amintiri din copilărie")
        self.assertEqual(self.meta["author"], "Ion Creangă")
        self.assertEqual(self.meta["language"], "ro")
        self.assertEqual(self.meta["year"], "1892")

    def test_structure(self):
        recs = self.book.records
        self.assertEqual((recs[0].kind, texts(self.book)[0]), (ibv.HEADING, "Partea I"))
        self.assertEqual(recs[1].kind, ibv.PARA)
        self.assertTrue(recs[1].flags & ibv.NO_INDENT)
        all_text = " ".join(texts(self.book))
        self.assertNotIn("Nota de subsol", all_text)        # corpul notei
        self.assertNotIn("noastre1", all_text)              # semnul notei
        self.assertNotIn("ascuns", all_text)                # display:none
        self.assertNotIn("afara firului", all_text)         # linear="no"
        self.assertIn("• unu", all_text)
        quote = next(r for r in recs if r.kind == ibv.QUOTE)
        self.assertEqual(quote.text.replace(SHY, ""), "Un citat cu bold în el.")
        self.assertEqual(quote.runs[0][2], ibv.BOLD)

    def test_hr_is_an_ornament_break(self):
        brk = next(r for r in self.book.records if r.kind == ibv.BREAK)
        self.assertTrue(brk.flags & ibv.BREAK_ORNAMENT)

    def test_poem_lines_and_stanza_break(self):
        recs = self.book.records
        i = next(k for k, r in enumerate(recs) if r.text == "Pe lângă plopii fără soț")
        self.assertEqual([r.kind for r in recs[i:i + 6]],
                         [ibv.VERSE, ibv.VERSE, ibv.BREAK, ibv.VERSE, ibv.VERSE, ibv.BREAK])

    def test_br_paragraph_of_short_lines_becomes_verse(self):
        self.assertIn("dar cu rânduri scurte,", texts(self.book, ibv.VERSE))

    def test_css_class_and_inline_styles(self):
        para = self.book.records[1]
        start, length, style = para.runs[0]
        word = para.text.encode()[start:start + length].decode().replace(SHY, "")
        self.assertEqual((word, style), ("începusem", ibv.ITALIC))
        later = next(r for r in self.book.records if "înclinat" in r.text.replace(SHY, ""))
        self.assertEqual([s for _, _, s in later.runs], [ibv.ITALIC, ibv.ITALIC])

    def test_toc_from_nav_with_fragment(self):
        toc = [(t.level, t.title, self.book.records[t.para].text.replace(SHY, "")) for t in self.book.toc]
        self.assertEqual(toc, [(1, "Partea I", "Partea I"), (2, "Capitolul II", "Capitolul II")])

    def test_cover(self):
        cov = cover.load(self.info["cover_bytes"])        # 300 x 500 scalat cu 0,8
        self.assertEqual((cov.width, cov.height), (240, 400))

    def test_no_nav_falls_back_to_headings(self):
        manifest = '<item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>'
        data = make_epub({"ch1.xhtml": CH1}, spine='<itemref idref="c1"/>', manifest=manifest)
        b, _, _ = epub.parse(data, "b.epub")
        book, _ = build(b)
        self.assertEqual([t.title for t in book.toc], ["Partea I"])
        self.assertIsNone(epub.Epub(data).cover_bytes())


# ---------------------------------------------------------------- FB2

FB2 = '''<?xml version="1.0" encoding="windows-1251"?>
<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0" xmlns:l="http://www.w3.org/1999/xlink">
<description><title-info>
 <genre>prose_classic</genre>
 <author><first-name>Александр</first-name><middle-name>Сергеевич</middle-name><last-name>Пушкин</last-name></author>
 <book-title>Повести Белкина</book-title>
 <annotation><p>Пять повестей.</p></annotation>
 <date value="1831-01-01">1831</date>
 <coverpage><image l:href="#cover.png"/></coverpage>
 <lang>ru</lang>
</title-info></description>
<body>
 <title><p>Александр Пушкин</p><p>Повести Белкина</p></title>
 <section id="s1">
  <title><p>Выстрел</p></title>
  <epigraph><p>Стрелялись мы.</p><text-author>Баратынский</text-author></epigraph>
  <section id="s1-1">
   <title><p>I</p></title>
   <p>Мы стояли в местечке <emphasis>***</emphasis>. Жизнь армейского офицера известна<a l:href="#n1" type="note">[1]</a>.</p>
   <subtitle>* * *</subtitle>
   <p>Один только человек принадлежал нашему <strong>обществу</strong>, не будучи военным.</p>
   <poem><stanza><v>Мороз и солнце; день чудесный!</v><v>Еще ты дремлешь, друг прелестный</v></stanza>
         <stanza><v>Пора, красавица, проснись</v></stanza></poem>
  </section>
 </section>
 <section id="s2"><title><p>Метель</p></title><p>В конце 1811 года жил добрый Гаврила Гаврилович.</p></section>
</body>
<body name="notes"><section id="n1"><title><p>1</p></title><p>Сноска, которой не место в тексте.</p></section></body>
<binary id="cover.png" content-type="image/png">{cover}</binary>
</FictionBook>'''


def sample_fb2() -> bytes:
    return FB2.format(cover=base64.b64encode(png(120, 200)).decode()).encode("cp1251")


class TestFb2(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        b, cls.meta, cls.info = fb2.parse(sample_fb2(), "p.fb2")
        cls.book, _ = build(b, "ru")
        cls.cover_bytes = cls.info["cover_bytes"]

    def test_metadata_and_encoding(self):
        self.assertEqual(self.meta["title"], "Повести Белкина")
        self.assertEqual(self.meta["author"], "Александр Сергеевич Пушкин")
        self.assertEqual((self.meta["language"], self.meta["year"]), ("ru", "1831"))
        self.assertEqual(self.meta["description"], "Пять повестей.")

    def test_structure(self):
        recs, plain = self.book.records, texts(self.book)
        self.assertEqual(recs[0].kind, ibv.HEADING)
        self.assertIn("Выстрел", plain)
        q = [r for r in recs if r.kind == ibv.QUOTE]
        self.assertEqual([r.text.replace(SHY, "") for r in q], ["Стрелялись мы.", "Баратынский"])
        self.assertEqual(q[1].flags & ibv.ALIGN_MASK, ibv.ALIGN_RIGHT)
        self.assertNotIn("Сноска", " ".join(plain))
        self.assertTrue(any(p.endswith("известна.") for p in plain))
        self.assertTrue(any(r.kind == ibv.BREAK and r.flags & ibv.BREAK_ORNAMENT for r in recs))
        self.assertEqual(texts(self.book, ibv.VERSE),
                         ["Мороз и солнце; день чудесный!", "Еще ты дремлешь, друг прелестный",
                          "Пора, красавица, проснись"])
        bold = next(r for r in recs if "обществу" in r.text.replace(SHY, ""))
        self.assertEqual([s for _, _, s in bold.runs], [ibv.BOLD])

    def test_toc_levels(self):
        self.assertEqual([(t.level, t.title) for t in self.book.toc],
                         [(1, "Выстрел"), (2, "I"), (1, "Метель")])

    def test_cover_and_zip(self):
        self.assertEqual(Image.open(io.BytesIO(self.cover_bytes)).size, (120, 200))
        out = io.BytesIO()
        with zipfile.ZipFile(out, "w") as z:
            z.writestr("p.fb2", sample_fb2())
        b, meta, _ = fb2.parse(out.getvalue(), "p.fb2.zip")
        self.assertEqual(meta["title"], "Повести Белкина")


# ---------------------------------------------------------------- coperta

class TestCover(unittest.TestCase):
    def test_flat_grey_keeps_its_average(self):
        for level in (32, 128, 200):
            white = cover.dither(np.full((60, 60), float(level))).mean()
            self.assertAlmostEqual(white, level / 255, delta=0.02)

    def test_packing(self):
        grey = np.zeros((2, 10))
        grey[0, 0] = grey[0, 9] = grey[1, 8] = 255
        c = cover.to_cover(grey)
        self.assertEqual((c.width, c.height), (10, 2))
        self.assertEqual(c.bits, bytes([0b10000000, 0b01000000, 0b00000000, 0b10000000]))
        self.assertTrue(np.array_equal(np.asarray(cover.to_image(c)) > 0, grey > 0))

    def test_load_fits_the_screen(self):
        self.assertEqual(cover.load(png(1000, 500)).width, 240)
        c = cover.load(png(500, 2000))
        self.assertEqual((c.width, c.height), (104, 416))

    def test_transparency_is_white(self):
        c = cover.load(png(240, 100, (0, 0, 0, 0), "RGBA"))
        self.assertEqual(set(c.bits), {0xFF})


# ---------------------------------------------------------------- glife + finish

class TestGlyphs(unittest.TestCase):
    def test_fallbacks_and_missing(self):
        missing, replaced = {}, {}
        st = glyphs.apply(styled(("ﬁne − ѣ 漢", 0)), glyphs.default_charset(), missing, replaced)
        self.assertEqual(st.text, "fine - е �")
        self.assertEqual(missing, {"漢": 1})
        self.assertEqual(replaced, {"ﬁ": 1, "−": 1, "ѣ": 1})

    def test_charset_has_the_three_languages(self):
        cs = glyphs.default_charset()
        for ch in "ăâîșțĂÂÎȘȚёЁ„”«»—–…’ �":
            self.assertIn(ord(ch), cs, ch)

    def test_font_follows_language(self):
        for lang in ("ru", "uk-UA", "BG", "sr", "sr_Cyrl", "ro-Cyrl", "kk"):
            self.assertTrue(glyphs.is_cyrillic(lang), lang)
        for lang in ("en", "ro", "mo", "sr-Latn", "de-CH", "", None, "rus"):
            self.assertFalse(glyphs.is_cyrillic(lang), lang)
        # O carte cu scriere latină se culege cu Libron: fără chirilice, iar ſ devine s.
        missing, replaced = {}, {}
        st = glyphs.apply(styled(("ſun Москва ăș", 0)), glyphs.charset_for("en"), missing, replaced)
        self.assertEqual(st.text, "sun ������ ăș")
        self.assertEqual(missing, {c: 1 for c in "Москва"})
        st = glyphs.apply(styled(("Москва", 0)), glyphs.charset_for("ru"), {}, {})
        self.assertEqual(st.text, "Москва")

    def test_language_list_matches_firmware(self):
        src = (pathlib.Path(__file__).resolve().parents[2] / "firmware/components/core/fontsel.c").read_text()
        block = re.search(r"CYRILLIC\[\] = \{(.*?)\};", src, re.S).group(1)
        self.assertEqual(set(re.findall(r'"(\w+)"', block)), glyphs.CYRILLIC_LANGS)


class TestFinish(unittest.TestCase):
    def test_long_paragraph_is_split(self):
        b = doc.Builder()
        b.text("cuvânt " * 3000, 0)
        b.text("final", ibv.BOLD)
        b.end()
        book, p = build(b)
        self.assertGreater(len(book.records), 2)
        self.assertTrue(all(r.flags & ibv.CONT for r in book.records[1:]))
        self.assertFalse(book.records[0].flags & ibv.CONT)
        self.assertTrue(all(len(r.text.encode()) <= doc.SPLIT_BYTES + 64 for r in book.records))
        last = book.records[-1]
        self.assertEqual(last.runs[-1][2], ibv.BOLD)

    def test_chapters_start_pages(self):
        # Un titlu, două părți, capitole destul de lungi ca să fie capitole, un subtitlu
        # chiar sub unul din ele și câteva poezii scurte pe nivelul capitolelor.
        b = doc.Builder()

        def head(level, title):
            b.text(title)
            b.end(ibv.HEADING, doc.heading_flags(level))

        head(1, "Book")
        for part in ("One", "Two"):
            head(2, f"Part {part}")
            for ch in range(3):
                head(3, f"Chapter {ch + 1}")
                if ch == 1:
                    head(4, "In which a subtitle follows")
                for _ in range(10):
                    b.text("o propoziție destul de lungă pentru un paragraf obișnuit " * 6)
                    b.end()
        head(2, "Poems")
        for n in range(4):
            head(3, f"Poem {n + 1}")
            b.text("un vers scurt")
            b.end(ibv.VERSE)
        book, p = build(b)
        page = [r.text for r in book.records if r.flags & ibv.PAGE]
        # Un șir de titluri începe o singură pagină: Cartea + Partea întâi + Capitolul 1
        # deschid cartea, Partea a doua își ia Capitolul 1 cu ea.
        self.assertEqual(page, ["Book", "Chapter 2", "Chapter 3", "Part Two", "Chapter 2", "Chapter 3"])
        self.assertFalse(any(t.startswith("Poem ") for t in page))
        self.assertNotIn("In which a subtitle follows", page)
        self.assertTrue(all(r.kind == ibv.HEADING for r in book.records if r.flags & ibv.PAGE))

    def test_breaks_are_tidied(self):
        b = doc.Builder()
        b.brk()
        b.text("Titlu"); b.end(ibv.HEADING, doc.heading_flags(1))
        b.brk()
        b.text("unu"); b.end()
        b.brk(); b.brk(ornament=True)
        b.text("doi"); b.end()
        b.brk()
        b.text("Alt titlu"); b.end(ibv.HEADING, doc.heading_flags(1))
        b.brk()
        book, _ = build(b)
        self.assertEqual([r.kind for r in book.records],
                         [ibv.HEADING, ibv.PARA, ibv.BREAK, ibv.PARA, ibv.HEADING])
        self.assertTrue(book.records[2].flags & ibv.BREAK_ORNAMENT)
        self.assertTrue(book.records[1].flags & ibv.NO_INDENT)
        self.assertTrue(book.records[3].flags & ibv.NO_INDENT)
        self.assertEqual([t.para for t in book.toc], [0, 4])

    def test_only_prose_is_hyphenated(self):
        b = doc.Builder()
        for kind in (ibv.HEADING, ibv.PARA, ibv.VERSE, ibv.QUOTE):
            b.text("copilărie înțelepciune"); b.end(kind)
        book, _ = build(b)
        self.assertEqual([SHY in r.text for r in book.records], [False, True, False, True])

    def test_unknown_language_is_not_hyphenated(self):
        b = doc.Builder()
        b.text("copilărie"); b.end()
        book, rep = doc.finish(b, {"title": "T", "language": "xx"})
        self.assertNotIn(SHY, book.records[0].text)
        self.assertTrue(any("nu există modele de despărțire" in w for w in rep.warnings))

    def test_toc_anchor_survives_dropped_items(self):
        b = doc.Builder()
        b.text("intro"); b.end()
        b.anchor("x")
        b.text("   "); b.end()               # gol, scos
        b.brk()                              # scos: înaintea unui titlu
        b.text("Cap"); b.end(ibv.HEADING, doc.heading_flags(2))
        b.toc_entry("x", 2, "Cap")
        b.toc_entry("missing", 2, "Nu există")
        book, rep = doc.finish(b, {"title": "T", "language": "ro"})
        self.assertEqual([(t.para, t.title) for t in book.toc], [(1, "Cap")])
        self.assertTrue(any("ținta 'missing' nu există" in w for w in rep.warnings))

    def test_meta(self):
        b = doc.Builder()
        b.text("x"); b.end()
        book, _ = doc.finish(b, {"title": '"Moara" cu noroc', "language": "ro", "author": ""})
        self.assertEqual(book.meta["title"], "„Moara” cu noroc")
        self.assertNotIn("author", book.meta)
        self.assertEqual(book.meta["converter"], doc.CONVERTER)


# ---------------------------------------------------------------- fișiere întregi

class TestConvert(unittest.TestCase):
    def test_cli_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            src = pathlib.Path(d) / "a.epub"
            src.write_bytes(sample_epub())
            (pathlib.Path(d) / "a.txt").write_text("\n\n".join([TestTxt.PROSE] * 5))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(convert.main(["convert", str(src), str(src.with_suffix(".txt")), "-o", d]), 0)
                outs = sorted(p.name for p in pathlib.Path(d).glob("*.ibv"))
                self.assertEqual(outs, ["a-txt.ibv", "a.ibv"])
                self.assertEqual(convert.main(["validate", *(str(pathlib.Path(d) / o) for o in outs)]), 0)

    def test_deterministic(self):
        b1, _, _ = epub.parse(sample_epub(), "a.epub")
        b2, _, _ = epub.parse(sample_epub(), "a.epub")
        meta = {"title": "T", "language": "ro", "converted": "2026-01-01"}
        one = ibv.write(doc.finish(b1, dict(meta))[0])
        two = ibv.write(doc.finish(b2, dict(meta))[0])
        self.assertEqual(one, two)


@unittest.skipUnless(CORPUS.is_dir() and any(CORPUS.iterdir()), "niciun corpus în corpus/files")
class TestCorpus(unittest.TestCase):
    """CONV-10: fiecare carte din corpus se convertește și se validează."""

    def test_corpus(self):
        books = [p for p in sorted(CORPUS.iterdir()) if convert.reader_for(p)]
        for src in books:
            with self.subTest(book=src.name):
                out, lines = convert.convert(src)
                p = ibv.validate(out)
                self.assertGreater(p.paragraph_count, 50)
                self.assertFalse(any("MISSING" in l and "×1" not in l for l in lines[3:]), lines)


if __name__ == "__main__":
    unittest.main()
