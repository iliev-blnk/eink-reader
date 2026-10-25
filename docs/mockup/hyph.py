# Marks hyphenation points in the prepared book files: inserts U+00AD (soft
# hyphen) into prose words, and ties a mid-sentence dash to the word before it
# with U+00A0 so no line starts with a dash. The PC-side converter will do the
# same job; the device only breaks lines.
#   python hyph.py <dir>   (reads <name>.txt, writes <name>.hy.txt)
import re, sys, pathlib, pyphen

LANG = {"amintiri": "ro_RO", "luceafarul": "ro_RO", "moara": "ro_RO", "belye": "ru_RU"}
LETTERS = re.compile(r"[^\W\d_]{5,}")
CAPTION = re.compile(r"\b(thumb|\d+px)\|")  # image captions left over from the wikitext

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
