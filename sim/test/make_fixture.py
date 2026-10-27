#!/usr/bin/env python3
"""Scrie layout.ibv: o carte mică cu fiecare tip de înregistrare, indicator și
caracter special tratat de paginare (LAY-1/2/3/4), pentru testul cu imagini de
referință (run.sh).

Fișierul e în depozit, deci testul nu depinde de convertor; se rulează din nou
doar pentru a schimba fișierul de test, apoi `run.sh --update` și o privire pe
paginile noi."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tools/converter"))
import ibv  # noqa: E402

SHY, NBSP, ZWSP, LSEP = "­", " ", "​", " "


def styled(*parts):
    """styled("simplu ", ("bold", 1), " mai mult") -> (text, intervale)."""
    text, runs, pos = "", [], 0
    for p in parts:
        s, style = (p, 0) if isinstance(p, str) else p
        n = len(s.encode("utf-8"))
        if style:
            runs.append((pos, n, style))
        text += s
        pos += n
    return text, runs


def para(*parts, kind=ibv.PARA, flags=0):
    text, runs = styled(*parts)
    return ibv.Record(kind, text, flags, runs)


def heading(title, level=1, page=False):
    return ibv.Record(ibv.HEADING, title, (level - 1) << ibv.LEVEL_SHIFT | (ibv.PAGE if page else 0))


LAMP = "The lamp was lit at dusk and the street went quiet, and for a while nobody said anything at all."


def filler(n):
    return " ".join([LAMP] * n)

records = [
    heading("Chapter One", page=True),
    para("A justified paragraph with ", ("bold", 1), ", ", ("italic", 2), " and ",
         ("bold italic", 3), " runs, kerning pairs like AV, Ta and “quoted” words, "
         f"and long words with soft hyphens: in{SHY}com{SHY}pre{SHY}hen{SHY}si{SHY}bil{SHY}i{SHY}ty, "
         f"ex{SHY}tra{SHY}or{SHY}di{SHY}nar{SHY}i{SHY}ly, un{SHY}ques{SHY}tion{SHY}a{SHY}bly."),
    para(f"Dashes tie to the word before{NBSP}— like this{NBSP}— and break inside words: "
         "north-west, 1914–1918, self-evident, well-known."),
    para("A word too long for any line: Pneumonoultramicroscopicsilicovolcanoconiosis, "
         "which has to be cut."),
    para(f"A path with break hints: www.example.org/{ZWSP}books/{ZWSP}volumes/{ZWSP}"
         f"chapter-seven/{ZWSP}page-index.html in the middle of a sentence."),
    ibv.Record(ibv.BREAK, flags=ibv.BREAK_ORNAMENT),
    para("After an ornament. " + filler(2)),
    ibv.Record(ibv.VERSE, f"A short verse line,{LSEP}a second line,{LSEP}and a third line that is long "
               "enough to wrap with a hanging indent."),
    ibv.Record(ibv.VERSE, "A second stanza, one line."),
    para("A quotation set in from both margins. " + LAMP, kind=ibv.QUOTE),
    para("A paragraph the converter split in two, so the line before the split stays "
         "justified and the next part continues"),
    para("without an indent or a gap, as if nothing happened. " + LAMP, flags=ibv.CONT),
    para("Centred.", flags=2),
    para("Right-aligned.", flags=3),
    para("No indent on this one. " + LAMP, flags=ibv.NO_INDENT),
    ibv.Record(ibv.BREAK),
    para("After a blank break. " + filler(3)),
    heading("A Section Heading That Is Long Enough to Wrap", level=2),
    para(filler(4)),
    para(f"Ро{SHY}ман{SHY}ский и рус{SHY}ский текст: «Кирила Петрович» — "
         f"și țara, în{SHY}tâm{SHY}plă{SHY}tor, ș/ț."),
    heading("Chapter Two", page=True),   # cum marchează convertorul capitolele
    para(filler(6)),
    heading("Late Heading", level=2),
    para(filler(3)),
]
toc = [ibv.TocEntry(i, r.level, r.text) for i, r in enumerate(records) if r.kind == ibv.HEADING]

# O copertă de 48x64: un chenar și o diagonală (bitul 1 = alb).
W, H = 48, 64
rows = []
for y in range(H):
    bits = 0
    for x in range(W):
        ink = x in (0, W - 1) or y in (0, H - 1) or abs(x * H - y * W) < W
        bits = bits << 1 | (0 if ink else 1)
    rows.append(bits.to_bytes(W // 8, "big"))
book = ibv.Book({"title": "Layout fixture", "author": "eink-reader", "language": "en"},
                records, toc, ibv.Cover(W, H, b"".join(rows)))
# Blocuri mici, ca paginile să treacă peste granițele blocurilor și privirea înainte după CONT să încarce alt bloc.
data = ibv.write(book, block_target=600)
ibv.validate(data)
(HERE / "layout.ibv").write_bytes(data)
print(f"layout.ibv: {len(records)} înregistrări, {len(data)} octeți, "
      f"{len(ibv.parse(data).blocks)} blocuri")
