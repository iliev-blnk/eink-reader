# Wikitext -> formatul simplu pe rânduri pentru mock.c:  "# titlu", "P text", "V vers", "S" (separator de strofă)
import html, re, sys
S = sys.argv[1]

def strip_templates(t):
    while True:
        n = re.sub(r"\{\{[^{}]*\}\}", "", t)
        if n == t: return n
        t = n

def inline(s):
    s = re.sub(r"<ref[^>]*/>|<ref[^>]*>.*?</ref>", "", s, flags=re.S)
    s = re.sub(r"\[\[(?:[^|\]]*\|)?([^\]]*)\]\]", r"\1", s)
    s = s.replace("'''", "").replace("''", "")
    s = re.sub(r"</?[a-zA-Z][^>]*>", "", s)
    s = html.unescape(s)
    return re.sub(r"[ \t\r\n]+", " ", s).strip()

def prose(name, start):
    t = open(f"{S}/texts/{name}.wiki", encoding="utf-8").read()
    t = strip_templates(t)
    t = re.sub(r"<ref[^>]*>.*?</ref>", "", t, flags=re.S)
    out, para = [], []
    def flush():
        if para:
            p = inline(" ".join(para))
            if p: out.append("P " + p)
            para.clear()
    started = False
    for line in t.split("\n"):
        m = re.match(r"^\s*(=+)\s*(.*?)\s*\1\s*$", line)
        if m:
            flush()
            h = inline(m.group(2))
            if h: out.append("# " + h); started = True
            continue
        if not started: continue
        if line.strip() == "": flush(); continue
        para.append(line.lstrip(":"))
    flush()
    # Se scoate tot ce e înaintea primului titlu dinaintea lui `start`.
    i = next(k for k, l in enumerate(out) if start in l)
    while i > 0 and not out[i].startswith("#"): i -= 1
    return out[i:]

def verse(name):
    t = open(f"{S}/texts/{name}.wiki", encoding="utf-8").read()
    body = t[t.index("<poem>") + 6 : t.index("</poem>")]
    out, prev_blank = ["# Luceafărul"], True
    for line in body.split("\n"):
        l = inline(line)
        if not l:
            if not prev_blank: out.append("S")
            prev_blank = True
            continue
        out.append("V " + l)
        prev_blank = False
    return out

books = {
    "amintiri": prose("amintiri", "Stau câteodată"),
    "luceafarul": verse("luceafarul"),
    "moara": prose("moara", "Omul să fie"),
    "belye": prose("belye", "Была чудная ночь"),
}
for k, lines in books.items():
    open(f"{S}/{k}.txt", "w", encoding="utf-8").write("\n".join(lines) + "\n")
    heads = [l for l in lines if l.startswith("#")]
    print(k, len(lines), "lines,", sum(len(l) for l in lines), "chars, headings:", heads[:6], "...", len(heads))
