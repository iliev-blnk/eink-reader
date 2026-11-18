#!/usr/bin/env python3
"""E6: tabelele și figurile din date/ (scrise de run.sh).

    python3 measurements/E6/analiza.py

Scrie tabele.md (generat, nu se editează de mână) și figuri/*.svg.
"""
import csv
import os
import statistics
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import svgplot  # noqa: E402
from linii import PROZA  # noqa: E402

DATE = os.path.join(HERE, "date")
FIG = os.path.join(HERE, "figuri")
STEPS = ["0", "1", "2", "3", "4"]
CONDS = [("greedy", "nu"), ("kp", "nu"), ("greedy", "da"), ("kp", "da")]
NAMES = {("greedy", "nu"): "greedy, fără despărțire", ("kp", "nu"): "Knuth–Plass, fără despărțire",
         ("greedy", "da"): "greedy, cu despărțire", ("kp", "da"): "Knuth–Plass, cu despărțire"}


def load(name):
    with open(os.path.join(DATE, name), newline="") as f:
        return list(csv.DictReader(f))


def n(v, nd=2):
    if nd:
        return svgplot.num(float(v), nd)
    return f"{float(v):,.0f}".replace(",", ".").replace("-", "−")


def change(a, b):
    """Schimbarea relativă de la a la b, în %."""
    return 100 * (b - a) / a


def main():
    lines = load("linii.csv")
    L = {(r["alg"], r["desp"], r["treapta"], r["carte"]): r for r in lines}
    hist = load("histograma.csv")
    timp = load("timp.csv")
    os.makedirs(FIG, exist_ok=True)
    out = []
    w = out.append

    # Paginile (din prima rulare) și timpul (mediana celor 5 rulări de la treapta 2).
    pages = defaultdict(int)
    hashes = defaultdict(set)
    ms = defaultdict(list)
    for r in timp:
        key = (r["alg"], r["desp"], r["treapta"])
        hashes[key + (r["carte"],)].add(r["hash"])
        if r["rulare"] == "1" and r["carte"] in PROZA:
            pages[key] += int(r["pagini"])
        if r["treapta"] == "2":
            ms[(r["alg"], r["desp"], r["carte"], r["rulare"])].append(float(r["ms"]))
    unstable = [k for k, v in hashes.items() if len(v) != 1]

    w("# E6: tabele (generate de analiza.py)\n")
    w(f"Nu se editează de mână. Proza = rândurile celor {len(PROZA)} cărți în proză, puse împreună. "
      "r = cât de lat e spațiul justificat față de unul simplu (0 = la fel, 1 = dublu).\n")
    w(f"Hash-urile paginării la cele 5 rulări repetate: {'identice' if not unstable else 'DIFERITE la ' + str(unstable)}.\n")

    # 1. Proza la treapta implicită.
    w("## 1. Proza, treapta implicită (2)\n")
    w("| condiția | rânduri justificate | r medie | r mediană | r p95 | r > 0,5 | r > 1 | strânse (r < 0) | cratimă | 2 cratime / 1000 rânduri | pagini |")
    w("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for c in CONDS:
        r = L[c + ("2", "PROZA")]
        w(f"| {NAMES[c]} | {n(r['justificate'], 0)} | {n(r['r_medie'])} | {n(r['r_mediana'])} | {n(r['r_p95'])} | "
          f"{n(r['peste_05_pct'], 1)} % | {n(r['peste_1_pct'], 1)} % | {n(r['negative_pct'], 1)} % | "
          f"{n(r['cratime_pct'], 1)} % | {n(r['cratime2_la1000'], 1)} | {n(pages[c + ('2',)], 0)} |")
    w("")

    # 2. Pe carte, cu despărțire, treapta 2.
    w("## 2. Pe carte, cu despărțire, treapta 2\n")
    w("| carte | font | greedy r medie | K–P r medie | schimbare | greedy r p95 | K–P r p95 | schimbare | greedy 2cr/1000 | K–P 2cr/1000 |")
    w("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    books = sorted({r["carte"] for r in lines if r["carte"] != "PROZA"})
    order = [b for b in books if b in PROZA] + [b for b in books if b not in PROZA] + ["PROZA"]
    for b in order:
        g, k = L[("greedy", "da", "2", b)], L[("kp", "da", "2", b)]
        mark = "" if b in PROZA or b == "PROZA" else " (în afara prozei)"
        w(f"| {'**proza**' if b == 'PROZA' else b + mark} | {k['font']} | {n(g['r_medie'])} | {n(k['r_medie'])} | "
          f"{n(change(float(g['r_medie']), float(k['r_medie'])), 0)} % | {n(g['r_p95'])} | {n(k['r_p95'])} | "
          f"{n(change(float(g['r_p95']), float(k['r_p95'])), 0)} % | {n(g['cratime2_la1000'], 1)} | {n(k['cratime2_la1000'], 1)} |")
    w("")

    # 3. Proza pe trepte.
    w("## 3. Proza pe trepte de mărime: r medie (r p95)\n")
    w("| condiția | " + " | ".join(f"treapta {s}" for s in STEPS) + " |")
    w("|---|" + "---:|" * len(STEPS))
    for c in CONDS:
        w(f"| {NAMES[c]} | " + " | ".join(f"{n(L[c + (s, 'PROZA')]['r_medie'])} ({n(L[c + (s, 'PROZA')]['r_p95'])})"
                                         for s in STEPS) + " |")
    w("")

    # 4. Timpul pe PC.
    w("## 4. Timpul de paginare pe PC, treapta 2\n")
    w("Mediana celor 5 rulări; o rulare paginează toată cartea și construiește indexul paginilor.\n")
    w("| carte | " + " | ".join(f"{NAMES[c]} (ms)" for c in CONDS) + " | K–P / greedy, cu despărțire |")
    w("|---|" + "---:|" * (len(CONDS) + 1))
    med = {}
    for b in books:
        for c in CONDS:
            med[c + (b,)] = statistics.median(ms[(c[0], c[1], b, str(i))][0] for i in range(1, 6))
    for b in order[:-1]:
        w(f"| {b} | " + " | ".join(n(med[c + (b,)], 1) for c in CONDS)
          + f" | {n(med[('kp', 'da', b)] / med[('greedy', 'da', b)])} |")
    tot = {c: sum(med[c + (b,)] for b in PROZA) for c in CONDS}
    ratio = tot[("kp", "da")] / tot[("greedy", "da")]
    w(f"| **proza, total** | " + " | ".join(n(tot[c], 1) for c in CONDS) + f" | **{n(ratio)}** |")
    w("")

    # 5. Predicțiile din METODA.md.
    P = lambda c, s="2": L[c + (s, "PROZA")]
    g, k = P(("greedy", "da")), P(("kp", "da"))
    d_mean = change(float(g["r_medie"]), float(k["r_medie"]))
    d_p95 = change(float(g["r_p95"]), float(k["r_p95"]))
    gn, kn = P(("greedy", "nu")), P(("kp", "nu"))
    f_g = float(gn["r_medie"]) / float(g["r_medie"])
    f_k = float(kn["r_medie"]) / float(k["r_medie"])
    worst_step = all(max(STEPS, key=lambda s: float(P(c, s)["r_medie"])) == "4" for c in CONDS)
    verdict = lambda ok: "**trece**" if ok else "**nu trece**"
    w("## 5. Predicțiile\n")
    w("| | predicția | măsurat | |")
    w("|---|---|---|---|")
    w(f"| 1 | K–P scade r medie ≥ 15 % și r p95 ≥ 20 % (proză, despărțire, treapta 2) | "
      f"r medie {n(d_mean, 0)} %, r p95 {n(d_p95, 0)} % | {verdict(d_mean <= -15 and d_p95 <= -20)} |")
    w(f"| 2 | fără despărțire r medie ≥ 1,5× mai mare, la amândoi algoritmii; K–P nu compensează | "
      f"greedy {n(f_g)}×, K–P {n(f_k)}×; K–P fără despărțire {n(kn['r_medie'])} față de greedy cu despărțire {n(g['r_medie'])} | "
      f"{verdict(f_g >= 1.5 and f_k >= 1.5 and float(kn['r_medie']) > float(g['r_medie']))} |")
    w(f"| 3 | K–P are mai puține cratime consecutive | greedy {n(g['cratime2_la1000'], 1)}, K–P {n(k['cratime2_la1000'], 1)} "
      f"la 1000 de rânduri | {verdict(float(k['cratime2_la1000']) < float(g['cratime2_la1000']))} |")
    w(f"| 4 | spațierea e cea mai proastă la treapta 4, la toate variantele | "
      + ", ".join(f"{NAMES[c]}: maxim la treapta {max(STEPS, key=lambda s: float(P(c, s)['r_medie']))}" for c in CONDS)
      + f" | {verdict(worst_step)} |")
    w(f"| 5 | K–P ≤ 2× timpul greedy pe PC | {n(ratio)}× | {verdict(ratio <= 2)} |")
    w("")
    with open(os.path.join(HERE, "tabele.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out))

    # Figurile.
    H = defaultdict(list)
    for r in hist:
        H[(r["alg"], r["desp"], r["treapta"])].append((float(r["r_de_la"]), int(r["randuri"])))
    series = []
    for c in CONDS:
        h = H[c + ("2",)]
        total, acc, ys = sum(v for _, v in h), 0, []
        for _, v in h[:-1]:             # ultima clasă adună tot ce e peste 4; curba se oprește înainte
            acc += v
            ys.append(100 * acc / total)
        series.append((NAMES[c], [x + 0.05 for x, _ in h[:-1]], ys))
    svgplot.lines(os.path.join(FIG, "distributia-r.svg"), "Spațierea rândurilor justificate (proză, treapta 2)",
                  "r (0 = spațiu simplu, 1 = de două ori mai lat)", "% din rânduri cu spațierea ≤ r", series,
                  xticks=[-0.5, 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4], ylo=0, yhi=100,
                  marks=[("v", 0, ""), ("v", 1, "r = 1")])
    svgplot.lines(os.path.join(FIG, "r-pe-trepte.svg"), "r mediu pe proză, pe trepte de mărime",
                  "treapta de mărime (0 = cel mai mic font, 2 = implicit)", "r mediu",
                  [(NAMES[c], [int(s) for s in STEPS], [float(P(c, s)["r_medie"]) for s in STEPS]) for c in CONDS],
                  xticks=[0, 1, 2, 3, 4], ylo=0, xfmt=lambda v: str(int(v)))
    prose_books = [b for b in order if b in PROZA]
    svgplot.bars(os.path.join(FIG, "r-pe-carte.svg"), "r mediu pe carte, cu despărțire, treapta 2", "r mediu",
                 prose_books, ["greedy", "Knuth–Plass"],
                 [[float(L[(a, "da", "2", b)]["r_medie"]) for b in prose_books] for a in ("greedy", "kp")],
                 fmt=lambda v: svgplot.num(v))
    print("tabele.md și figuri/ scrise")


if __name__ == "__main__":
    main()
