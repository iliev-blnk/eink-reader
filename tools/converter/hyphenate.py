"""Algoritmul de despărțire în silabe al lui Liang (ca în TeX) peste modelele hyph-utf8 din patterns/.

    h = Hyphenator.for_language("ro")
    h.points("copilărie")          -> [2, 4, 6, 8]  (ruperea e permisă înaintea acestor litere)
    h.hyphenate("copilărie", "-")  -> "co-pi-lă-ri-e"

Modelele sunt șiruri ca "a3i2e". Citit fără cifre, un model e un subșir care
poate apărea în ".cuvânt." (punctele marchează capetele cuvântului). Cifrele sunt
scoruri pentru golurile dintre literele lui. Fiecare model găsit în cuvânt își
adaugă scorurile, iar unde două modele punctează același gol câștigă scorul mai
mare. Un scor final impar permite ruperea acolo; unul par o interzice.
"""
from __future__ import annotations

import functools
import pathlib
import re

PATTERN_DIR = pathlib.Path(__file__).resolve().parent / "patterns"

# eticheta limbii (cu litere mici, ca în BCP 47) -> (numele fișierului de modele, minimul stânga, minimul dreapta)
LANGUAGES = {
    "ro": ("hyph-ro", 2, 2),
    "ru": ("hyph-ru", 2, 2),
    "en": ("hyph-en-us", 2, 3),
    "en-us": ("hyph-en-us", 2, 3),
    "en-gb": ("hyph-en-gb", 2, 3),
}
ALIASES = {"mo": "ro", "ro-md": "ro", "ro-ro": "ro", "ru-ru": "ru", "ru-md": "ru", "en-uk": "en-gb"}


def resolve(lang: str) -> str | None:
    """Cheia din LANGUAGES pentru o etichetă BCP 47, sau None când n-avem modele pentru ea."""
    tag = (lang or "").strip().lower().replace("_", "-")
    tag = ALIASES.get(tag, tag)
    if tag in LANGUAGES:
        return tag
    base = tag.split("-")[0]
    base = ALIASES.get(base, base)
    return base if base in LANGUAGES else None


class Hyphenator:
    def __init__(self, patterns: list[str], exceptions: list[str], left: int, right: int):
        self.left, self.right = left, right
        self.patterns: dict[str, tuple[int, ...]] = {}
        for pat in patterns:
            letters, scores = [], [0]
            for ch in pat:
                if ch.isdigit():
                    scores[-1] = int(ch)
                else:
                    letters.append(ch)
                    scores.append(0)
            self.patterns["".join(letters)] = tuple(scores)
        self.longest = max((len(k) for k in self.patterns), default=0)
        self.exceptions: dict[str, list[int]] = {}
        for word in exceptions:
            pts, n = [], 0
            for ch in word:
                if ch == "-":
                    pts.append(n)
                else:
                    n += 1
            self.exceptions[word.replace("-", "")] = pts

    @classmethod
    @functools.cache
    def for_language(cls, lang: str) -> "Hyphenator | None":
        key = resolve(lang)
        if key is None:
            return None
        stem, left, right = LANGUAGES[key]
        pats = _read_list(PATTERN_DIR / f"{stem}.pat.txt")
        exc = _read_list(PATTERN_DIR / f"{stem}.hyp.txt")
        return cls(pats, exc, left, right)

    def points(self, word: str) -> list[int]:
        """Indicii i pentru care e permisă ruperea între word[i-1] și word[i]."""
        w = word.lower()
        if len(w) != len(word) or len(w) < self.left + self.right:
            return []
        if w in self.exceptions:
            return [i for i in self.exceptions[w] if self.left <= i <= len(w) - self.right]
        s = f".{w}."
        score = [0] * (len(s) + 1)
        for i in range(len(s)):
            for j in range(i + 1, min(len(s), i + self.longest) + 1):
                pat = self.patterns.get(s[i:j])
                if pat:
                    for k, v in enumerate(pat):
                        if v > score[i + k]:
                            score[i + k] = v
        # score[k] e golul dinainte de s[k]; litera i a cuvântului e s[i + 1]
        return [i for i in range(self.left, len(w) - self.right + 1) if score[i + 1] % 2]

    def hyphenate(self, word: str, mark: str = "\u00ad") -> str:
        out, last = [], 0
        for p in self.points(word):
            out.append(word[last:p])
            last = p
        out.append(word[last:])
        return mark.join(out)


def _read_list(path: pathlib.Path) -> list[str]:
    if not path.exists():
        return []
    return [ln.strip() for ln in path.read_text(encoding="utf-8").splitlines()
            if ln.strip() and not ln.startswith("%")]


# Cuvintele despărțite: șiruri de litere. Un cuvânt cu o majusculă după prima
# literă (NATO, McDonald, iPhone) e un acronim sau un nume și rămâne neatins.
WORD = re.compile(r"[^\W\d_]+")


def should_hyphenate(word: str) -> bool:
    return not any(ch.isupper() for ch in word[1:])
