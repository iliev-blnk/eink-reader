#!/usr/bin/env python3
"""E6: rezumă rândurile scrise de `sim breaks` (de pe intrarea standard).

Adaugă câte un rând pe carte, plus rândul PROZA (toate rândurile cărților în
proză puse împreună), la `date/linii.csv`. Adaugă histograma lui r pe proză la
`date/histograma.csv`. Condiția (algoritmul, despărțirea, treapta) vine din
argumente, ca run.sh să le poată scrie pe toate în aceleași fișiere.

    sim breaks CĂRȚI [opțiuni] | linii.py ALG DESP TREAPTA

Rândurile numărate (ca în `sim stats`): fără titluri și ornamente. r se
calculează doar pe rândurile justificate (cele cu cel puțin un interval
lărgit); ultimul rând al paragrafului are gaps = 0 și nu intră.
"""
import csv
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATE = os.path.join(HERE, "date")

# Subsetul principal, stabilit în METODA.md înainte de rulare.
PROZA = {"libru-dubrovsky", "pg11", "pg1342", "pg2701", "pg62916", "pg64597", "pg65565", "pg84",
         # v2 (2026-11-18), tot dinainte de rulare: proza adăugată în MEAS-1
         "libru-geroi", "pg4300", "ws-amintiri", "ws-ion", "ws-kapitanskaya", "ws-moara-cu-noroc"}

LINE_HYPHEN, LINE_ORNAMENT, LINE_HEADING = 1, 2, 4
HIST_LO, HIST_HI, HIST_STEP = -0.5, 4.0, 0.05

COLS = ["alg", "desp", "treapta", "carte", "font", "spatiu_64px", "randuri", "justificate",
        "r_medie", "r_mediana", "r_p95", "r_p99", "r_min", "r_max", "peste_05_pct", "peste_1_pct", "negative_pct",
        "cratime_pct", "cratime2_la1000", "kp_treapta1", "kp_treapta2", "kp_treapta3"]


def pct(xs, p):
    """Percentila prin rangul cel mai apropiat, pe o listă sortată."""
    if not xs:
        return float("nan")
    k = max(0, math.ceil(p / 100 * len(xs)) - 1)
    return xs[k]


class Book:
    def __init__(self, name, head):
        self.name, self.head = name, head
        self.space = int(head["space"])
        self.lines = self.hyph = self.hyph2 = 0
        self.last_hyph = False
        self.r = []
        self.passes = ["", "", ""]

    def add(self, flags, extra, gaps):
        if flags & (LINE_HEADING | LINE_ORNAMENT):
            self.last_hyph = False
            return
        self.lines += 1
        hy = bool(flags & LINE_HYPHEN)
        self.hyph += hy
        self.hyph2 += hy and self.last_hyph
        self.last_hyph = hy
        if gaps:
            self.r.append(extra / gaps / self.space)


def row(cond, name, font, space, lines, hyph, hyph2, r, passes):
    r = sorted(r)
    n = len(r)
    f = lambda v: f"{v:.4f}"
    return dict(zip(COLS, [*cond, name, font, space, lines, n,
                           f(sum(r) / n), f(pct(r, 50)), f(pct(r, 95)), f(pct(r, 99)), f(r[0]), f(r[-1]),
                           f(100 * sum(x > 0.5 for x in r) / n), f(100 * sum(x > 1 for x in r) / n),
                           f(100 * sum(x < 0 for x in r) / n),
                           f(100 * hyph / lines), f(1000 * hyph2 / lines), *passes]))


def main():
    cond = sys.argv[1:4]
    books, cur = [], None
    for line in sys.stdin:
        if line.startswith("book "):
            w = line.split()
            head = dict(zip(w[2::2], w[3::2]))
            cur = Book(os.path.basename(w[1]).removesuffix(".ibv"), head)
            books.append(cur)
        elif line.startswith("passes "):
            cur.passes = line.split()[1:4]
        else:
            _para, _s, _e, flags, _x, extra, gaps = map(int, line.split())
            cur.add(flags, extra, gaps)

    rows = [row(cond, b.name, b.head["font"], b.space, b.lines, b.hyph, b.hyph2, b.r, b.passes) for b in books]
    prose = [b for b in books if b.name in PROZA]
    if len(prose) != len(PROZA):
        sys.exit(f"lipsesc cărți din proză: {sorted(PROZA - {b.name for b in prose})}")
    pooled = [x for b in prose for x in b.r]
    rows.append(row(cond, "PROZA", "", "", sum(b.lines for b in prose), sum(b.hyph for b in prose),
                    sum(b.hyph2 for b in prose), pooled,
                    [str(sum(int(b.passes[i] or 0) for b in prose)) if prose[0].passes[i] else "" for i in range(3)]))

    os.makedirs(DATE, exist_ok=True)
    path = os.path.join(DATE, "linii.csv")
    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        w = csv.DictWriter(f, COLS)
        if new:
            w.writeheader()
        w.writerows(rows)

    nb = round((HIST_HI - HIST_LO) / HIST_STEP)
    counts = [0] * nb
    for x in pooled:      # valorile din afara intervalului intră în prima sau ultima clasă
        counts[min(nb - 1, max(0, int((x - HIST_LO) / HIST_STEP)))] += 1
    path = os.path.join(DATE, "histograma.csv")
    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["alg", "desp", "treapta", "r_de_la", "randuri"])
        for i, c in enumerate(counts):
            w.writerow([*cond, f"{HIST_LO + i * HIST_STEP:.2f}", c])


if __name__ == "__main__":
    main()
