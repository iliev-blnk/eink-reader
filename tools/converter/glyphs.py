"""Acoperirea glifelor (CONV-5): fiecare caracter dintr-o carte trebuie să aibă o
glifă în fontul dispozitivului, altfel cititorul desenează o căsuță.

Setul de caractere e cel pe care fonttool.py îl pune în font (DEFAULT_RANGES),
minus ce îi lipsește fontului cărții: dispozitivul culege o carte cu PT Serif
dacă limba ei se scrie cu chirilice și cu Libron altfel (fontsel.c din
firmware), iar Libron nu are chirilice. Caracterele din afara lui se înlocuiesc
cu un echivalent apropiat din FALLBACK, când există. Celelalte se numără și se
raportează, apoi se înlocuiesc cu U+FFFD, ca problema să se vadă pe dispozitiv
în loc să treacă neobservată.
"""
from __future__ import annotations

import functools
import pathlib
import re

from textnorm import LSEP, SHY, ZWSP, Styled, sub

# Nu se desenează niciodată, deci nu le trebuie glifă.
INVISIBLE = {ord(SHY), ord(ZWSP), ord(LSEP)}

FALLBACK = {
    # linii, minus, ghilimele, prime
    "―": "—", "⸺": "—", "⸻": "—", "−": "-", "‑": "-",
    "‟": "“", "″": "”", "′": "’", "‵": "‘", "ʼ": "’",
    "´": "’", "⁄": "/", "‧": "·", "∙": "·", "◦": "•",
    "‣": "•", "⁃": "-", "⁣": "",
    # ligaturi
    "ﬀ": "ff", "ﬁ": "fi", "ﬂ": "fl", "ﬃ": "ffi", "ﬄ": "ffl",
    "ﬅ": "st", "ﬆ": "st", "Ĳ": "IJ", "ĳ": "ij", "ſ": "s", "ŉ": "’n",
    # Rusă: literele de dinainte de 1918, semnul numărului, accentele
    "Ѣ": "Е", "ѣ": "е", "Ѳ": "Ф", "ѳ": "ф", "Ѵ": "И", "ѵ": "и",
    "№": "№",            # păstrat dacă îl are fontul, vezi mai jos
    "́": "", "̀": "",
    # spații și puncte
    "․": ".", "‥": "..", "…": "…", "⋯": "…",
    "\u00a0": "\u00a0",
}
# a doua variantă când nici prima nu e în font
FALLBACK_2 = {"№": "No", "…": "...", "—": "-", "“": '"', "”": '"', "’": "'", "‘": "'", "•": "*"}


# Aceeași listă și aceleași reguli ca fontsel.c (un test verifică dacă sunt de acord).
CYRILLIC_LANGS = frozenset({"ba", "be", "bg", "ce", "cv", "kk", "ky", "mk", "mn",
                            "os", "ru", "sah", "sr", "tg", "tt", "uk"})
# În DEFAULT_RANGES, dar nu în Libron.
LIBRON_LACKS = frozenset(range(0x400, 0x530)) | {0x132, 0x133, 0x149, 0x17F}


def is_cyrillic(lang: str | None) -> bool:
    """True dacă dispozitivul culege o carte în `lang` (BCP 47) cu fontul chirilic."""
    tags = re.split(r"[-_]", (lang or "").lower())
    for t in tags[1:]:
        if t in ("cyrl", "latn"):
            return t == "cyrl"
    return tags[0] in CYRILLIC_LANGS


@functools.cache
def charset_for(lang: str | None) -> frozenset[int]:
    """Caracterele pe care le are fontul dispozitivului pentru o carte în `lang`."""
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
