"""De la intrarea parsată la un ibv.Book.

Fiecare cititor (txt.py, epub.py, fb2.py) hrănește un `Builder`: text cu
stiluri, sfârșituri de paragraf cu un tip, separatoare, ancore și intrări de
cuprins. Apoi `finish()` face munca ce e aceeași pentru orice intrare:

1. curăță textul (textnorm), înlocuiește caracterele care lipsesc din font (glyphs);
2. pune ordine în structură: paragrafele care sunt de fapt strofe (rânduri
   scurte legate cu <br>) devin rânduri VERSE; scoate paragrafele goale, unește
   separatoarele repetate, scoate separatoarele de la capete și de lângă
   titluri, fără alineat după un titlu;
3. desparte în silabe proza (cratime moi);
4. împarte paragrafele prea lungi pentru o înregistrare și le marchează pe celelalte CONT;
5. transformă stilurile în intervale de octeți și rezolvă țintele cuprinsului în numere de paragraf.
"""
from __future__ import annotations

import datetime
import re
from dataclasses import dataclass, field

import glyphs
import ibv
import textnorm
from hyphenate import URL, WORD, Hyphenator, should_hyphenate
from textnorm import SHY, Styled

CONVERTER = "ibv-convert 0.1"
SPLIT_BYTES = 8192          # paragrafele mai lungi devin mai multe înregistrări CONT
CHAPTER_BYTES = 3000        # un nivel de titlu cu secțiuni atât de lungi (mediana) începe pagini
VERSE_MAX_LINE = 80         # un paragraf legat cu <br> al cărui fiecare rând încape e o strofă
VERSE_MEAN_LINE = 55
LSEP = textnorm.LSEP
LSEP_SPACES = re.compile(rf" *{LSEP} *")
LSEP_RUN = re.compile(rf"{LSEP}{{3,}}")
HYPHENATED_KINDS = (ibv.PARA, ibv.QUOTE)


@dataclass
class Item:
    kind: int
    flags: int = 0
    st: Styled = field(default_factory=Styled)


@dataclass
class TocRef:
    target: object          # o cheie de ancoră sau un indice întreg de element
    level: int
    title: str


class Builder:
    def __init__(self) -> None:
        self.items: list[Item] = []
        self.cur = Styled()
        self.anchors: dict[object, int] = {}
        self.toc: list[TocRef] = []

    # textul
    def text(self, s: str, style: int = 0) -> None:
        if s:
            self.cur.append(s, style)

    def line_break(self) -> None:
        self.cur.append(textnorm.LSEP, 0)

    def has_text(self) -> bool:
        return bool(self.cur.text.strip(" \t\r\n\u00a0\u2028"))

    # structura
    def end(self, kind: int = ibv.PARA, flags: int = 0) -> int | None:
        """Încheie paragraful curent. Întoarce indicele lui de element, sau None dacă e gol."""
        st, self.cur = self.cur, Styled()
        if not st.text.strip(" \t\r\n\u00a0\u2028"):
            return None
        self.items.append(Item(kind, flags, st))
        return len(self.items) - 1

    def brk(self, ornament: bool = False) -> None:
        self.end()
        self.items.append(Item(ibv.BREAK, ibv.BREAK_ORNAMENT if ornament else 0))

    def anchor(self, key: object) -> None:
        """Marchează `key` ca arătând spre următorul paragraf care se va încheia."""
        self.anchors.setdefault(key, len(self.items))

    def toc_entry(self, target: object, level: int, title: str) -> None:
        self.toc.append(TocRef(target, level, title))


@dataclass
class Report:
    paragraphs: int = 0
    hyphenated_words: int = 0
    hyphenation: str = ""
    missing: dict[str, int] = field(default_factory=dict)
    replaced: dict[str, int] = field(default_factory=dict)
    chapter_levels: list[int] = field(default_factory=list)
    chapter_pages: int = 0
    warnings: list[str] = field(default_factory=list)


def finish(b: Builder, meta: dict[str, str], *, hyphenate: bool = True,
           ro_legacy: bool | None = None, cover: ibv.Cover | None = None,
           charset: set[int] | None = None) -> tuple[ibv.Book, Report]:
    lang = meta["language"]
    rep = Report()
    if ro_legacy is None:
        sample = "".join(it.st.text for it in b.items[:2000])
        ro_legacy = lang.lower().startswith(("ro", "mo")) and textnorm.detect_ro_legacy(sample)
    if ro_legacy:
        rep.warnings.append("text românesc scris cu ã/º/þ în loc de ă/ș/ț: corectat")
    charset = charset if charset is not None else glyphs.charset_for(lang)

    # 1. curățarea textului; fiecare element sursă devine zero sau mai multe elemente
    groups: list[list[Item]] = []
    for it in b.items:
        if it.kind == ibv.BREAK:
            groups.append([it])
            continue
        st = textnorm.normalize(it.st, lang, ro_legacy)
        st = glyphs.apply(st, charset, rep.missing, rep.replaced)
        st = textnorm.strip(textnorm.sub(st, textnorm.DOUBLE_SPACE, " "))
        st = _tidy_line_breaks(st)
        if not st.text.strip(" "):
            groups.append([])
        elif it.kind == ibv.PARA and _is_stanza(st.text):
            groups.append(_stanza(st, it.flags & ibv.ALIGN_MASK))
        else:
            groups.append([Item(it.kind, it.flags, st)])

    # 2. structura
    keep: list[Item] = []
    new_index: list[int] = []            # indicele vechi al elementului -> indicele în keep (următorul rămas)
    for group in groups:
        new_index.append(len(keep))
        for it in group:
            _place(it, keep, new_index)
    while keep and keep[-1].kind == ibv.BREAK:
        keep.pop()
    new_index = [min(i, len(keep) - 1) for i in new_index]
    for prev, it in zip([None] + keep, keep):
        if it.kind == ibv.PARA and prev is not None and prev.kind in (ibv.HEADING, ibv.BREAK):
            it.flags |= ibv.NO_INDENT

    # 3. despărțirea în silabe
    hy = Hyphenator.for_language(lang) if hyphenate else None
    if hyphenate and hy is None:
        rep.warnings.append(f"nu există modele de despărțire pentru limba '{lang}': cuvintele nu se despart")
    rep.hyphenation = "off" if not hyphenate else ("none" if hy is None else lang)
    if hy:
        for it in keep:
            if it.kind in HYPHENATED_KINDS:
                it.st, n = _hyphenate(it.st, hy)
                rep.hyphenated_words += n

    # 4 + 5. înregistrările
    records: list[ibv.Record] = []
    first_record: list[int] = []          # indicele în keep -> indicele primei înregistrări
    for it in keep:
        first_record.append(len(records))
        if it.kind == ibv.BREAK:
            records.append(ibv.Record(ibv.BREAK, "", it.flags))
            continue
        for k, part in enumerate(_split(it.st, SPLIT_BYTES)):
            flags = it.flags if k == 0 else (it.flags & ~(ibv.NO_INDENT | ibv.PAGE)) | ibv.CONT
            records.append(ibv.Record(it.kind, part.text, flags, _runs(part)))
    rep.paragraphs = len(records)
    if not records:
        raise ibv.FormatError("intrarea nu conține text")
    rep.chapter_levels, rep.chapter_pages = _chapter_pages(records)

    toc = _resolve_toc(b, new_index, first_record, keep, lang, ro_legacy, rep)
    meta = {k: v for k, v in meta.items() if v}
    for key in ("title", "author", "description"):
        if key in meta:
            meta[key] = textnorm.plain(meta[key], lang, ro_legacy)
    meta.setdefault("converter", CONVERTER)
    meta.setdefault("converted", datetime.date.today().isoformat())
    return ibv.Book(meta, records, toc, cover), rep


def _chapter_pages(records: list[ibv.Record]) -> tuple[list[int], int]:
    """Pune PAGE pe titlurile care trebuie să înceapă o pagină nouă. Secțiunea unui
    titlu ține până la următorul titlu de același nivel sau de nivel mai înalt.
    Un nivel e nivel de capitol dacă secțiunile lui au cel puțin CHAPTER_BYTES
    (mediana): titlurile, părțile și capitolele au, poeziile scurte dintr-o
    culegere nu. Pe un nivel de capitol, un titlu a cărui secțiune are sub un
    sfert din asta (o poezie scurtă printre povestiri) continuă pe aceeași
    pagină, la fel și un titlu chiar după altul (un subtitlu). Prima
    înregistrare a cărții se marchează și ea când se califică: oricum începe o
    pagină, iar paginarea așază titlul unui capitol mai jos pe pagină. Întoarce
    nivelurile de capitol și câte înregistrări au fost marcate."""
    size: dict[int, int] = {}           # înregistrarea titlului -> octeții din secțiunea lui
    open_: dict[int, int] = {}          # nivelul -> titlul lui deschis
    for i, r in enumerate(records):
        if r.kind == ibv.HEADING:
            for lv in [lv for lv in open_ if lv >= r.level]:
                del open_[lv]
            open_[r.level] = i
            size[i] = 0
        n = len(r.text.encode("utf-8"))
        for h in open_.values():
            size[h] += n
    by_level: dict[int, list[int]] = {}
    for i, n in size.items():
        by_level.setdefault(records[i].level, []).append(n)
    levels = sorted(lv for lv, v in by_level.items() if sorted(v)[len(v) // 2] >= CHAPTER_BYTES)
    marked = 0
    for i, n in size.items():
        r = records[i]
        if (r.level in levels and n >= CHAPTER_BYTES // 4 and not r.flags & ibv.CONT
                and (i == 0 or records[i - 1].kind != ibv.HEADING)):
            r.flags |= ibv.PAGE
            marked += 1
    return levels, marked


def _place(it: Item, keep: list[Item], new_index: list[int]) -> None:
    if it.kind == ibv.BREAK:
        if keep and keep[-1].kind == ibv.BREAK:
            keep[-1].flags |= it.flags
            return
        if not keep or keep[-1].kind == ibv.HEADING:
            return
    elif it.kind == ibv.HEADING and keep and keep[-1].kind == ibv.BREAK:
        keep.pop()
        new_index[-1] = min(new_index[-1], len(keep))
    keep.append(it)


def _tidy_line_breaks(st: Styled) -> Styled:
    """Fără spații în jurul unei ruperi de rând, cel mult un rând gol la rând."""
    st = textnorm.sub(st, LSEP_SPACES, LSEP)
    st = textnorm.sub(st, LSEP_RUN, LSEP * 2)
    return textnorm.strip(st)


def _is_stanza(text: str) -> bool:
    lines = [l for l in text.split(LSEP) if l]
    if len(lines) < 2:
        return False
    lengths = [len(l) for l in lines]
    return max(lengths) <= VERSE_MAX_LINE and sum(lengths) / len(lengths) <= VERSE_MEAN_LINE


def _stanza(st: Styled, align: int) -> list[Item]:
    """Câte un element VERSE pe rând, un BREAK la fiecare rând gol și după strofă."""
    out, start = [], 0
    for i in range(len(st.text) + 1):
        if i == len(st.text) or st.text[i] == LSEP:
            line = textnorm.strip(Styled(st.text[start:i], st.styles[start:i]))
            if line.text:
                out.append(Item(ibv.VERSE, align, line))
            elif out and out[-1].kind != ibv.BREAK:
                out.append(Item(ibv.BREAK))
            start = i + 1
    out.append(Item(ibv.BREAK))
    return out


def _resolve_toc(b: Builder, new_index, first_record, keep, lang, ro_legacy, rep) -> list[ibv.TocEntry]:
    refs = b.toc
    if not refs:   # sursa nu are cuprins: se face unul din titluri
        refs = [TocRef(i, 1 + ((it.flags & ibv.LEVEL_MASK) >> ibv.LEVEL_SHIFT), it.st.text)
                for i, it in enumerate(b.items) if it.kind == ibv.HEADING]
    out: list[ibv.TocEntry] = []
    for ref in refs:
        idx = ref.target if isinstance(ref.target, int) else b.anchors.get(ref.target)
        if idx is None:
            rep.warnings.append(f"intrarea din cuprins {ref.title!r}: ținta {ref.target!r} nu există")
            continue
        if idx >= len(new_index):
            idx = len(new_index) - 1
        para = first_record[new_index[idx]] if idx >= 0 else 0
        title = textnorm.plain(ref.title, lang, ro_legacy).replace(SHY, "")
        if not title:
            continue
        if out and para <= out[-1].para:
            continue                       # aceeași țintă ca intrarea dinainte (de exemplu parte + capitol)
        out.append(ibv.TocEntry(para, min(max(ref.level, 1), 4), title[:200]))
    return out


def _hyphenate(st: Styled, hy: Hyphenator) -> tuple[Styled, int]:
    text, styles = st.text, st.styles
    out, sty, pos, n = [], [], 0, 0
    urls = [u.span() for u in URL.finditer(text)]
    for m in WORD.finditer(text):
        word = m.group(0)
        if not should_hyphenate(word) or any(a <= m.start() < e for a, e in urls):
            continue
        pts = hy.points(word)
        if not pts:
            continue
        n += 1
        a = m.start()
        for p in pts:
            out.append(text[pos:a + p])
            sty.extend(styles[pos:a + p])
            out.append(SHY)
            sty.append(styles[a + p - 1])
            pos = a + p
    if not n:
        return st, 0
    out.append(text[pos:])
    sty.extend(styles[pos:])
    return Styled("".join(out), sty), n


def _split(st: Styled, limit: int) -> list[Styled]:
    if len(st.text.encode("utf-8")) <= limit:
        return [st]
    parts, start, size, last_space = [], 0, 0, -1
    for i, ch in enumerate(st.text):
        if ch == " ":
            last_space = i
        size += len(ch.encode("utf-8"))
        if size > limit and last_space > start:
            parts.append(Styled(st.text[start:last_space], st.styles[start:last_space]))
            start = last_space + 1
            size = len(st.text[start:i + 1].encode("utf-8"))
            last_space = -1
    parts.append(Styled(st.text[start:], st.styles[start:]))
    return parts


def _runs(st: Styled) -> list[tuple[int, int, int]]:
    runs, byte, cur, cur_start = [], 0, 0, 0
    for ch, s in zip(st.text, st.styles):
        s &= ibv.STYLE_KNOWN
        if s != cur:
            if cur:
                runs.append((cur_start, byte - cur_start, cur))
            cur, cur_start = s, byte
        byte += len(ch.encode("utf-8"))
    if cur:
        runs.append((cur_start, byte - cur_start, cur))
    return runs


def heading_flags(level: int, align: int = ibv.ALIGN_CENTER) -> int:
    return align | ((min(max(level, 1), 4) - 1) << ibv.LEVEL_SHIFT)
