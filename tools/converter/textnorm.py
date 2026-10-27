"""Curățarea textului, aplicată fiecărui paragraf înainte de despărțirea în silabe (CONV-3).

Textul se poartă ca `Styled`: un șir plus o valoare de stil pe caracter, deci
bold-ul și italicul trec prin fiecare înlocuire de aici. Fiecare regulă e o mică
substituție cu expresii regulate; `normalize()` le rulează în ordine.
"""
from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass, field

SHY, NBSP, ZWSP, LSEP = "\u00ad", "\u00a0", "\u200b", "\u2028"


@dataclass
class Styled:
    text: str = ""
    styles: list[int] = field(default_factory=list)

    def append(self, s: str, style: int) -> None:
        self.text += s
        self.styles.extend([style] * len(s))

    def __len__(self) -> int:
        return len(self.text)


def sub(st: Styled, pattern: re.Pattern, repl) -> Styled:
    """re.sub care păstrează stilurile pe caracter. O înlocuire de aceeași lungime
    păstrează stilul fiecărui caracter; orice altceva ia stilul primului
    caracter înlocuit (sau al caracterului dinainte, la o potrivire goală)."""
    out, sty, pos = [], [], 0
    for m in pattern.finditer(st.text):
        a, b = m.span()
        r = repl(m) if callable(repl) else m.expand(repl)
        out.append(st.text[pos:a])
        sty.extend(st.styles[pos:a])
        if len(r) == b - a:
            sty.extend(st.styles[a:b])
        else:
            s = st.styles[a] if a < len(st.styles) else (st.styles[a - 1] if a else 0)
            sty.extend([s] * len(r))
        out.append(r)
        pos = b
    if pos == 0 and not out:
        return st
    out.append(st.text[pos:])
    sty.extend(st.styles[pos:])
    return Styled("".join(out), sty)


def table_sub(st: Styled, table: dict[str, str]) -> Styled:
    if not any(ch in table for ch in st.text):
        return st
    pattern = re.compile("|".join(re.escape(k) for k in sorted(table, key=len, reverse=True)))
    return sub(st, pattern, lambda m: table[m.group(0)])


# ---------------------------------------------------------------- regulile

# s/t românești cu sedilă (moștenite din paginile de cod turcești) -> virgulă dedesubt
RO_COMMA = {"ş": "ș", "Ş": "Ș", "ţ": "ț", "Ţ": "Ț"}
# Româna tastată cu fonturi occidentale în anii '90: ã/º/þ țineau locul lui ă/ș/ț.
# Se aplică doar când le folosește toată cartea (vezi detect_ro_legacy).
RO_LEGACY = {"ã": "ă", "Ã": "Ă", "º": "ș", "ª": "Ș", "þ": "ț", "Þ": "Ț"}

SPACES = re.compile(r"[\t\n\r\f\v   -\u200a\u205f\u3000\u0085]+")
NARROW_NBSP = re.compile(r"[\u202f\u2007]")
DROP = re.compile(r"[\u0000-\u0008\u000b\u000e-\u001f\u007f-\u0084\u0086-\u009f\ufeff\u200c\u200d\u200e\u200f\u2060\u00ad]")
COMBINING_RUN = re.compile(r".[̀-ͯ҃-҉]+", re.S)
ELLIPSIS = re.compile(r"\.\s?\.\s?\.(?!\.)")
DOUBLE_HYPHEN = re.compile(r"(?<!-)---?(?!-)")
SPACED_HYPHEN = re.compile(r"(?<=\S) [-–] (?=\S)")
LEADING_DASH = re.compile(r"^[-–](?= )")
DASH_TIE = re.compile(r"(?<=\S) (?=[—–](?: |$))")
NBSP_SPACE = re.compile(r" ?\u00a0 ?")
DOUBLE_SPACE = re.compile(r"  +")
LINESEP_SPACE = re.compile(r" ?\u2028 ?")
OPEN_CONTEXT = set(" \u00a0\u2028([{<«„“‘—–-/") | {""}

QUOTES = {           # (deschidere, închidere) pentru " în fiecare limbă
    "ro": ("„", "”"),
    "ru": ("«", "»"),
    "en": ("“", "”"),
}


def detect_ro_legacy(sample: str) -> bool:
    """True când un text românesc folosește peste tot ã/º/þ pentru ă/ș/ț."""
    legacy = sum(sample.count(c) for c in "ãºþ")
    proper = sum(sample.count(c) for c in "ăășşțţ")
    return legacy > 20 and legacy > 5 * proper


# "ceva , te rog" (scanări, fișiere TXT vechi); nu înainte de "...", ".5" sau o paranteză deschisă
SPACE_BEFORE_PUNCT = re.compile(r" +(?=[,;:!?.](?:[\s\u2028)\]”»’]|$))")


def _quotes(st: Styled, lang: str) -> Styled:
    """Ghilimelele drepte -> cele tipografice. Ghilimelele se deschid după un spațiu,
    o paranteză sau o linie și se închid în rest; ,, (fișierele românești vechi) deschide."""
    if '"' not in st.text and "'" not in st.text and ",," not in st.text:
        return st
    opening, closing = QUOTES.get(lang, QUOTES["en"])
    if lang == "ro":
        st = sub(st, re.compile(r",,(?=\w)"), "„")

    def repl(m: re.Match) -> str:
        prev = st.text[m.start() - 1] if m.start() else ""
        if m.group(0) == '"':
            return opening if prev in OPEN_CONTEXT else closing
        nxt = st.text[m.end()] if m.end() < len(st.text) else ""
        if lang == "en" and prev in OPEN_CONTEXT and not nxt.isdigit():     # dar ’90s
            return "‘"
        return "’"
    return sub(st, re.compile(r"[\"']"), repl)


def normalize(st: Styled, lang: str, ro_legacy: bool = False) -> Styled:
    base = (lang or "").lower().split("-")[0]
    st = sub(st, DROP, "")
    st = sub(st, NARROW_NBSP, NBSP)
    st = strip(sub(st, SPACES, " "))
    if not unicodedata.is_normalized("NFC", st.text):
        st = sub(st, COMBINING_RUN, lambda m: unicodedata.normalize("NFC", m.group(0)))
    if base in ("ro", "mo"):
        if ro_legacy:
            st = table_sub(st, RO_LEGACY)
        st = table_sub(st, RO_COMMA)
    st = sub(st, ELLIPSIS, "…")
    st = sub(st, DOUBLE_HYPHEN, "—")
    dash = "–" if base == "en" else "—"
    st = sub(st, SPACED_HYPHEN, f" {dash} ")
    if base != "en":
        st = sub(st, LEADING_DASH, "—")
    st = _quotes(st, base)
    st = sub(st, SPACE_BEFORE_PUNCT, "")
    st = sub(st, LINESEP_SPACE, LSEP)
    st = sub(st, NBSP_SPACE, NBSP)
    st = sub(st, DOUBLE_SPACE, " ")
    st = strip(st)
    # linia de dialog se leagă de cuvântul dinainte, ca niciun rând să nu înceapă cu ea
    st = sub(st, DASH_TIE, NBSP)
    return st


def strip(st: Styled) -> Styled:
    text = st.text
    a = len(text) - len(text.lstrip(" " + LSEP))
    b = len(text.rstrip(" " + LSEP))
    if a == 0 and b == len(text):
        return st
    return Styled(text[a:b], st.styles[a:b])


def plain(text: str, lang: str, ro_legacy: bool = False) -> str:
    """normalize() pentru șiruri fără stil, ca titlurile și etichetele din cuprins."""
    st = Styled()
    st.append(text, 0)
    return normalize(st, lang, ro_legacy).text.replace(LSEP, " ")
