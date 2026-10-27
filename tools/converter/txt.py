"""Plain text input (CONV-1).

1. Encoding: a BOM, else strict UTF-8, else the legacy code page that reads
   best (CP1250/ISO-8859-2/-16 for old Romanian files, CP1251/KOI8-R for
   Russian, CP1252 for Western). "Reads best" is scored per word: words in one
   script and with normal capitalisation score, mixed-script words like
   "mгrgea" (CP1250 read as CP1251) and odd-case words like "пРИВЕТ" (KOI8-R
   read as CP1251) cost.
2. Project Gutenberg header and licence are cut off; its Title/Author/Language
   lines become metadata. The same for lib.ru's "Author. Title" line and the
   source note between dashed lines under it.
3. Paragraphs: the file is classified as blank-line separated, indented
   first lines, or one paragraph per line, and lines are joined accordingly.
4. Inside that: chapter headings, scene breaks (* * *) and verse blocks.
"""
from __future__ import annotations

import re
import statistics

import ibv
from doc import Builder, heading_flags

LEGACY = ("cp1250", "iso8859_16", "iso8859_2", "cp1251", "koi8_r", "cp1252")
WORDS = re.compile(r"[^\W\d_]+")
LATIN_OK = set("ăâîșşțţĂÂÎȘŞȚŢàáäçèéêëíìïñòóôöùúûüÿßæœÀÁÄÇÈÉÊËÍÌÏÑÒÓÔÖÙÚÛÜŸÆŒ")


def decode(data: bytes) -> tuple[str, str]:
    for bom, enc in ((b"\xef\xbb\xbf", "utf-8-sig"), (b"\xff\xfe", "utf-16"), (b"\xfe\xff", "utf-16")):
        if data.startswith(bom):
            return data.decode(enc), enc
    try:
        return data.decode("utf-8"), "utf-8"
    except UnicodeDecodeError:
        pass
    best, best_score, best_text = "latin-1", float("-inf"), data.decode("latin-1")
    for enc in LEGACY:
        try:
            text = data.decode(enc)
        except UnicodeDecodeError:
            continue
        s = score(text)
        if s > best_score:
            best, best_score, best_text = enc, s, text
    return best_text, best


def score(text: str) -> float:
    total = 0.0
    for w in WORDS.findall(text[:200_000]):
        latin = cyr = other = 0
        for ch in w:
            if ch < "\u0080" or ch in LATIN_OK:
                latin += 1
            elif "Ѐ" <= ch <= "џ":
                cyr += 1
            else:
                other += 1
        if other or (latin and cyr):
            total -= 3
            continue
        if not (w.islower() or w.isupper() or (w[0].isupper() and w[1:].islower())):
            total -= 2
            continue
        total += 1
    for ch in text[:200_000]:
        if "\u0080" <= ch < "\u00a0":
            total -= 5
    return total


# ---------------------------------------------------------------- Gutenberg

GB_START = re.compile(r"^\*\*\* ?START OF (THE|THIS) PROJECT GUTENBERG.*$", re.M | re.I)
GB_END = re.compile(r"^\*\*\* ?END OF (THE|THIS) PROJECT GUTENBERG.*$", re.M | re.I)
GB_FIELD = re.compile(r"^(Title|Author|Language):\s*(.+)$", re.M)
LANG_NAMES = {"romanian": "ro", "russian": "ru", "english": "en", "moldavian": "ro"}


def gutenberg(text: str) -> tuple[str, dict[str, str]]:
    m = GB_START.search(text)
    if not m:
        return text, {}
    meta = {}
    for k, v in GB_FIELD.findall(text[:m.start()]):
        k = k.lower()
        if k == "title":
            meta["title"] = v.strip()
        elif k == "author":
            meta["author"] = v.strip()
        elif k == "language":
            meta["language"] = LANG_NAMES.get(v.strip().lower(), "")
    body = text[m.end():]
    e = GB_END.search(body)
    if e:
        body = body[:e.start()]
    meta["source"] = "Project Gutenberg"
    return body, {k: v for k, v in meta.items() if v}


LIBRU_DASHES = re.compile(r"-{20,}")
LIBRU_TITLE = re.compile(r"^\s*(.*?[^\s.]{2})\.\s+(\S.*?)\s*$")


def libru(text: str) -> tuple[str, dict[str, str]]:
    """lib.ru files start with "Author. Title", then a note between two dashed lines."""
    lines = text.split("\n", 40)[:40]
    nonblank = [i for i, l in enumerate(lines) if l.strip()]
    if len(nonblank) < 3 or not LIBRU_DASHES.fullmatch(lines[nonblank[1]].strip()):
        return text, {}
    end = next((i for i in nonblank[2:] if LIBRU_DASHES.search(lines[i])), None)
    if end is None:
        return text, {}
    meta = {"source": "lib.ru"}
    m = LIBRU_TITLE.match(lines[nonblank[0]])
    if m:
        meta["author"], meta["title"] = m.group(1).strip(), m.group(2)
    else:
        meta["title"] = lines[nonblank[0]].strip()
    return "\n".join(text.split("\n")[end + 1:]), meta


def guess_language(text: str) -> str:
    sample = text[:100_000]
    cyr = sum(1 for ch in sample if "Ѐ" <= ch <= "ӿ")
    lat = sum(1 for ch in sample if ch.isalpha() and ch < "ɐ")
    if cyr > lat:
        return "uk" if sum(sample.count(c) for c in "іїєґ") > cyr / 200 else "ru"
    ro = sum(sample.count(c) for c in "ăâîșşțţãº")
    return "ro" if lat and ro / lat > 0.005 else "en"


# ---------------------------------------------------------------- structure

HEADING_WORDS = (r"chapter|book|part|volume|canto|act|scene|prologue|epilogue|preface|introduction|"
                 r"capitolul|partea|cartea|volumul|prolog|epilog|prefață|prefata|introducere|"
                 r"глава|часть|книга|том|пролог|эпилог|предисловие|введение|действие|явление")
PART_WORDS = re.compile(r"^(book|part|volume|partea|cartea|volumul|часть|книга|том)\b", re.I)
HEADING_RE = re.compile(rf"^(?:{HEADING_WORDS})\b[\s\S]{{0,80}}$", re.I)
NUMERAL_RE = re.compile(r"^(?:[IVXLCDM]{1,8}|\d{1,3})[.)]?$")
LIBRU_HEADING = re.compile(r"\s*\x14[^\x14\x15]{1,200}\x15\s*")
CONTROLS = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")
HEADING_DECOR = re.compile(r"^[\s*=~#_-]+|[\s*=~#_-]+$")    # "* ТОМ ПЕРВЫЙ *", "== Chapter 1 =="
BREAK_RE = re.compile(r"^[\s*·•~#=_\-–—.oO◊⁂]+$")


def paragraphs_mode(lines: list[str]) -> str:
    nonblank = [l for l in lines if l.strip()]
    if not nonblank:
        return "line"
    blank = len(lines) - len(nonblank)
    indented = sum(1 for l in nonblank if re.match(r"^(\t| {2,})\S", l))
    long_lines = sum(1 for l in nonblank if len(l) > 120)
    if long_lines > 0.3 * len(nonblank):
        return "line"
    if 0.05 * len(nonblank) < indented < 0.7 * len(nonblank) and blank < indented:
        return "indent"
    if blank > 0.05 * len(nonblank):
        return "blank"
    return "line"


def blocks(lines: list[str], mode: str) -> list[list[str]]:
    """Groups lines into paragraphs (each a list of its lines)."""
    out, cur = [], []
    for line in lines:
        if not line.strip():
            if cur:
                out.append(cur)
                cur = []
            if mode != "blank":
                out.append([])        # keep blank lines visible as gaps
            continue
        if mode == "line" or (mode == "indent" and re.match(r"^(\t| {2,})\S", line) and cur):
            if cur:
                out.append(cur)
            cur = []
        cur.append(line.rstrip())
        if mode == "line":
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return out


def join(lines: list[str]) -> str:
    text = ""
    for line in lines:
        line = line.strip()
        if text.endswith("-") and not text.endswith(" -"):
            text += line                  # "dintr-" + "o", "word--" + "next"
        else:
            text = f"{text} {line}" if text else line
    return text


def is_heading(text: str, lines: list[str]) -> bool:
    if len(lines) > 2 or len(text) > 90:
        return False
    text = _clean_heading(text)
    if HEADING_RE.match(text) or NUMERAL_RE.match(text):
        return True
    letters = [ch for ch in text if ch.isalpha()]
    return (len(letters) >= 3 and all(not ch.islower() for ch in letters)
            and len(text.split()) <= 8 and not text.endswith((",", ";")))


def _clean_heading(text: str) -> str:
    text = CONTROLS.sub("", text).strip()
    return HEADING_DECOR.sub("", text) or text


def parse(data: bytes, name: str = "") -> tuple[Builder, dict[str, str], dict[str, str]]:
    text, encoding = decode(data)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text, meta = gutenberg(text)
    if not meta:
        text, meta = libru(text)
    meta.setdefault("language", guess_language(text))
    if "title" not in meta:
        meta["title"] = name
    lines = text.split("\n")
    mode = paragraphs_mode(lines)
    wrap = _wrap_width(lines)
    b = Builder()
    groups = blocks(lines, mode)
    heading_level = 2 if any(PART_WORDS.match(_clean_heading(join(g))) for g in groups if g) else 1
    last_heading = None
    for gi, g in enumerate(groups):
        if not g:
            continue
        text_ = join(g)
        marked = LIBRU_HEADING.fullmatch(text_)    # lib.ru wraps headings in \x14 ... \x15
        text_ = CONTROLS.sub("", text_).strip()
        if not text_:
            continue
        if BREAK_RE.match(text_) and len(text_) <= 20:
            b.brk(ornament=True)
            last_heading = None
            continue
        if marked or is_heading(text_, g):
            text_ = _clean_heading(text_)
            level = 1 if PART_WORDS.match(text_) else heading_level
            b.text(text_)
            idx = b.end(ibv.HEADING, heading_flags(level))
            b.toc_entry(idx, level, text_)
            last_heading = (idx, level)
            continue
        if (last_heading is not None and len(g) == 1 and len(text_) < 70
                and not text_.endswith((".", ",", ";", ":", "!", "?", "»", "”", '"'))):
            # chapter subtitle on its own line: join it to the heading
            idx, level = last_heading
            item = b.items[idx]
            item.st.append("\u2028" + text_, 0)
            sep = " " if b.toc[-1].title.endswith(".") else ". "
            b.toc[-1].title += sep + text_
            last_heading = None
            continue
        last_heading = None
        if wrap and _is_verse(g, wrap, mode):
            for line in g:
                b.text(line.strip())
                b.end(ibv.VERSE)
            if gi + 1 < len(groups):
                b.brk()
            continue
        b.text(text_)
        b.end(ibv.PARA)
    _demote_contents_list(b)
    meta.setdefault("source", name)
    return b, meta, {"encoding": encoding, "paragraphs": mode}


def _demote_contents_list(b: Builder) -> None:
    """A printed table of contents reads as a run of headings. A run of 3+
    headings (with at most two short lines between neighbours), most of which
    come back later as headings, is one:
    its lines become plain unindented paragraphs and leave the TOC."""
    def key(i):
        return re.sub(r"\W+", " ", b.items[i].st.text).strip().casefold()
    heads = [i for i, it in enumerate(b.items) if it.kind == ibv.HEADING]
    runs, run = [], []
    def bridged(a, c):      # only short lines between two headings
        return c - a <= 3 and all(len(b.items[k].st.text) < 80 for k in range(a + 1, c))
    for i in heads:
        if run and not bridged(run[-1], i):
            runs.append(run)
            run = []
        run.append(i)
    runs.append(run)
    demoted = set()
    for run in runs:
        if len(run) < 3:
            continue
        later = {key(i) for i in heads if i > run[-1]}
        if sum(key(i) in later for i in run) * 2 >= len(run):
            for i in run:
                b.items[i].kind, b.items[i].flags = ibv.PARA, ibv.NO_INDENT | ibv.ALIGN_LEFT
            demoted.update(run)
    if demoted:
        b.toc = [t for t in b.toc if t.target not in demoted]


def _wrap_width(lines: list[str]) -> int:
    lengths = sorted(len(l.rstrip()) for l in lines if l.strip())
    if len(lengths) < 20:
        return 0
    return lengths[int(len(lengths) * 0.9)]


def _is_verse(g: list[str], wrap: int, mode: str) -> bool:
    if mode == "line" or len(g) < 2 or wrap > 120:
        return False
    lengths = [len(l.strip()) for l in g]
    return max(lengths) < 0.75 * wrap and statistics.mean(lengths) < 0.6 * wrap
