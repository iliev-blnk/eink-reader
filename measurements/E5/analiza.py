#!/usr/bin/env python3
"""E5: tabelele și figurile din date/ (scrise de run.py).

    python3 measurements/E5/analiza.py

Scrie tabele.md (generat, nu se editează de mână) și figuri/*.svg.
"""
import csv
import os
import statistics
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
sys.path.insert(0, os.path.join(HERE, "..", "E6"))
import svgplot  # noqa: E402
from linii import PROZA  # noqa: E402

DATE = os.path.join(HERE, "date")
FIG = os.path.join(HERE, "figuri")


def load(name):
    with open(os.path.join(DATE, name), newline="") as f:
        return [{k: (v if k in ("carte", "varianta") else float(v)) for k, v in r.items()} for r in csv.DictReader(f)]


def kb(v):
    return svgplot.num(v / 1024, 0 if v >= 100 * 1024 else 1)


def pc(v, nd=0):
    return svgplot.num(100 * v, nd) + " %"


def sp(v):
    """Schimbare relativă cu semn: +12 %, −3 %."""
    return ("+" if v >= 0 else "") + pc(v, 1)


def main():
    rows = load("carti.csv")
    V = {(r["carte"], r["varianta"]): r for r in rows}
    books = sorted({r["carte"] for r in rows}, key=lambda b: (b not in PROZA, b))
    blocks = load("blocuri.csv")
    B = {(r["carte"], int(r["bloc_tinta"])): r for r in blocks}
    sizes = sorted({int(r["bloc_tinta"]) for r in blocks})
    tm = defaultdict(list)
    for r in load("timp.csv"):
        tm[r["carte"]].append(r["s"])
    os.makedirs(FIG, exist_ok=True)
    out = []
    w = out.append
    tag = lambda b: b if b in PROZA else b + " *"

    w("# E5: tabele (generate de analiza.py)\n")
    w("Nu se editează de mână. KB = 1024 B. * = în afara prozei (versuri, culegerea de probleme). "
      "Textul simplu = octeții UTF-8 ai paragrafelor, fără cratimele moi.\n")

    w("## 1. Mărimile, varianta implicită (DEFLATE, blocuri de 16 KB, despărțire, copertă)\n")
    w("| carte | sursa | text simplu | TEXT brut | TEXT comprimat | comprimat / brut | comprimat / simplu | copertă | fișierul | deschiderea (B) | conversia (s) |")
    w("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for b in books:
        r = V[(b, "implicit")]
        w(f"| {tag(b)} | {kb(r['sursa'])} | {kb(r['text_simplu'])} | {kb(r['text_brut'])} | {kb(r['text'])} | "
          f"{pc(r['text'] / r['text_brut'])} | {pc(r['text'] / r['text_simplu'])} | {kb(r['covr'])} | {kb(r['fisier'])} | "
          f"{r['deschidere']:.0f} | {svgplot.num(statistics.median(tm[b]))} |")
    w("")

    w("## 2. Ce costă fiecare alegere: mărimea fișierului față de varianta implicită\n")
    w("| carte | stocat (fără compresie) | fără copertă | fără despărțire | cratimele moi, din fișier |")
    w("|---|---:|---:|---:|---:|")
    hy_cost = {}
    for b in books:
        i = V[(b, "implicit")]["fisier"]
        nh = V[(b, "fara_despartire")]["fisier"]
        hy_cost[b] = (i - nh) / i
        w(f"| {tag(b)} | {sp(V[(b, 'stocat')]['fisier'] / i - 1)} | {sp(V[(b, 'fara_coperta')]['fisier'] / i - 1)} | "
          f"{sp(nh / i - 1)} | {pc(hy_cost[b], 1)} |")
    w("")

    w("## 3. Mărimea blocului\n")
    w("Proza, totalul celor 8 cărți. „Citit la un salt” = octeții stocați ai unui bloc (cât se citește de pe card ca "
      "să ajungi la o poziție); „RAM” = blocul decomprimat, cel mai mare din toate cărțile.\n")
    w("| bloc țintă | fișierele (KB) | față de 16 KB | blocuri | citit la un salt, mediana (B) | citit, maximul (B) | RAM, maximul (B) |")
    w("|---:|---:|---:|---:|---:|---:|---:|")
    tot = {s: sum(B[(b, s)]["fisier"] for b in PROZA) for s in sizes}
    for s in sizes:
        rs = [B[(b, s)] for b in PROZA]
        w(f"| {s // 1024} KB | {kb(tot[s])} | {sp(tot[s] / tot[16384] - 1)} | {sum(r['blocuri'] for r in rs):.0f} | "
          f"{statistics.median(r['bloc_stocat_mediana'] for r in rs):.0f} | {max(r['bloc_stocat_max'] for r in rs):.0f} | "
          f"{max(B[(b, s)]['bloc_brut_max'] for b in books):.0f} |")
    w("")

    # Predicțiile.
    ratio_ok = all(V[(b, "implicit")]["text"] / V[(b, "implicit")]["text_brut"] <= 0.5 for b in PROZA)
    worst_ratio = max(V[(b, "implicit")]["text"] / V[(b, "implicit")]["text_brut"] for b in PROZA)
    fit = [b for b in books if V[(b, "implicit")]["deschidere"] <= 4096]
    nofit = [b for b in books if b not in fit]
    gain32 = tot[32768] / tot[16384] - 1
    cost4 = tot[4096] / tot[16384] - 1
    slowest = max(books, key=lambda b: statistics.median(tm[b]))
    t_max = statistics.median(tm[slowest])
    verdict = lambda ok: "**trece**" if ok else "**nu trece**"
    w("## 4. Predicțiile\n")
    w("| | predicția | măsurat | |")
    w("|---|---|---|---|")
    w(f"| 1 | TEXT comprimat ≤ 50 % din brut, la fiecare carte în proză | cel mai mare raport {pc(worst_ratio)} | {verdict(ratio_ok)} |")
    w(f"| 2 | cratimele moi adaugă ≤ 10 % la fișier | între {pc(min(hy_cost.values()), 1)} și {pc(max(hy_cost.values()), 1)} | "
      f"{verdict(max(hy_cost.values()) <= 0.10)} |")
    w(f"| 3 | deschiderea încape în 4 KB la ≥ 9 din 12 cărți | {len(fit)} din {len(books)}; nu încap: {', '.join(nofit) or 'niciuna'} | "
      f"{verdict(len(fit) >= 9)} |")
    w(f"| 4 | 32 KB câștigă < 5 %, 4 KB costă > 15 % (proză) | 32 KB: {sp(gain32)}, 4 KB: {sp(cost4)} | "
      f"{verdict(-gain32 < 0.05 and cost4 > 0.15)} |")
    w(f"| 5 | conversia < 5 s pe carte | cea mai lentă {slowest}, {svgplot.num(t_max)} s | {verdict(t_max < 5)} |")
    w("")
    with open(os.path.join(HERE, "tabele.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out))

    svgplot.bars(os.path.join(FIG, "marimi.svg"), "Textul simplu și secțiunea TEXT comprimată, pe carte", "KB",
                 books, ["text simplu", "TEXT brut (cu cratime și marcaje)", "TEXT comprimat"],
                 [[V[(b, "implicit")][k] / 1024 for b in books] for k in ("text_simplu", "text_brut", "text")])
    svgplot.lines(os.path.join(FIG, "bloc.svg"), "Mărimea blocului: fișierul față de cât se citește la un salt",
                  "blocul țintă (KB)", "% față de 16 KB",
                  [("mărimea fișierelor (proză)", [s / 1024 for s in sizes], [100 * tot[s] / tot[16384] for s in sizes]),
                   ("citit la un salt, mediana", [s / 1024 for s in sizes],
                    [100 * statistics.median(B[(b, s)]["bloc_stocat_mediana"] for b in PROZA)
                     / statistics.median(B[(b, 16384)]["bloc_stocat_mediana"] for b in PROZA) for s in sizes])],
                  xticks=[4, 8, 16, 32], ylo=0, xfmt=lambda v: str(int(v)))
    print("tabele.md și figuri/ scrise")


if __name__ == "__main__":
    main()
