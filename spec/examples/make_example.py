"""Scrie example.ibv, exemplul lucrat din ../book-format.md (stocat, fără compresie,
ca fiecare octet să se poată citi).  python3 make_example.py"""
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "converter"))
import ibv
from ibv import Record

t = "Ce mai «faci»?"
s = t.encode().index("«".encode())
book = ibv.Book(
    {"title": "Exemplu", "language": "ro"},
    [
        Record(ibv.HEADING, "I", ibv.ALIGN_CENTER),
        Record(ibv.PARA, "Stau câ­teo­da­tă.", ibv.NO_INDENT),
        Record(ibv.PARA, t, 0, [(s, len("«faci»".encode()), ibv.ITALIC)]),
        Record(ibv.BREAK, "", ibv.BREAK_ORNAMENT),
        Record(ibv.VERSE, "A fost odată"),
    ],
    [ibv.TocEntry(0, 1, "I")],
)
data = ibv.write(book, compress=False)
ibv.validate(data)
out = pathlib.Path(__file__).with_name("example.ibv")
out.write_bytes(data)
print(f"{out.name}: {len(data)} octeți")
