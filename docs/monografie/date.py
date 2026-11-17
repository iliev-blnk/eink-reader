#!/usr/bin/env python3
"""Pregătește datele figurilor din monografie, direct din măsurători.

Citește measurements/E5/date și measurements/E6/date (nu le copiază: figura
urmează datele) și scrie tabele mici, separate prin spații, pentru pgfplots.

    python3 docs/monografie/date.py OUT_DIR
"""
import csv
import sys
from pathlib import Path

RAD = Path(__file__).resolve().parents[2]
E5 = RAD / "measurements" / "E5" / "date"
E6 = RAD / "measurements" / "E6" / "date"
sys.path.insert(0, str(RAD / "measurements" / "E6"))
from linii import PROZA  # noqa: E402

COND = [("greedy", "nu"), ("kp", "nu"), ("greedy", "da"), ("kp", "da")]


def rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def histograma(out):
    """Distribuția lui r pe proză, treapta 2, cu despărțire: % din rânduri pe interval."""
    h = {}
    for r in rows(E6 / "histograma.csv"):
        if r["desp"] == "da" and r["treapta"] == "2":
            h.setdefault(r["alg"], {})[float(r["r_de_la"])] = int(r["randuri"])
    tot = {a: sum(v.values()) for a, v in h.items()}
    with open(out / "histograma.dat", "w") as f:
        f.write("r greedy kp\n")
        for x in sorted(h["kp"]):
            f.write(f"{x:.2f} {100 * h['greedy'][x] / tot['greedy']:.4f} {100 * h['kp'][x] / tot['kp']:.4f}\n")


def trepte(out):
    """r mediu și r p95 pe proză, pe trepte, pentru cele patru condiții."""
    L = {(r["alg"], r["desp"], r["treapta"]): r for r in rows(E6 / "linii.csv") if r["carte"] == "PROZA"}
    with open(out / "trepte.dat", "w") as f:
        f.write("treapta " + " ".join(f"{a}_{d}_m {a}_{d}_p95" for a, d in COND) + "\n")
        for t in range(5):
            f.write(str(t))
            for a, d in COND:
                r = L[(a, d, str(t))]
                f.write(f" {float(r['r_medie']):.4f} {float(r['r_p95']):.4f}")
            f.write("\n")


def blocuri(out):
    """Mărimea totală a celor 8 cărți în proză, pe mărimea țintă a blocului, față de 16 KB."""
    tot, med = {}, {}
    for r in rows(E5 / "blocuri.csv"):
        if r["carte"] in PROZA:
            b = int(r["bloc_tinta"])
            tot[b] = tot.get(b, 0) + int(r["fisier"])
            med.setdefault(b, []).append(int(r["bloc_stocat_mediana"]))
    with open(out / "blocuri.dat", "w") as f:
        f.write("kb relativ citit_kb\n")
        for b in sorted(tot):
            m = sorted(med[b])
            mediana = (m[len(m) // 2 - 1] + m[len(m) // 2]) / 2 if len(m) % 2 == 0 else m[len(m) // 2]
            f.write(f"{b // 1024} {100 * (tot[b] / tot[16384] - 1):.3f} {mediana / 1024:.3f}\n")


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    histograma(out)
    trepte(out)
    blocuri(out)


if __name__ == "__main__":
    main()
