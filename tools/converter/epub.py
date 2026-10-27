"""Intrarea EPUB 2/3 (CONV-2).

container.xml -> pachetul OPF -> metadatele, manifestul și spine-ul. Documentele
din spine se citesc în ordine cu un parser HTML (îngăduitor: EPUB-urile reale au
entități HTML și etichete neînchise pe care un parser XML strict le respinge).

Corespondența, pe scurt:
  p, div, li, td ...          paragraf (PARA)
  h1-h6                        HEADING, nivelul 1-4
  blockquote                   QUOTE
  poem / stanza / verse        câte un VERSE pe rând, BREAK între strofe
  hr                           BREAK cu ornament
  br                           rupere de rând în paragraf (U+2028)
  b/strong, i/em/cite          bold, italic; și din stilurile inline și din
                               regulile simple pe clase din CSS-ul cărții
  semnele și textele notelor   scoase (în afara lui v0.1)
  orice altceva                textul lui, ca text simplu

Cuprinsul vine din documentul nav al EPUB 3, altfel din NCX, altfel din
titluri. Fiecare id de element e o ancoră, deci legăturile din cuprins spre
"ch3.xhtml#sec2" ajung la paragraful potrivit.
"""
from __future__ import annotations

import html.parser
import io
import posixpath
import re
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile

import ibv
from doc import Builder, heading_flags

BLOCK = {"p", "div", "li", "dt", "dd", "pre", "section", "article", "header", "footer", "main",
         "aside", "figure", "figcaption", "table", "tr", "td", "th", "caption", "ul", "ol", "dl",
         "body", "nav", "address", "center", "hgroup", "details", "summary", "blockquote",
         "h1", "h2", "h3", "h4", "h5", "h6", "hr"}
SKIP = {"head", "script", "style", "svg", "math", "rt", "rp", "noscript", "template", "button",
        "select", "textarea", "iframe", "object", "video", "audio"}
VOID = {"br", "hr", "img", "meta", "link", "input", "col", "area", "base", "wbr", "source", "embed"}
BOLD_TAGS = {"b", "strong", "th"}
ITALIC_TAGS = {"i", "em", "cite", "dfn", "var", "address"}
VERSE_HINT = re.compile(r"\b(poem|poetry|verse|stanza|lg|linegroup|line-group|song|lyrics|"
                        r"z3998:poem|z3998:verse|z3998:song|z3998:hymn|vers|strofa|стих)\b", re.I)
NOTE_HINT = re.compile(r"\b(noteref|footnote|endnote|rearnote|endnotes|footnotes|rearnotes|"
                       r"doc-noteref|doc-footnote|doc-endnotes|annoref)\b", re.I)
BOILERPLATE_HINT = re.compile(r"\bpg-boilerplate\b")    # Antetul și licența Project Gutenberg
EPIGRAPH_HINT = re.compile(r"\b(epigraph|z3998:epigraph|doc-epigraph|motto)\b", re.I)


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _find(root: ET.Element, name: str) -> list[ET.Element]:
    return [e for e in root.iter() if _local(e.tag) == name]


class Epub:
    def __init__(self, data: bytes | str):
        self.zip = zipfile.ZipFile(io.BytesIO(data) if isinstance(data, bytes) else data)
        self.names = {n: n for n in self.zip.namelist()}
        self.lower = {n.lower(): n for n in self.zip.namelist()}
        container = ET.fromstring(self.read("META-INF/container.xml"))
        rootfile = _find(container, "rootfile")[0].get("full-path")
        self.opf_path = rootfile
        self.opf_dir = posixpath.dirname(rootfile)
        self.opf = ET.fromstring(self.read(rootfile))
        self.manifest: dict[str, dict] = {}
        for item in _find(self.opf, "item"):
            href = self.resolve(self.opf_dir, item.get("href", ""))
            self.manifest[item.get("id")] = {"href": href, "type": item.get("media-type", ""),
                                             "props": item.get("properties", "")}
        spine = _find(self.opf, "spine")
        self.spine_el = spine[0] if spine else None
        self.spine = []
        for ref in _find(self.opf, "itemref"):
            it = self.manifest.get(ref.get("idref"))
            if it and ref.get("linear", "yes") != "no":
                self.spine.append(it["href"])

    def read(self, path: str) -> bytes:
        name = self.names.get(path) or self.lower.get(path.lower())
        if name is None:
            raise KeyError(path)
        return self.zip.read(name)

    def exists(self, path: str) -> bool:
        return path in self.names or path.lower() in self.lower

    @staticmethod
    def resolve(base_dir: str, href: str) -> str:
        href = urllib.parse.unquote(href.split("#", 1)[0])
        return posixpath.normpath(posixpath.join(base_dir, href)) if href else base_dir

    # metadatele
    def metadata(self) -> dict[str, str]:
        md = _find(self.opf, "metadata")
        md = md[0] if md else self.opf
        def first(name):
            for e in md.iter():
                if _local(e.tag) == name and (e.text or "").strip():
                    return e.text.strip()
            return ""
        creators = [e.text.strip() for e in md.iter() if _local(e.tag) == "creator" and (e.text or "").strip()]
        meta = {"title": first("title"), "author": ", ".join(dict.fromkeys(creators)),
                "language": first("language"), "description": _strip_tags(first("description"))}
        date = first("date")
        m = re.search(r"\b(\d{4})\b", date)
        if m:
            meta["year"] = m.group(1)
        return {k: v for k, v in meta.items() if v}

    def cover_bytes(self) -> bytes | None:
        md = _find(self.opf, "meta")
        cover_id = next((m.get("content") for m in md if m.get("name") == "cover"), None)
        cands = []
        if cover_id and cover_id in self.manifest:
            cands.append(self.manifest[cover_id])
        cands += [it for it in self.manifest.values() if "cover-image" in it["props"]]
        cands += [it for k, it in self.manifest.items()
                  if it["type"].startswith("image/") and "cover" in (k + it["href"]).lower()]
        for it in cands:
            if it["type"].startswith("image/") and self.exists(it["href"]):
                return self.read(it["href"])
        for ref in _find(self.opf, "reference"):      # ghidul EPUB 2: pagina copertei -> prima ei imagine
            if ref.get("type") == "cover":
                page = self.resolve(self.opf_dir, ref.get("href", ""))
                if self.exists(page):
                    m = re.search(rb'(?:src|xlink:href|href)="([^"]+\.(?:jpe?g|png|gif))"', self.read(page), re.I)
                    if m:
                        img = self.resolve(posixpath.dirname(page), m.group(1).decode())
                        if self.exists(img):
                            return self.read(img)
        return None

    # cuprinsul: listă de (cale, fragment, nivel, titlu)
    def toc(self) -> list[tuple[str, str, int, str]]:
        nav = next((it for it in self.manifest.values() if "nav" in it["props"].split()), None)
        if nav and self.exists(nav["href"]):
            out = self._nav_toc(nav["href"])
            if out:
                return out
        ncx_id = self.spine_el.get("toc") if self.spine_el is not None else None
        ncx = self.manifest.get(ncx_id) if ncx_id else None
        ncx = ncx or next((it for it in self.manifest.values() if it["type"] == "application/x-dtbncx+xml"), None)
        if ncx and self.exists(ncx["href"]):
            return self._ncx_toc(ncx["href"])
        return []

    def _ncx_toc(self, path: str) -> list:
        root = ET.fromstring(self.read(path))
        base = posixpath.dirname(path)
        out = []
        def walk(el, depth):
            for np_ in el:
                if _local(np_.tag) != "navPoint":
                    continue
                label = " ".join("".join(t.itertext()) for t in np_ if _local(t.tag) == "navLabel").strip()
                content = next((c for c in np_ if _local(c.tag) == "content"), None)
                if content is not None:
                    src = content.get("src", "")
                    out.append((self.resolve(base, src), _fragment(src), depth, label))
                walk(np_, depth + 1)
        for nm in _find(root, "navMap"):
            walk(nm, 1)
        return out

    def _nav_toc(self, path: str) -> list:
        p = _NavParser()
        p.feed(_decode(self.read(path)))
        base = posixpath.dirname(path)
        return [(self.resolve(base, href), _fragment(href), depth, label) for href, depth, label in p.entries]

    # CSS: clasă -> biți de stil / aliniere / ascuns
    def css_rules(self) -> dict[str, dict]:
        rules: dict[str, dict] = {}
        for it in self.manifest.values():
            if it["type"] != "text/css" or not self.exists(it["href"]):
                continue
            css = re.sub(r"/\*.*?\*/", "", _decode(self.read(it["href"])), flags=re.S)
            for sels, body in re.findall(r"([^{}]+)\{([^{}]*)\}", css):
                decl = _declarations(body)
                if not decl:
                    continue
                for sel in sels.split(","):
                    m = re.fullmatch(r"\s*(?:[a-z0-9]+)?\.([\w-]+)\s*", sel, re.I)
                    if m:
                        rules.setdefault(m.group(1), {}).update(decl)
        return rules


def _declarations(body: str) -> dict:
    d = {}
    for prop, val in re.findall(r"([\w-]+)\s*:\s*([^;]+)", body):
        prop, val = prop.lower(), val.strip().lower()
        if prop == "font-style":
            d["italic"] = val.startswith(("italic", "oblique"))
        elif prop == "font-weight":
            d["bold"] = val.startswith("bold") or (val.isdigit() and int(val) >= 600)
        elif prop == "text-align":
            d["align"] = {"center": ibv.ALIGN_CENTER, "right": ibv.ALIGN_RIGHT, "left": ibv.ALIGN_LEFT,
                          "justify": 0}.get(val.split()[0])
        elif prop == "display" and val.startswith("none"):
            d["hidden"] = True
    return {k: v for k, v in d.items() if v is not None}


def _fragment(href: str) -> str:
    return urllib.parse.unquote(href.split("#", 1)[1]) if "#" in href else ""


def _decode(b: bytes) -> str:
    m = re.match(rb'<\?xml[^>]*encoding=["\']([\w.-]+)["\']', b)
    enc = m.group(1).decode() if m else "utf-8"
    try:
        return b.decode(enc)
    except (LookupError, UnicodeDecodeError):
        return b.decode("utf-8", errors="replace")


def _strip_tags(s: str) -> str:
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", s)).strip()


class _NavParser(html.parser.HTMLParser):
    """Citește lista <nav epub:type="toc"> dintr-un document nav EPUB 3."""
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.in_toc = 0
        self.depth = 0
        self.href = None
        self.label = ""
        self.entries = []
        self.nav_depth = 0

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "nav":
            self.nav_depth += 1
            kind = (a.get("epub:type", "") + " " + a.get("role", "")).lower()
            if "toc" in kind and not self.in_toc:
                self.in_toc = self.nav_depth
        elif self.in_toc and tag == "ol":
            self.depth += 1
        elif self.in_toc and tag == "a":
            self.href, self.label = a.get("href", ""), ""

    def handle_endtag(self, tag):
        if tag == "nav":
            if self.in_toc == self.nav_depth:
                self.in_toc = 0
            self.nav_depth -= 1
        elif self.in_toc and tag == "ol":
            self.depth -= 1
        elif self.in_toc and tag == "a" and self.href is not None:
            label = re.sub(r"\s+", " ", self.label).strip()
            if label and self.href:
                self.entries.append((self.href, max(self.depth, 1), label))
            self.href = None

    def handle_data(self, data):
        if self.href is not None:
            self.label += data


class _Ctx:
    __slots__ = ("tag", "style", "align", "verse", "quote", "skip", "kind", "level", "brs", "list_kind", "n",
                 "stanza")

    def __init__(self, tag, parent=None):
        self.tag = tag
        self.style = parent.style if parent else 0
        self.align = parent.align if parent else 0
        self.verse = parent.verse if parent else False
        self.quote = parent.quote if parent else False
        self.skip = parent.skip if parent else False
        self.kind = None
        self.level = 0
        self.brs = 0
        self.list_kind = None
        self.n = 0
        self.stanza = False


class _DocParser(html.parser.HTMLParser):
    """Un document XHTML din spine -> apeluri Builder."""

    def __init__(self, b: Builder, path: str, css: dict[str, dict]):
        super().__init__(convert_charrefs=True)
        self.b, self.path, self.css = b, path, css
        self.stack: list[_Ctx] = [_Ctx("#root")]
        self.b.anchor((path, ""))
        self.para_kind = ibv.PARA
        self.para_flags = 0
        self.prefix = ""          # buline de listă, scrisă înaintea primului text al elementului

    @property
    def top(self) -> _Ctx:
        return self.stack[-1]

    def flush(self):
        if self.top.skip:
            return
        kind, flags = self.para_kind, self.para_flags
        self.b.end(kind, flags)
        self.para_kind, self.para_flags = ibv.PARA, 0

    def _start_paragraph(self, ctx: _Ctx):
        """Pune tipul și indicatorii pentru paragraful care începe cu blocul ăsta."""
        if ctx.kind == ibv.HEADING:
            self.para_kind = ibv.HEADING
            self.para_flags = heading_flags(ctx.level, ctx.align or ibv.ALIGN_CENTER)
        elif ctx.verse:
            self.para_kind, self.para_flags = ibv.VERSE, ctx.align
        elif ctx.quote:
            self.para_kind, self.para_flags = ibv.QUOTE, ctx.align
        else:
            self.para_kind, self.para_flags = ibv.PARA, ctx.align

    def handle_starttag(self, tag, attrs):
        a = {k: (v or "") for k, v in attrs}
        parent = self.top
        if tag in VOID:
            self._void(tag, a, parent)
            return
        ctx = _Ctx(tag, parent)
        self.stack.append(ctx)
        if ctx.skip:
            return
        hints = " ".join((a.get("class", ""), a.get("epub:type", ""), a.get("role", "")))
        if tag in SKIP or NOTE_HINT.search(hints) or BOILERPLATE_HINT.search(hints):
            ctx.skip = True
            return
        cls_rules = {}
        for c in a.get("class", "").split():
            cls_rules.update(self.css.get(c, {}))
        inline = _declarations(a.get("style", ""))
        rules = {**cls_rules, **inline}
        if rules.get("hidden"):
            ctx.skip = True
            return
        if tag in BOLD_TAGS or rules.get("bold"):
            ctx.style |= ibv.BOLD
        elif rules.get("bold") is False:
            ctx.style &= ~ibv.BOLD
        if tag in ITALIC_TAGS or rules.get("italic"):
            ctx.style |= ibv.ITALIC
        elif rules.get("italic") is False:
            ctx.style &= ~ibv.ITALIC
        if "align" in rules:
            ctx.align = rules["align"]
        elif tag == "center":
            ctx.align = ibv.ALIGN_CENTER
        if VERSE_HINT.search(hints):
            ctx.verse = ctx.stanza = True
        if tag == "blockquote" or EPIGRAPH_HINT.search(hints):
            ctx.quote = True
        if tag == "pre":
            ctx.verse = True
        if tag in ("h1", "h2", "h3", "h4", "h5", "h6"):
            ctx.kind, ctx.level = ibv.HEADING, min(int(tag[1]), 4)
            ctx.style &= ~ibv.BOLD            # titlurile sunt bold prin tipul lor
        if tag in ("ul", "ol"):
            ctx.list_kind = tag
        if a.get("id"):
            self.b.anchor((self.path, a["id"]))
        if tag == "a" and a.get("name"):
            self.b.anchor((self.path, a["name"]))
        if tag in BLOCK:
            self.flush()
            self._start_paragraph(ctx)
            if tag == "li":
                lst = next((c for c in reversed(self.stack[:-1]) if c.list_kind), None)
                if lst is not None:
                    lst.n += 1
                    self.prefix = "• " if lst.list_kind == "ul" else f"{lst.n}. "

    def _void(self, tag, a, parent):
        if parent.skip:
            return
        if a.get("id"):
            self.b.anchor((self.path, a["id"]))
        if tag == "br":
            if parent.verse:
                if not self.b.has_text():
                    self.b.brk()              # <br/><br/>: un rând gol între strofe
                self.flush()
                self._start_paragraph(parent)
                for c in reversed(self.stack):
                    c.brs += 1
                    if c.tag in BLOCK:
                        break
            else:
                self.b.line_break()
        elif tag == "hr":
            self.b.brk(ornament=True)

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        # se închide până la eticheta pereche (tolerează copii neînchiși)
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                break
        else:
            return
        while len(self.stack) > i:
            ctx = self.stack.pop()
            if ctx.skip and not self.top.skip:
                continue
            if ctx.tag in BLOCK and not ctx.skip:
                self.flush()
                # se termină o strofă: un element marcat ca strofă sau un bloc de rânduri despărțite cu <br>
                if ctx.verse and (ctx.stanza or ctx.brs) and not self.top.skip:
                    self.b.brk()
                if ctx.tag == "li":
                    self.prefix = ""
                self._start_paragraph(self.top)

    def handle_data(self, data):
        if self.top.skip:
            return
        if self.top.tag == "pre" or any(c.tag == "pre" for c in self.stack):
            lines = data.split("\n")
            for k, line in enumerate(lines):
                if k:
                    self.flush()
                    self._start_paragraph(self.top)
                self.b.text(line, self.top.style)
            return
        if self.prefix and data.strip():
            self.b.text(self.prefix, self.top.style)
            self.prefix = ""
        self.b.text(data, self.top.style)

    def close(self):
        super().close()
        while len(self.stack) > 1:
            self.handle_endtag(self.top.tag)
        self.flush()


def parse(data: bytes, name: str = "") -> tuple[Builder, dict[str, str], dict]:
    ep = Epub(data)
    meta = ep.metadata()
    meta.setdefault("title", name)
    meta.setdefault("language", "")
    meta["source"] = name
    css = ep.css_rules()
    b = Builder()
    for path in ep.spine:
        if not ep.exists(path):
            continue
        p = _DocParser(b, path, css)
        p.feed(_decode(ep.read(path)))
        p.close()
        # un document nou din spine începe o secțiune nouă: textul nu trece niciodată peste el
        b.end()
    for path, frag, depth, label in ep.toc():
        b.toc_entry((path, frag) if frag and (path, frag) in b.anchors else (path, ""), depth, label)
    info = {"spine": len(ep.spine), "toc": "nav/ncx" if b.toc else "headings", "css_classes": len(css)}
    return b, meta, {**info, "cover_bytes": ep.cover_bytes()}
