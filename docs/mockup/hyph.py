# Marchează punctele de despărțire în fișierele de carte pregătite: pune U+00AD
# (cratima moale) în cuvintele din proză și leagă o linie din mijlocul frazei de
# cuvântul dinainte cu U+00A0, ca niciun rând să nu înceapă cu o linie. Convertorul
# de pe PC va face aceeași treabă; dispozitivul doar rupe rândurile.
#   python hyph.py <dir>   (citește <name>.txt, scrie <name>.hy.txt)
import re, sys, pathlib, pyphen

LANG = {"amintiri": "ro_RO", "luceafarul": "ro_RO", "moara": "ro_RO", "belye": "ru_RU"}
LETTERS = re.compile(r"[^\W\d_]{5,}")
CAPTION = re.compile(r"\b(thumb|\d+px)\|")  # legende de imagini rămase din wikitext

def mark(line, dic):
    kind, text = line[:2], line[2:]
    if kind != "P ":
        return line
    text = LETTERS.sub(lambda m: dic.inserted(m.group(0), hyphen="­"), text)
    text = re.sub(r"(?<=\S) (—|–)", " \\1", text)
    return kind + text

d = pathlib.Path(sys.argv[1])
for name, lang in LANG.items():
    dic = pyphen.Pyphen(lang=lang, left=2, right=2)
    lines = (d / f"{name}.txt").read_text().splitlines()
    lines = [l for l in lines if not CAPTION.search(l)]
    (d / f"{name}.hy.txt").write_text("\n".join(mark(l, dic) for l in lines) + "\n")
    print(name, lang, len(lines))
