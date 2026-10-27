"""Glyph coverage (CONV-5): every character in a book must have a glyph in the
device font, or the reader draws a box.

The character set is the one fonttool.py puts into the font (its
DEFAULT_RANGES). Characters outside it are replaced by a close stand-in from
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
    "ﬅ": "st", "ﬆ": "st",
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
