"""Glyph coverage (CONV-5): every character in a book must have a glyph in the
device font, or the reader draws a box.

The character set is the one fonttool.py puts into the font (its
DEFAULT_RANGES), less what the book's font lacks: the device sets a book in
PT Serif if its language is written in Cyrillic and in Libron otherwise
(firmware fontsel.c), and Libron has no Cyrillic. Characters outside it are replaced by a close stand-in from
FALLBACK when there is one. The rest are counted and reported, then replaced
by U+FFFD so the problem is visible on the device rather than silent.
"""
from __future__ import annotations

import functools
import pathlib
import re

from textnorm import LSEP, SHY, ZWSP, Styled, sub

# Never drawn, so they need no glyph.
INVISIBLE = {ord(SHY), ord(ZWSP), ord(LSEP)}

FALLBACK = {
    # dashes, minus, quotes, primes
    "―": "—", "⸺": "—", "⸻": "—", "−": "-", "‑": "-",
    "‟": "“", "″": "”", "′": "’", "‵": "‘", "ʼ": "’",
    "´": "’", "⁄": "/", "‧": "·", "∙": "·", "◦": "•",
    "‣": "•", "⁃": "-", "⁣": "",
    # ligatures
    "ﬀ": "ff", "ﬁ": "fi", "ﬂ": "fl", "ﬃ": "ffi", "ﬄ": "ffl",
    "ﬅ": "st", "ﬆ": "st", "Ĳ": "IJ", "ĳ": "ij", "ſ": "s", "ŉ": "’n",
    # Russian: pre-1918 letters, number sign, stress marks
    "Ѣ": "Е", "ѣ": "е", "Ѳ": "Ф", "ѳ": "ф", "Ѵ": "И", "ѵ": "и",
    "№": "№",            # kept if the font has it, see below
    "́": "", "̀": "",
    # spaces and dots
    "․": ".", "‥": "..", "…": "…", "⋯": "…",
    "\u00a0": "\u00a0",
}
# second choice when the first one is missing from the font too
FALLBACK_2 = {"№": "No", "…": "...", "—": "-", "“": '"', "”": '"', "’": "'", "‘": "'", "•": "*"}


# Same list and rules as fontsel.c (a test checks they agree).
CYRILLIC_LANGS = frozenset({"ba", "be", "bg", "ce", "cv", "kk", "ky", "mk", "mn",
                            "os", "ru", "sah", "sr", "tg", "tt", "uk"})
# In DEFAULT_RANGES but not in Libron.
LIBRON_LACKS = frozenset(range(0x400, 0x530)) | {0x132, 0x133, 0x149, 0x17F}


def is_cyrillic(lang: str | None) -> bool:
    """True if the device sets a book in `lang` (BCP 47) in the Cyrillic font."""
    tags = re.split(r"[-_]", (lang or "").lower())
    for t in tags[1:]:
        if t in ("cyrl", "latn"):
            return t == "cyrl"
    return tags[0] in CYRILLIC_LANGS


@functools.cache
def charset_for(lang: str | None) -> frozenset[int]:
    """The characters the device font for a book in `lang` has."""
    full = default_charset()
    return full if is_cyrillic(lang) else full - LIBRON_LACKS


@functools.cache
def default_charset() -> frozenset[int]:
    path = pathlib.Path(__file__).resolve().parents[1] / "fonttool" / "fonttool.py"
    src = path.read_text(encoding="utf-8")
    m = re.search(r'DEFAULT_RANGES = \((.*?)\)', src, re.S)
    ranges = "".join(re.findall(r'"([^"]*)"', m.group(1)))
    return frozenset(parse_ranges(ranges))


def parse_ranges(spec: str) -> set[int]:
    cps = set()
    for part in spec.split(","):
        part = part.strip()
        if part:
            lo, _, hi = part.partition("-")
            cps.update(range(int(lo, 0), int(hi or lo, 0) + 1))
    return cps


def apply(st: Styled, charset: set[int], missing: dict[str, int], replaced: dict[str, int]) -> Styled:
    bad = {ch for ch in st.text if ord(ch) not in charset and ord(ch) not in INVISIBLE}
    if not bad:
        return st
    table = {}
    for ch in bad:
        rep = FALLBACK.get(ch)
        if rep is not None and all(ord(c) in charset for c in rep):
            table[ch] = rep
        elif rep is not None and FALLBACK_2.get(rep) is not None:
            table[ch] = FALLBACK_2[rep]
        elif FALLBACK_2.get(ch) is not None:
            table[ch] = FALLBACK_2[ch]
        else:
            table[ch] = "�"
    for ch in bad:
        n = st.text.count(ch)
        if table[ch] == "�":
            missing[ch] = missing.get(ch, 0) + n
        else:
            replaced[ch] = replaced.get(ch, 0) + n
    return sub(st, re.compile("|".join(map(re.escape, sorted(bad)))), lambda m: table[m.group(0)])
