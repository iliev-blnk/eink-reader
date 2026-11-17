#!/usr/bin/env python3
"""TeX ca arbitru al ruperii în rânduri.

    sim/build/sim breaks corpus/out/*.ibv --fonts tools/fonttool/out/fam > rupturi.txt
    tools/texref/texref.py rupturi.txt [--fonts DIR] [--work DIR] [--examples N] [--json FIȘIER]

Ia rândurile pe care le-a așezat dispozitivul (`sim breaks`), reface din carte
și din fișierele de font, independent de codul C, fiecare paragraf justificat
ca listă de cutii, spații și puncte de rupere, îl dă algoritmului de rupere
din LuaTeX cu aceleași lățimi și aceiași parametri și compară rupturile.

Ca să se poată spune și de ce diferă un paragraf, programul are și un model al
algoritmului din TeX (tex.web, §813–§890), scris aici în Python, cu abaterile
dispozitivului ca opțiuni. Modelul fără abateri trebuie să dea
exact rupturile și penalizarea totală din LuaTeX, iar cel cu abaterile pe care
le are versiunea motorului din fișier (LAYOUT_VERSION, în antetul `sim breaks`)
exact rândurile dispozitivului; raportul spune dacă se întâmplă așa. Apoi
fiecare abatere e pornită singură și se numără paragrafele pe care le schimbă.
"""
import argparse
import collections
import json
import multiprocessing
import os
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "converter"))
sys.path.insert(0, os.path.join(HERE, "..", "fonttool"))
import ibf   # noqa: E402
import ibv   # noqa: E402

FONT_UNIT = 64                 # lățimile fonturilor sunt în 1/64 px
SP = 1024                      # 1/64 px în unitățile TeX (sp), cu 1 px = 1 pt
FIT_MAX = 2048                 # LAYOUT_FIT_MAX din layout.h

# Parametrii ruperii, aceiași în layout.c și în linebreak.lua.
LINE, HYPHEN, DOUBLE_HYPHEN, FINAL_HYPHEN, ADJ = 10, 100, 100000, 5000, 10000
TOLERANCE = 400
BAD_MAX = 10000000             # badness-ul maxim al dispozitivului (a doua trecere)
INF_BAD = 10000                # badness-ul maxim din TeX
EJECT = -10000
AWFUL = 1 << 100               # mai mare decât orice penalizare totală (în TeX, awful_bad = 2^30 - 1)

# Abaterile de la TeX pe care le știe modelul.
FLAGS = {
    "one_node": "o singură stare pe punct de rupere, nu câte una pe clasă de potrivire",
    "exact_bad": "badness exact, 100·r³ rotunjit în jos și fără plafon, nu aproximarea din TeX",
    "shrink_total": "îngustarea rândului e o treime din suma spațiilor, rotunjită o dată pe rând",
    "fill_natural": "ultimul rând nu se îngustează: trebuie să încapă la lățimea lui",
    "no_first_adj": "primul rând nu e comparat cu o clasă de potrivire dinainte",
}
# Ce abateri are dispozitivul, după LAYOUT_VERSION din antetul `sim breaks`.
# Până la versiunea 3 le avea pe toate cinci; trei erau greșeli și au fost
# îndreptate în versiunea 4 după prima rulare a acestui program.
ENGINE = {3: tuple(FLAGS), 4: ("exact_bad", "shrink_total")}
NONE = dict.fromkeys(FLAGS, False)


# ------------------------------------------------------------------ fonturi

class Face:
    """Lățimile și kerningul unui font, cum le caută dispozitivul."""

    def __init__(self, path):
        font = ibf.parse(open(path, "rb").read()).font
        self.adv = {g.cp: g.advance for g in font.glyphs}
        self.kern_as = {g.cp: g.cp if g.kern_as is None else g.kern_as for g in font.glyphs}
        self.kerns = font.kerns

    def glyph(self, cp):
        return cp if cp in self.adv else 0xFFFD

    def kern(self, a, b):
        v = self.kerns.get((a, b))
        if v is None:
            v = self.kerns.get((self.kern_as[a], self.kern_as[b]), 0)
        return v


def load_faces(prefix):
    faces = []
    for suffix in ("r", "b", "i", "bi"):
        path = "%s-%s.ibf" % (prefix, suffix)
        faces.append(Face(path) if os.path.exists(path) else None)
    return [f or faces[0] for f in faces]


# ----------------------------------------------------------------- măsurare

def measure(rec, faces, hyphenate):
    """Înregistrarea ca listă de elemente, după regulile din docs (LAY-5):

    ("box", lățime, octet)              litere între două puncte de rupere
    ("glue", lățime, octet, următorul)  spațiu
    ("nbsp", lățime)                    U+00A0
    ("shy", pre, rep, octet, următorul) cratimă moale
    ("dash", rep, octet)                după o liniuță în interiorul unui cuvânt
    ("zwsp", octet, următorul)          U+200B
    ("hard", octet, următorul)          U+2028

    Kerningul se aplică doar între litere alăturate cu același stil; trece peste
    o cratimă moale nefolosită și se pierde dacă rândul se rupe între ele.
    """
    style = bytearray(len(rec.text.encode("utf-8")))
    for start, length, st in rec.runs:
        style[start:start + length] = bytes([st & 3]) * length
    items = []
    box = None                   # [lățime, octet]
    prev = prev_face = None
    pending = None               # ruperea din cuvânt care așteaptă kerningul literei următoare
    in_word = False
    at = 0
    text = rec.text

    def close():
        nonlocal box
        if box:
            items.append(("box", box[0], box[1]))
        box = None

    for i, ch in enumerate(text):
        cp = ord(ch)
        nx = at + len(ch.encode("utf-8"))
        face = faces[style[at]]
        if cp in (0x20, 0xA0):
            close()
            w = face.adv[face.glyph(0x20)]
            items.append(("glue", w, at, nx) if cp == 0x20 else ("nbsp", w))
            prev, in_word, pending = None, False, None
        elif cp == 0x2028:
            close()
            items.append(("hard", at, nx))
            prev, in_word, pending = None, False, None
        elif cp == 0xAD:
            if hyphenate:
                close()
                h = face.glyph(0x2D)
                pre = face.adv[h] + (face.kern(prev, h) if prev is not None and face is prev_face else 0)
                items.append(["shy", pre, 0, at, nx])
                pending = items[-1]
        elif cp == 0x200B:
            close()
            items.append(("zwsp", at, nx))
            prev, pending = None, None
        else:
            g = face.glyph(cp)
            k = face.kern(prev, g) if prev is not None and face is prev_face else 0
            if pending is not None:
                pending[2 if pending[0] == "shy" else 1] += k
                k = 0
            pending = None
            if box is None:
                box = [0, at]
            box[0] += face.adv[g] + k
            prev, prev_face = g, face
            if cp in (0x2D, 0x2013, 0x2014) and in_word and i + 1 < len(text) and text[i + 1] != " ":
                close()
                items.append(["dash", 0, nx])
                pending = items[-1]
            in_word = True
        at = nx
    close()
    return [tuple(it) for it in items], at


def unusual(items):
    """Spații și ruperi lipite între ele sau la capete. TeX aruncă spațiile de la
    începutul unui rând, dispozitivul nu, deci aici cele două nu rezolvă aceeași problemă."""
    soft = ("glue", "nbsp", "zwsp", "hard")
    if not items or items[0][0] in soft or items[-1][0] in soft:
        return True
    return any(a[0] in soft and b[0] in soft for a, b in zip(items, items[1:]))


class Par:
    """Un paragraf pregătit pentru rupere: sumele prefix la fiecare punct de rupere."""

    def __init__(self, pid, items, indent, hsize, text_len):
        self.pid, self.items, self.indent, self.hsize, self.text_len = pid, items, indent, hsize, text_len
        # Un punct de rupere: (elementul, sfârșitul rândului care se rupe aici, începutul
        # următorului, penalizarea, cu cratimă, ultimul). Un capăt de rând e
        # (lățimea, lărgirea, îngustarea pe spații, suma spațiilor, lărgirile infinite).
        self.bp = []
        self.by_bytes = {}       # (sfârșitul rândului, începutul următorului), în octeți -> punctul de rupere
        w, st, sh, sp, fil = indent * SP, 0, 0, 0, 0
        for idx, it in enumerate(items):
            kind = it[0]
            if kind == "box":
                w += it[1] * SP
            elif kind in ("glue", "nbsp"):
                end = (w, st, sh, sp, fil)
                w += it[1] * SP
                st += 2 * it[1] * SP
                sh += it[1] * SP // 3
                sp += it[1]
                if kind == "glue":
                    self.by_bytes[(it[2], it[3])] = len(self.bp)
                    self.bp.append((idx, end, (w, st, sh, sp, fil), 0, False, False))
            elif kind == "shy":
                self.by_bytes[(it[3], it[4])] = len(self.bp)
                self.bp.append((idx, (w + it[1] * SP, st, sh, sp, fil), (w + it[2] * SP, st, sh, sp, fil), HYPHEN, True, False))
                w += it[2] * SP
            elif kind == "dash":
                self.by_bytes[(it[2], it[2])] = len(self.bp)
                self.bp.append((idx, (w, st, sh, sp, fil), (w + it[1] * SP, st, sh, sp, fil), HYPHEN, True, False))
                w += it[1] * SP
            elif kind == "zwsp":
                self.by_bytes[(it[1], it[2])] = len(self.bp)
                self.bp.append((idx, (w, st, sh, sp, fil), (w, st, sh, sp, fil), 0, False, False))
            elif kind == "hard":
                fil += 1
                self.by_bytes[(it[1], it[2])] = len(self.bp)
                self.bp.append((idx, (w, st, sh, sp, fil), (w, st, sh, sp, fil), EJECT, False, False))
        fil += 1
        self.bp.append((-1, (w, st, sh, sp, fil), None, EJECT, True, True))

    def tex_input(self):
        out = ["i %d" % self.indent] if self.indent else []
        for it in self.items:
            k = it[0]
            out.append("b %d" % it[1] if k == "box" else "g %d" % it[1] if k == "glue" else "n %d" % it[1] if k == "nbsp"
                       else "s %d %d" % (it[1], it[2]) if k == "shy" else "d %d" % it[1] if k == "dash"
                       else "z" if k == "zwsp" else "h")
        return "P %s %d\n%s\n" % (self.pid, self.hsize, " ".join(out))

    def item_offset(self):
        return 1 if self.indent else 0


# -------------------------------------------------------- modelul algoritmului

def tex_badness(t, s):
    """badness(t, s) din tex.web §108: aproximarea lui 100 (t/s)³."""
    if t == 0:
        return 0
    if s <= 0:
        return INF_BAD
    if t <= 7230584:
        r = t * 297 // s
    elif s >= 1663497:
        r = t // (s // 297)
    else:
        r = t
    if r > 1290:
        return INF_BAD
    return (r * r * r + 0x20000) // 0x40000


def exact_badness(over, room):
    """badness() din layout.c."""
    if over <= 0:
        return 0
    if room <= 0:
        return BAD_MAX
    return min(100 * over * over * over // (room * room * room), BAD_MAX)


def line_cost(par, start, fit0, hyph0, first, j, opt, threshold):
    """Rândul de la capătul `start` până la punctul de rupere j.

    Întoarce (badness, clasa de potrivire, prea_lung, penalizarea rândului),
    cu penalizarea None dacă rândul nu e admis la toleranța `threshold`.
    `fit0` și `hyph0` sunt clasa și felul rândului dinainte, `first` spune că
    rândul e primul al paragrafului.
    """
    _, end, _, pen, hyph, final = par.bp[j]
    short = par.hsize * SP - (end[0] - start[0])
    space = end[3] - start[3]
    shrink = space // 3 * SP if opt["shrink_total"] else end[2] - start[2]
    too_long = False
    if pen == EJECT and opt["fill_natural"]:
        bad, fit = (0, 2) if short >= 0 else (BAD_MAX + 1, 2)
        too_long = short < 0
    elif short > 0:
        stretch = end[1] - start[1]
        if end[4] - start[4] > 0:
            bad = 0
        elif opt["exact_bad"]:
            bad = exact_badness(short, stretch)
        elif short > 7230584 and stretch < 1663497:
            bad = INF_BAD
        else:
            bad = tex_badness(short, stretch)
        fit = 0 if bad > 99 else 1 if bad > 12 else 2
    else:
        if -short > shrink:
            bad, too_long = INF_BAD + 1, True
        else:
            bad = exact_badness(-short, shrink) if opt["exact_bad"] else tex_badness(-short, shrink)
        fit = 3 if bad > 12 else 2
    if too_long or bad > threshold:
        return bad, fit, too_long, None
    d = LINE + bad
    d = d * d if opt["exact_bad"] or d < 10000 else 100000000
    if pen > 0:
        d += pen * pen
    elif EJECT < pen < 0:
        d -= pen * pen
    if hyph and hyph0:
        d += FINAL_HYPHEN if final else DOUBLE_HYPHEN
    if abs(fit - fit0) > 1 and not (first and opt["no_first_adj"]):
        d += ADJ
    return bad, fit, too_long, d


def solve(par, opt, threshold=TOLERANCE, final_pass=True):
    """Rupturile cu penalizarea totală cea mai mică, ca în try_break (tex.web §829–§860).

    Întoarce (punctele de rupere fără cel final, penalizarea totală, forțat) sau
    None dacă nu există nicio împărțire (doar cu final_pass=False). `forțat`
    spune că TeX a trebuit să accepte un rând peste toleranță ca să ajungă la
    capăt (rând „overfull” sau „underfull”). Penalizarea totală e cea pe care o
    raportează TeX: un rând luat cu artificial_demerits nu e numărat în ea.
    """
    bps = par.bp
    # O stare: (capătul de la care pornește rândul, clasa, cu cratimă, primul rând, totalul, de unde, punctul)
    active = [((0, 0, 0, 0, 0), 2, False, True, 0, None, -1)]
    forced = False
    one = opt["one_node"]
    for j, (_, _, nxt, pen, hyph, final) in enumerate(bps):
        best_d = [AWFUL] * 4
        best_r = [None] * 4
        minimum = AWFUL
        single = None
        keep = []
        last = len(active) - 1
        for n, r in enumerate(active):
            bad, fit, too_long, d = line_cost(par, r[0], r[1], r[2], r[3], j, opt, threshold)
            if too_long or pen == EJECT:
                if final_pass and minimum == AWFUL and n == last and not keep:
                    # artificial_demerits: rândul e luat oricum și nu costă nimic. TeX face
                    # asta și cu un ultim rând bun, dacă a rămas o singură stare.
                    forced = forced or d is None
                    d = r[4]
                elif d is None:
                    continue                               # dezactivat
                else:
                    d += r[4]
                stays = False
            else:
                if d is None:
                    keep.append(r)
                    continue
                d += r[4]
                stays = True
            if d <= best_d[fit]:
                best_d[fit], best_r[fit] = d, r
                if d < minimum:
                    minimum = d
            if single is None or d <= single[0]:
                single = (d, fit, r)
            if stays:
                keep.append(r)
        if minimum < AWFUL:
            if one:
                keep.append((nxt, single[1], hyph, False, single[0], single[2], j))
            else:
                limit = minimum + ADJ
                for fit in range(4):
                    if best_d[fit] <= limit:
                        keep.append((nxt, fit, hyph, False, best_d[fit], best_r[fit], j))
        active = keep
        if not active:
            return None
    best = active[0]
    for r in active[1:]:
        if r[4] < best[4]:
            best = r
    total, breaks = best[4], []
    r = best[5]                                            # starea de la care pornește ultimul rând
    while r[5] is not None:
        breaks.append(r[6])
        r = r[5]
    return breaks[::-1], total, forced


def path_cost(par, breaks, opt, threshold=TOLERANCE):
    """Penalizarea totală a unor rupturi date, sau None dacă un rând nu e admis."""
    start, fit0, hyph0, first, total = (0, 0, 0, 0, 0), 2, False, True, 0
    for j in list(breaks) + [len(par.bp) - 1]:
        _, fit, _, d = line_cost(par, start, fit0, hyph0, first, j, opt, threshold)
        if d is None:
            return None
        total += d
        start, fit0, hyph0, first = par.bp[j][2], fit, par.bp[j][4], False
    return total


def options(flags):
    return dict(NONE, **dict.fromkeys(flags, True))


def ours(par):
    """Dispozitivul: trecerea întâi la TOLERANCE, apoi cu orice rând care încape."""
    opt = options(par.flags)
    r = solve(par, opt, TOLERANCE, False)
    if r:
        return 0, r[0]
    r = solve(par, opt, BAD_MAX, False)
    return (1, r[0]) if r else (2, None)


def analyse(par):
    """Tot ce se calculează cu modelul pentru un paragraf (rulează în paralel)."""
    out = {"pid": par.pid, "ours": ours(par)}
    if not par.tex_ok:
        return out
    out["tex"] = {tol: solve(par, NONE, tol, True) for tol in (TOLERANCE, INF_BAD)}
    out["single"] = {}
    for flag in par.flags:
        r = solve(par, options([flag]), TOLERANCE, True)
        out["single"][flag] = r[0]
    # Dispozitivul cu câte o abatere scoasă: ce s-ar câștiga dacă ar fi îndreptată.
    out["without"] = {}
    for flag in par.flags:
        r = solve(par, options(f for f in par.flags if f != flag), TOLERANCE, False)
        out["without"][flag] = r[0] if r else None
    return out


# ---------------------------------------------------------------- intrările

def read_breaks(path):
    """Fișierul scris de `sim breaks`: pentru fiecare carte, antetul, rândurile pe înregistrări și trecerile."""
    books = []
    for line in open(path, encoding="utf-8"):
        f = line.split()
        if not f:
            continue
        if f[0] == "book":
            cfg = dict(zip(f[2::2], f[3::2]))
            books.append({"path": f[1], "font": cfg["font"], "width": int(cfg["width"]), "indent": int(cfg["indent"]),
                          "hyphenate": cfg["hyphenate"] == "1", "justify": cfg["justify"] == "1",
                          "fit": cfg["fit"] == "1", "version": int(cfg.get("version", 3)),
                          "lines": collections.defaultdict(list), "passes": None})
        elif f[0] == "passes":
            books[-1]["passes"] = [int(x) for x in f[1:]]
        else:
            para, start, end, flags, x, extra, gaps = (int(v) for v in f)
            books[-1]["lines"][para].append((start, end, flags, x, extra, gaps))
    return books


def prepare(book, fonts_dir, counts):
    """Paragrafele justificate ale unei cărți, măsurate, cu rupturile dispozitivului."""
    faces = load_faces(os.path.join(fonts_dir, book["font"]))
    parsed = ibv.parse(open(book["path"], "rb").read())
    name = os.path.basename(book["path"])
    pars = []
    ind = book["indent"] * FONT_UNIT
    for para, rec in enumerate(parsed.records()):
        if rec.kind not in (ibv.PARA, ibv.QUOTE) or rec.flags & ibv.ALIGN_MASK or not rec.text:
            continue
        counts["înregistrări justificate"] += 1
        lines = book["lines"].get(para)
        items, text_len = measure(rec, faces, book["hyphenate"])
        if not lines or lines[0][0] != 0:
            counts["fără rânduri în `sim breaks`"] += 1
            continue
        # Până la versiunea 3 dispozitivul nu admitea un rând gol între două U+2028 și
        # rupea greedy o astfel de înregistrare; de la 4 se compară, dar numai cu modelul.
        tex_ok = not unusual(items)
        if not tex_ok and book["version"] < 4:
            counts["cu spații sau ruperi forțate lipite între ele (sărite)"] += 1
            continue
        if not tex_ok:
            counts["cu spații sau ruperi forțate lipite între ele (comparate doar cu modelul)"] += 1
        hsize = book["width"] * FONT_UNIT
        first = hsize
        if rec.kind == ibv.QUOTE:
            hsize -= 2 * ind
            first = hsize
        elif not rec.flags & (ibv.NO_INDENT | ibv.CONT):
            first -= ind
        par = Par("%s:%d" % (name, para), items, hsize - first, hsize, text_len)
        if len(par.bp) + 1 > FIT_MAX or text_len > 65535:
            counts["prea lungi pentru încadrarea totală (greedy pe dispozitiv)"] += 1
            continue
        par.c_lines = lines
        par.c_breaks, par.c_cut = [], False
        for a, b in zip(lines, lines[1:]):
            j = par.by_bytes.get((a[1], b[0]))
            if j is None:
                par.c_cut = True                           # un cuvânt tăiat de ruperea greedy
            par.c_breaks.append(j)
        par.tex_ok = tex_ok
        par.flags = ENGINE[min(book["version"], max(ENGINE))]
        pars.append(par)
    return pars


def check_widths(par):
    """Lățimea fiecărui rând justificat, măsurată aici, față de cea a dispozitivului
    (lățimea rândului minus spațiul împărțit la justificare). Întoarce (verificate, greșite)."""
    checked = wrong = 0
    start = (0, 0, 0, 0, 0)
    for k, line in enumerate(par.c_lines):
        j = par.c_breaks[k] if k < len(par.c_breaks) else len(par.bp) - 1
        if j is None:
            return checked, wrong
        end = par.bp[j][1]
        if line[5] > 0 and (par.bp[j][3] != EJECT or line[4] < 0):
            checked += 1
            if par.hsize * SP - (end[0] - start[0]) != line[4] * SP or end[3] - start[3] <= 0:
                wrong += 1
        start = par.bp[j][2]
    return checked, wrong


def run_luatex(pars, work):
    inp, out = os.path.join(work, "paragrafe.txt"), os.path.join(work, "rupturi-tex.txt")
    with open(inp, "w") as f:
        for p in pars:
            if p.tex_ok:
                f.write(p.tex_input())
    env = dict(os.environ, TEXREF_IN=inp, TEXREF_OUT=out)
    cmd = ["luatex", "--ini", "--interaction=batchmode", "--output-directory=" + work,
           r"\catcode`\{=1 \catcode`\}=2 \directlua{dofile('%s')}\end" % os.path.join(HERE, "linebreak.lua")]
    r = subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=work)
    if not os.path.exists(out):
        sys.exit("luatex a eșuat (cod %d), vezi %s/texput.log" % (r.returncode, work))
    res = collections.defaultdict(dict)
    for line in open(out):
        f = line.split()
        res[f[1]][int(f[2])] = (int(f[3]), int(f[4]), [int(x) for x in f[5:]])
    version = subprocess.run(["luatex", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    return res, version


# ------------------------------------------------------------------ raportul

def pct(a, b):
    return "%d din %d (%.2f %%)" % (a, b, 100.0 * a / b) if b else "0 din 0"


def show(par, breaks):
    """Paragraful cu rupturile date, ca text: rândurile despărțite prin „ / ”."""
    text = par.text.encode("utf-8")
    cuts = {}
    for j in breaks:
        it = par.items[par.bp[j][0]]
        cuts[it[3] if it[0] == "shy" else it[2] if it[0] in ("glue", "dash") else it[1]] = it[0]
    out, at = [], 0
    for ch in par.text:
        if at in cuts:
            out.append("- / " if cuts[at] == "shy" else " / ")
            if cuts[at] in ("glue",):
                at += len(ch.encode("utf-8"))
                continue
        if ch != "­":
            out.append(ch)
        at += len(ch.encode("utf-8"))
    assert at == len(text)
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description="Compară ruperea în rânduri a dispozitivului cu cea din LuaTeX.")
    ap.add_argument("breaks", help="fișierul scris de `sim breaks`")
    ap.add_argument("--fonts", default=os.path.join(HERE, "..", "fonttool", "out", "fam"))
    ap.add_argument("--work", help="dosarul de lucru (implicit unul temporar)")
    ap.add_argument("--examples", type=int, default=0, help="câte paragrafe diferite să arate")
    ap.add_argument("--json", help="scrie numerele raportului și aici")
    ap.add_argument("--jobs", type=int, default=max(1, min(6, (os.cpu_count() or 2) - 2)))
    a = ap.parse_args()

    counts = collections.Counter()
    books = read_breaks(a.breaks)
    pars = []
    for book in books:
        if not (book["justify"] and book["fit"]):
            sys.exit("`sim breaks` trebuie rulat cu justificare și încadrare totală")
        if book["version"] != books[0]["version"]:
            sys.exit("cărțile din fișier sunt paginate cu versiuni diferite")
        pars.extend(prepare(book, a.fonts, counts))
    flags = ENGINE[min(books[0]["version"], max(ENGINE))]
    passes = [sum(b["passes"][i] for b in books) for i in range(3)]
    texed = [p for p in pars if p.tex_ok]

    work = a.work or tempfile.mkdtemp(prefix="texref-")
    os.makedirs(work, exist_ok=True)
    tex, version = run_luatex(pars, work)
    with multiprocessing.Pool(a.jobs) as pool:
        model = {r["pid"]: r for r in pool.imap_unordered(analyse, pars, chunksize=32)}

    rep = {"layout_version": books[0]["version"], "flags": list(flags)}
    print("# TeX ca arbitru: %d cărți, LAYOUT_VERSION %d, %s" % (len(books), books[0]["version"], version))
    fonts = collections.Counter("%s, %d px" % (b["font"], b["width"]) for b in books)
    print("# fonturi și lățimea textului: " + "; ".join("%s (%d cărți)" % kv for kv in sorted(fonts.items())))
    print("# abaterile dispozitivului de la TeX, cunoscute modelului:")
    for flag in flags:
        print("#   %-13s %s" % (flag, FLAGS[flag]))
    print()
    print("Înregistrări PARA și QUOTE justificate: %d" % counts["înregistrări justificate"])
    for k, v in sorted(counts.items()):
        if k != "înregistrări justificate":
            print("  %s: %d" % (k, v))
    print("  comparate cu LuaTeX: %d" % len(texed))
    print("Trecerile dispozitivului (din `sim breaks`): %d la toleranța %d, %d cu orice rând care încape, %d greedy"
          % (passes[0], TOLERANCE, passes[1], passes[2]))

    # 1. Măsurarea de aici față de cea a dispozitivului.
    checked = wrong = 0
    for p in pars:
        c, w = check_widths(p)
        checked, wrong = checked + c, wrong + w
    print()
    print("## 1. Aceeași problemă pe ambele părți")
    print("Lățimile rândurilor justificate, măsurate aici din fișierele de font, față de ale dispozitivului:")
    print("  %s identice" % pct(checked - wrong, checked))
    rep["widths"] = [checked - wrong, checked]

    # 2. Modelul față de cele două părți.
    by_pass = [0, 0, 0]
    ours_ok = ours_n = 0
    tex_ok = collections.Counter()
    tex_bad = []
    for p in pars:
        m = model[p.pid]
        p.pass_, p.model_breaks = m["ours"]
        by_pass[p.pass_] += 1
        if p.pass_ < 2:
            ours_n += 1
            ours_ok += p.model_breaks == p.c_breaks
        if not p.tex_ok:
            continue
        for tol in (TOLERANCE, INF_BAD):
            breaks, total, _ = m["tex"][tol]
            demerits, _, lua = tex[p.pid][tol]
            lua = [x - p.item_offset() for x in lua]
            mine = [p.bp[j][0] for j in breaks]
            if mine == lua and total == demerits:
                tex_ok[tol] += 1
            else:
                tex_bad.append((p.pid, tol, total, demerits))
    print()
    print("## 2. Modelul din texref.py")
    print("Cu abaterile dispozitivului, față de rândurile lui: %s identice" % pct(ours_ok, ours_n))
    same_passes = len(pars) != sum(passes) or by_pass == passes
    if len(pars) == sum(passes):
        print("  trecerile după model: %d, %d, %d greedy (%s)"
              % (by_pass[0], by_pass[1], by_pass[2], "ca ale dispozitivului" if same_passes else "ALTELE decât ale dispozitivului"))
    for tol in (TOLERANCE, INF_BAD):
        print("Fără abateri, față de LuaTeX la toleranța %d (rupturi și penalizarea totală): %s identice"
              % (tol, pct(tex_ok[tol], len(texed))))
    for pid, tol, total, demerits in tex_bad[:5]:
        print("  diferă: %s la toleranța %d (model %d, LuaTeX %d)" % (pid, tol, total, demerits))
    rep["model_ours"] = [ours_ok, ours_n]
    rep["model_tex"] = {str(t): [tex_ok[t], len(texed)] for t in (TOLERANCE, INF_BAD)}
    rep["by_pass"] = by_pass

    # 3. Dispozitivul față de LuaTeX.
    def lua_breaks(p, tol):
        """Rupturile din LuaTeX ca puncte de rupere ale paragrafului."""
        where = {b[0]: j for j, b in enumerate(p.bp)}
        return [where[x - p.item_offset()] for x in tex[p.pid][tol][2]]

    none = NONE
    print()
    print("## 3. Dispozitivul față de LuaTeX")
    groups = {0: [p for p in texed if p.pass_ == 0], 1: [p for p in texed if p.pass_ == 1]}
    diff0 = []
    overall = [0, 0, 0, 0]
    for pas, tol, title in ((0, TOLERANCE, "Paragrafele rupte de dispozitiv din prima trecere, față de LuaTeX la toleranța 400"),
                            (1, TOLERANCE, "Paragrafele din a doua trecere, față de LuaTeX la toleranța 400"),
                            (1, INF_BAD, "Paragrafele din a doua trecere, față de LuaTeX la toleranța 10000")):
        same = lines = same_lines = forced = same_count = 0
        for p in groups[pas]:
            lb = lua_breaks(p, tol)
            forced += model[p.pid]["tex"][tol][2]
            lines += len(p.c_breaks) + 1
            if lb == p.c_breaks:
                same += 1
                same_lines += len(lb) + 1
            else:
                if pas == 0:
                    diff0.append(p)
                same_count += len(lb) == len(p.c_breaks)
                # Un rând e același dacă începe și se termină la aceleași ruperi.
                ca, la = [-1] + p.c_breaks + [-2], [-1] + lb + [-2]
                same_lines += len(set(zip(ca, ca[1:])) & set(zip(la, la[1:])))
        n = len(groups[pas])
        print(title + ":")
        print("  paragrafe cu aceleași rupturi: %s" % pct(same, n))
        print("  rânduri identice: %s" % pct(same_lines, lines))
        print("  dintre cele diferite, cu același număr de rânduri: %s" % pct(same_count, n - same))
        print("  paragrafe în care TeX a acceptat un rând peste toleranță (overfull/underfull): %s" % pct(forced, n))
        rep["pass%d_tol%d" % (pas, tol)] = {"same": same, "n": n, "same_lines": same_lines, "lines": lines,
                                            "same_count": same_count, "forced": forced}
        if (pas, tol) != (1, TOLERANCE):
            overall = [x + y for x, y in zip(overall, (same, n, same_lines, lines))]
    print("Toate la un loc (prima trecere față de toleranța 400, a doua față de 10000):")
    print("  paragrafe cu aceleași rupturi: %s" % pct(overall[0], overall[1]))
    print("  rânduri identice: %s" % pct(overall[2], overall[3]))
    rep["overall"] = overall

    # 4. Cât costă diferențele, în măsura lui TeX.
    ratios, not_admitted, equal_cost, tex_forced0 = [], 0, 0, 0
    for p in diff0:
        # Ambele împărțiri costate rând cu rând (totalul raportat de TeX poate sări ultimul rând).
        best = path_cost(p, lua_breaks(p, TOLERANCE), none)
        cost = path_cost(p, p.c_breaks, none)
        if best is None:
            tex_forced0 += 1
        elif cost is None:
            not_admitted += 1
        elif cost == best:
            equal_cost += 1
        else:
            ratios.append(cost / best)
    print()
    print("## 4. Paragrafele diferite din prima trecere, judecate cu penalizarea din TeX")
    print("  %d paragrafe; rupturile dispozitivului au aceeași penalizare totală ca ale lui TeX: %d" % (len(diff0), equal_cost))
    print("  au un rând pe care TeX nu-l admite la toleranța 400: %d" % not_admitted)
    print("  TeX însuși a luat un rând peste toleranță: %d" % tex_forced0)
    if ratios:
        ratios.sort()
        print("  restul de %d: penalizarea dispozitivului e mai mare decât optimul lui TeX cu %.1f %% la mediană, "
              "%.1f %% la percentila 90, %.1f %% cel mult"
              % (len(ratios), 100 * (statistics.median(ratios) - 1), 100 * (ratios[len(ratios) * 9 // 10] - 1),
                 100 * (ratios[-1] - 1)))
    rep["cost"] = {"n": len(diff0), "equal": equal_cost, "not_admitted": not_admitted, "tex_forced": tex_forced0,
                   "median": statistics.median(ratios) if ratios else None, "max": ratios[-1] if ratios else None}

    # 5. De unde vin diferențele.
    print()
    print("## 5. De unde vin diferențele (prima trecere, %d paragrafe)" % len(groups[0]))
    print("Fiecare abatere pornită singură peste modelul TeX: câte paragrafe schimbă.")
    base = {p.pid: model[p.pid]["tex"][TOLERANCE][0] for p in groups[0]}
    rep["single"], rep["without"] = {}, {}
    for flag in flags:
        n = sum(model[p.pid]["single"][flag] != base[p.pid] for p in groups[0])
        print("  %-13s %5d  %s" % (flag, n, FLAGS[flag]))
        rep["single"][flag] = n
    n_all = sum(p.model_breaks != base[p.pid] for p in groups[0])
    print("  %-13s %5d  toate (dispozitivul)" % ("toate", n_all))
    print("Dispozitivul cu câte o abatere scoasă: câte paragrafe mai diferă de TeX.")
    for flag in flags:
        n = sum(model[p.pid]["without"][flag] != base[p.pid] for p in groups[0])
        print("  fără %-13s %5d" % (flag, n))
        rep["without"][flag] = n
    # Paragrafele pe care prima trecere a dispozitivului nu le poate așeza, dar TeX da.
    rescued = [p for p in groups[1] if not model[p.pid]["tex"][TOLERANCE][2]]
    print("Paragrafe din a doua trecere pe care TeX le așază la toleranța 400 fără niciun rând peste ea: %s"
          % pct(len(rescued), len(groups[1])))
    for flag in flags:
        n = sum(model[p.pid]["without"][flag] is not None for p in rescued)
        print("  dintre ele, așezate de dispozitiv din prima trecere fără %-13s %5d" % (flag, n))
    rep["rescued"] = len(rescued)

    if a.examples:
        print()
        print("## Exemple (dispozitivul, apoi LuaTeX)")
        recs = {}
        for p in diff0[:a.examples]:
            name, para = p.pid.rsplit(":", 1)
            if name not in recs:
                path = next(b["path"] for b in books if os.path.basename(b["path"]) == name)
                recs[name] = list(ibv.parse(open(path, "rb").read()).records())
            p.text = recs[name][int(para)].text
            print("%s  (penalizarea în măsura lui TeX: %s față de %s)"
                  % (p.pid, path_cost(p, p.c_breaks, none), path_cost(p, lua_breaks(p, TOLERANCE), none)))
            print("  D: " + show(p, p.c_breaks))
            print("  T: " + show(p, lua_breaks(p, TOLERANCE)))
    if a.json:
        rep["counts"], rep["passes"], rep["luatex"] = dict(counts), passes, version
        with open(a.json, "w") as f:
            json.dump(rep, f, indent=1, ensure_ascii=False)
    ok = wrong == 0 and ours_ok == ours_n and same_passes and all(tex_ok[t] == len(texed) for t in (TOLERANCE, INF_BAD))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
