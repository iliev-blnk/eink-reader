"""Intrarea FictionBook 2 (CONV-9), formatul obișnuit al cărților electronice rusești.

FB2 e un singur fișier XML (uneori arhivat ca .fb2.zip) care spune deja ce e
fiecare parte, deci corespondența e directă:

  section/title     HEADING, nivelul = adâncimea secțiunii; și intrare în cuprins
  p                 PARA
  epigraph, cite    QUOTE (text-author aliniat la dreapta)
  poem/stanza/v     câte un VERSE pe rând, BREAK între strofe
  subtitle          paragraf centrat ("* * *" devine un BREAK cu ornament)
  empty-line        BREAK
  emphasis/strong   italic/bold
  a type="note"     semnul notei, scos; corpul notelor se sare
  binary            imaginea copertei, când coverpage arată spre ea
"""
from __future__ import annotations

import base64
import io
import re
import xml.etree.ElementTree as ET
import zipfile

import ibv
from doc import Builder, heading_flags

XLINK = "{http://www.w3.org/1999/xlink}href"
STARS = re.compile(r"^[\s*·•~#=_\-–—.oO◊⁂]+$")


def _local(tag) -> str:
    return tag.rsplit("}", 1)[-1] if isinstance(tag, str) else ""


def _child(el, name):
    return next((c for c in el if _local(c.tag) == name), None)


def _children(el, name):
    return [c for c in el if _local(c.tag) == name]


def _text(el) -> str:
    return re.sub(r"\s+", " ", "".join(el.itertext())).strip() if el is not None else ""


def unzip(data: bytes) -> bytes:
    if data[:2] == b"PK":
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            name = next(n for n in z.namelist() if n.lower().endswith(".fb2"))
            return z.read(name)
    return data


class _Walker:
    def __init__(self, b: Builder):
        self.b = b

    def inline(self, el, style: int) -> None:
        """Textul unui element de tip paragraf, cu emphasis/strong ca stiluri."""
        if el.text:
            self.b.text(el.text, style)
        for c in el:
            name = _local(c.tag)
            if name == "a" and c.get("type") == "note":
                pass                                   # semnul notei de subsol
            elif name == "emphasis":
                self.inline(c, style | ibv.ITALIC)
            elif name == "strong":
                self.inline(c, style | ibv.BOLD)
            elif name != "image":
                self.inline(c, style)
            if c.tail:
                self.b.text(c.tail, style)

    def para(self, el, kind=ibv.PARA, flags=0, style=0):
        if el.get("id"):
            self.b.anchor(el.get("id"))
        self.inline(el, style)
        return self.b.end(kind, flags)

    def block(self, el, depth: int, kind=ibv.PARA) -> None:
        for c in el:
            name = _local(c.tag)
            if name == "p":
                self.para(c, kind)
            elif name == "section":
                self.section(c, depth + 1)
            elif name == "title":
                self.title(c, depth)
            elif name in ("epigraph", "cite"):
                self.block(c, depth, ibv.QUOTE)
            elif name == "text-author":
                self.para(c, ibv.QUOTE, ibv.ALIGN_RIGHT, ibv.ITALIC)
            elif name == "poem":
                self.poem(c, depth, kind)
            elif name == "subtitle":
                if STARS.match(_text(c) or "x"):
                    self.b.brk(ornament=True)
                else:
                    self.para(c, ibv.PARA, ibv.ALIGN_CENTER, ibv.BOLD)
            elif name == "empty-line":
                self.b.brk()
            elif name == "table":
                for row in c.iter():
                    if _local(row.tag) in ("td", "th"):
                        self.para(row, kind)
            elif name not in ("image", "annotation"):
                self.block(c, depth, kind)

    def poem(self, el, depth, kind):
        for c in el:
            name = _local(c.tag)
            if name == "title":
                for p in _children(c, "p"):
                    self.para(p, ibv.PARA, ibv.ALIGN_CENTER, ibv.BOLD)
            elif name == "epigraph":
                self.block(c, depth, ibv.QUOTE)
            elif name == "stanza":
                for v in c:
                    vn = _local(v.tag)
                    if vn == "v":
                        self.para(v, ibv.VERSE)
                    elif vn in ("title", "subtitle"):
                        self.para(v, ibv.VERSE, ibv.ALIGN_CENTER, ibv.BOLD)
                self.b.brk()
            elif name in ("text-author", "date"):
                self.para(c, ibv.VERSE, ibv.ALIGN_RIGHT, ibv.ITALIC)

    def title(self, el, depth: int) -> None:
        lines = [p for p in el if _local(p.tag) in ("p", "empty-line")]
        if not lines:
            return
        first = True
        for p in lines:
            if _local(p.tag) != "p" or not _text(p):
                continue
            if not first:
                self.b.line_break()
            self.inline(p, 0)
            first = False
        level = min(max(depth, 1), 4)
        idx = self.b.end(ibv.HEADING, heading_flags(level))
        if idx is not None:
            self.b.toc_entry(idx, level, " ".join(_text(p) for p in lines if _local(p.tag) == "p"))

    def section(self, el, depth: int) -> None:
        if el.get("id"):
            self.b.anchor(el.get("id"))
        self.block(el, depth)


def parse(data: bytes, name: str = "") -> tuple[Builder, dict[str, str], dict]:
    root = ET.fromstring(unzip(data))
    desc = _child(root, "description")
    ti = _child(desc, "title-info") if desc is not None else None
    meta: dict[str, str] = {"source": name}
    cover_bytes = None
    if ti is not None:
        meta["title"] = _text(_child(ti, "book-title"))
        authors = []
        for a in _children(ti, "author"):
            parts = [_text(_child(a, k)) for k in ("first-name", "middle-name", "last-name")]
            full = " ".join(p for p in parts if p) or _text(_child(a, "nickname"))
            if full:
                authors.append(full)
        meta["author"] = ", ".join(authors)
        meta["language"] = _text(_child(ti, "lang"))
        meta["description"] = _text(_child(ti, "annotation"))
        date = _child(ti, "date")
        m = re.search(r"\b(\d{4})\b", (date.get("value", "") + " " + _text(date)) if date is not None else "")
        if m:
            meta["year"] = m.group(1)
        cp = _child(ti, "coverpage")
        img = _child(cp, "image") if cp is not None else None
        href = (img.get(XLINK) or img.get("href") or "") if img is not None else ""
        if href.startswith("#"):
            for bin_ in _children(root, "binary"):
                if bin_.get("id") == href[1:]:
                    cover_bytes = base64.b64decode(bin_.text or "")
    meta.setdefault("title", name)
    b = Builder()
    w = _Walker(b)
    for body in _children(root, "body"):
        if body.get("name") in ("notes", "comments"):
            continue
        for c in body:
            n = _local(c.tag)
            if n == "title":
                b.end()
                # pagina de titlu a cărții: centrată, nu e intrare în cuprins
                for p in _children(c, "p"):
                    w.para(p, ibv.HEADING, heading_flags(1))
            elif n == "epigraph":
                w.block(c, 0, ibv.QUOTE)
            elif n == "section":
                w.section(c, 1)
    return b, {k: v for k, v in meta.items() if v}, {"cover_bytes": cover_bytes}
