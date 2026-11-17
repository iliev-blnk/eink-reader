#!/usr/bin/env python3
"""ina226: capturile înregistratorului de curent (MEAS-2), pentru E3 și E4.

    ina226.py captura /dev/ttyUSB0 -o E3/date/ina226-asteptare.csv --durata 60
    ina226.py rezumat E3/date/ina226-*.csv [--tabel rezumat.csv] [--de S] [--pana S]
    ina226.py figura E3/date/ina226-pagini.csv -o figuri/curent.svg [--de S] [--pana S]
    ina226.py pagini E3/date/ina226-pagini.csv --pagini pagini.csv [--decalaj-us N] [-o energie.csv]

`captura` citește consola înregistratorului (tools/ina226/firmware) și scrie
rândurile așa cum vin: datele rămân brute. Restul le citesc, le trec în mA și mV
cu valorile din antet și scot:

  rezumat  pe fiecare fișier: eșantioane, rata, golurile, curentul mediu,
           median, p99, minim și maxim, tensiunea, puterea, energia, sarcina;
  figura   curentul în timp (SVG), cu media și maximul pe fiecare coloană;
  pagini   energia fiecărei întoarceri de pagină (E3): intervalele vin din
           `perf.py --pagini`, iar ceasul cititorului se aliniază cu al
           înregistratorului prin corelație (sau --decalaj-us).

Doar biblioteca standard.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import os
import pathlib
import re
import statistics
import sys
import time
from dataclasses import dataclass, field

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "measurements" / "common"))

ROW = re.compile(r"^(\d+),(-?\d+),(\d+)$")
MIN_RATE = 500  # E3: cel puțin 500 de eșantioane pe secundă


@dataclass
class Capture:
    name: str
    meta: dict = field(default_factory=dict)
    t: list = field(default_factory=list)       # s, de la primul eșantion
    ma: list = field(default_factory=list)
    mv: list = field(default_factory=list)
    lost: int = 0
    i2c_errors: int = 0
    restarts: int = 0
    t0_us: int = 0


def parse_header(line: str) -> dict:
    out = {}
    for part in line.lstrip("#").strip().split(",")[1:]:
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v
    return out


def read(path) -> Capture:
    """Citește o captură. Un rând de date stricat (zgomot la pornire) se sare.
    Dacă înregistratorul a repornit (ceasul scade), se păstrează doar prima bucată."""
    cap = Capture(os.path.basename(str(path)))
    raw_t, raw_sh, raw_bus = [], [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# ina226,"):
                if not cap.meta:
                    cap.meta = parse_header(line)
            elif line.startswith("# pierdute,"):
                cap.lost = max(cap.lost, int(line.split(",")[1]))
            elif line.startswith("# erori_i2c,"):
                cap.i2c_errors = max(cap.i2c_errors, int(line.split(",")[1]))
            elif m := ROW.match(line):
                t = int(m.group(1))
                if raw_t and t <= raw_t[-1]:
                    cap.restarts += 1
                    break
                raw_t.append(t)
                raw_sh.append(int(m.group(2)))
                raw_bus.append(int(m.group(3)))
    if not cap.meta:
        raise SystemExit(f"{path}: fără antetul „# ina226,...”: nu e o captură a înregistratorului")
    if not raw_t:
        raise SystemExit(f"{path}: niciun eșantion")
    r_mohm = float(cap.meta["shunt_mohm"])
    sh_nv = float(cap.meta.get("shunt_lsb_nv", 2500))
    bus_uv = float(cap.meta.get("bus_lsb_uv", 1250))
    cap.t0_us = raw_t[0]
    cap.t = [(t - raw_t[0]) / 1e6 for t in raw_t]
    # I = U_șunt / R: nV / mΩ = µA
    cap.ma = [s * sh_nv / r_mohm / 1000 for s in raw_sh]
    cap.mv = [b * bus_uv / 1000 for b in raw_bus]
    return cap


def window(cap: Capture, de=None, pana=None) -> Capture:
    if de is None and pana is None:
        return cap
    i = bisect.bisect_left(cap.t, de or 0)
    j = bisect.bisect_right(cap.t, pana if pana is not None else cap.t[-1])
    out = Capture(cap.name, cap.meta, cap.t[i:j], cap.ma[i:j], cap.mv[i:j], cap.lost, cap.i2c_errors, cap.restarts,
                  cap.t0_us)
    if len(out.t) < 2:
        raise SystemExit(f"{cap.name}: fereastra {de}–{pana} s are sub 2 eșantioane")
    return out


def integrate(t, y, a=None, b=None) -> float:
    """∫y dt prin trapeze, pe [a, b] (implicit: tot)."""
    i = 0 if a is None else max(bisect.bisect_left(t, a) - 1, 0)
    j = len(t) if b is None else min(bisect.bisect_right(t, b) + 1, len(t))
    s = 0.0
    for k in range(i + 1, j):
        t0, t1 = t[k - 1], t[k]
        if a is not None:
            t0 = max(t0, a)
        if b is not None:
            t1 = min(t1, b)
        if t1 > t0:
            # y liniar între eșantioane, evaluat la capetele tăiate
            dt = t[k] - t[k - 1]
            y0 = y[k - 1] + (y[k] - y[k - 1]) * (t0 - t[k - 1]) / dt
            y1 = y[k - 1] + (y[k] - y[k - 1]) * (t1 - t[k - 1]) / dt
            s += (y0 + y1) / 2 * (t1 - t0)
    return s


def pct(sorted_v, p):
    k = (len(sorted_v) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(sorted_v) - 1)
    return sorted_v[lo] + (sorted_v[hi] - sorted_v[lo]) * (k - lo)


def summary(cap: Capture) -> dict:
    n = len(cap.t)
    dur = cap.t[-1] - cap.t[0]
    dts = [b - a for a, b in zip(cap.t, cap.t[1:])]
    med_dt = statistics.median(dts) if dts else 0
    gaps = sum(1 for d in dts if d > 1.5 * med_dt)
    s = sorted(cap.ma)
    p_mw = [i * v / 1000 for i, v in zip(cap.ma, cap.mv)]
    return {
        "fisier": cap.name,
        "esantioane": n,
        "durata_s": dur,
        "rata_hz": (n - 1) / dur if dur > 0 else 0,
        "goluri": gaps,
        "pierdute": cap.lost,
        "erori_i2c": cap.i2c_errors,
        "ma_medie": statistics.fmean(cap.ma),
        "ma_mediana": pct(s, 0.5),
        "ma_p99": pct(s, 0.99),
        "ma_min": s[0],
        "ma_max": s[-1],
        "mv_medie": statistics.fmean(cap.mv),
        "mw_medie": statistics.fmean(p_mw),
        "energie_mj": integrate(cap.t, p_mw),          # mW·s = mJ
        "sarcina_mah": integrate(cap.t, cap.ma) / 3600,
        "rezolutie_ua": float(cap.meta.get("shunt_lsb_nv", 2500)) / float(cap.meta["shunt_mohm"]),
    }


def ro(v, nd=2):
    return f"{v:.{nd}f}".replace(".", ",")


def cmd_rezumat(args) -> int:
    rows = []
    for p in args.fisiere:
        cap = window(read(p), args.de, args.pana)
        s = summary(cap)
        rows.append(s)
        print(f"{s['fisier']}")
        print(f"  {s['esantioane']} eșantioane în {ro(s['durata_s'], 1)} s: {ro(s['rata_hz'], 0)}/s, "
              f"{s['goluri']} goluri, {s['pierdute']} pierdute, {s['erori_i2c']} erori I2C"
              + (f", REPORNIT de {cap.restarts} ori (păstrată prima bucată)" if cap.restarts else ""))
        if s["rata_hz"] < MIN_RATE:
            print(f"  ATENȚIE: sub {MIN_RATE}/s, cât cere E3")
        print(f"  curent: medie {ro(s['ma_medie'], 3)} mA, median {ro(s['ma_mediana'], 3)}, "
              f"p99 {ro(s['ma_p99'], 3)}, min {ro(s['ma_min'], 3)}, max {ro(s['ma_max'], 3)} "
              f"(rezoluția {ro(s['rezolutie_ua'], 1)} µA)")
        print(f"  tensiune {ro(s['mv_medie'], 0)} mV, putere {ro(s['mw_medie'], 2)} mW, "
              f"energie {ro(s['energie_mj'], 1)} mJ, sarcină {ro(s['sarcina_mah'], 4)} mAh")
    if args.tabel:
        with open(args.tabel, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            for r in rows:
                w.writerow({k: (f"{v:.6g}" if isinstance(v, float) else v) for k, v in r.items()})
    return 0


def cmd_figura(args) -> int:
    import svgplot

    cap = window(read(args.fisier), args.de, args.pana)
    # pe coloane: media și maximul, ca vârfurile scurte să nu dispară la micșorare
    cols = min(args.coloane, len(cap.t))
    t0, t1 = cap.t[0], cap.t[-1]
    span = (t1 - t0) or 1
    xs, mean, peak = [], [], []
    k = 0
    for c in range(cols):
        end = t0 + span * (c + 1) / cols
        seg = []
        while k < len(cap.t) and (cap.t[k] <= end or c == cols - 1):
            seg.append(cap.ma[k])
            k += 1
        if seg:
            xs.append(t0 + span * (c + 0.5) / cols)
            mean.append(statistics.fmean(seg))
            peak.append(max(seg))
    svgplot.lines(str(args.output), args.titlu or f"Curentul: {cap.name}", "timp (s)", "curent (mA)",
                  [("media", xs, mean), ("maximul", xs, peak)], ylo=min(0, min(mean)))
    print(f"scris {args.output} ({len(xs)} coloane din {len(cap.t)} eșantioane)")
    return 0


def read_pages(path, fisier=None):
    """Întoarcerile din `perf.py --pagini`: (fișier, început µs, sfârșit µs), în ceasul cititorului.
    Începutul e frontul butonului dacă e (întoarcerea automată n-are), sfârșitul BUSY eliberat."""
    out = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if fisier and r["fisier"] != fisier:
                continue
            start = int(r["t_us"]) - (float(r["intrare"]) * 1000 if r.get("intrare") else 0)
            out.append((r["fisier"], start, start + float(r["total"]) * 1000))
    files = {p[0] for p in out}
    if len(files) > 1:
        raise SystemExit(f"{path}: întoarceri din {len(files)} porniri ({', '.join(sorted(files))}); "
                         "fiecare pornire are ceasul ei, alege una cu --fisier")
    if not out:
        raise SystemExit(f"{path}: nicio întoarcere")
    return [(a, b) for _, a, b in out]


def align(cap: Capture, pages, step_us=1000):
    """Decalajul d (µs) cu t_înregistrator = t_cititor + d care pune întoarcerile peste
    curentul cel mai mare. Curentul se mediază pe bucăți de 1 ms, fără medie (scăzută),
    și se caută d care maximizează suma lui pe intervalele întoarcerilor: întâi din
    10 în 10 ms, apoi la 1 ms în jurul celui mai bun. Întoarce (d, calitatea): calitatea
    e cât de mult iese vârful peste cel mai bun decalaj din afara lobului lui (mai departe
    decât cea mai lungă întoarcere: până acolo întoarcerile se suprapun cu ele însele)."""
    t_us = [cap.t0_us + t * 1e6 for t in cap.t]
    lo, hi = t_us[0], t_us[-1]
    nb = int((hi - lo) // step_us) + 1
    sums, cnt = [0.0] * nb, [0] * nb
    for t, i in zip(t_us, cap.ma):
        b = int((t - lo) // step_us)
        sums[b] += i
        cnt[b] += 1
    # bucățile goale iau valoarea de dinainte
    vals, last = [], cap.ma[0]
    for s, c in zip(sums, cnt):
        last = s / c if c else last
        vals.append(last)
    m = statistics.fmean(vals)
    pre = [0.0]
    for v in vals:
        pre.append(pre[-1] + v - m)

    def score(d):
        s = 0.0
        for a, b in pages:
            i = int((a + d - lo) // step_us)
            j = int((b + d - lo) // step_us)
            if j < 0 or i >= nb:
                continue
            s += pre[min(max(j, 0), nb)] - pre[min(max(i, 0), nb)]
        return s

    first, last_end = pages[0][0], pages[-1][1]
    # întoarcerile trebuie să încapă în captură
    d_lo, d_hi = lo - first, hi - last_end
    if d_hi < d_lo:
        raise SystemExit("întoarcerile țin mai mult decât captura: nu se pot alinia")
    coarse = 10 * step_us
    cands = [(score(d), d) for d in range(int(d_lo), int(d_hi) + 1, coarse)]
    best_s, best_d = max(cands)
    fine = [(score(d), d) for d in range(int(best_d - coarse), int(best_d + coarse) + 1, step_us)]
    best_s, best_d = max(fine)
    lobe = max(b - a for a, b in pages)
    others = [s for s, d in cands if abs(d - best_d) > lobe]
    second = max(others) if others else 0.0
    quality = best_s / second if second > 0 else float("inf")
    return best_d, quality


def cmd_pagini(args) -> int:
    cap = read(args.fisier)
    pages = read_pages(args.pagini, args.fisier_perf)
    if args.decalaj_us is not None:
        d, q = args.decalaj_us, None
    else:
        d, q = align(cap, pages)
        print(f"aliniere: t_înregistrator = t_cititor + {d} µs (vârful de {ro(q, 1)}× peste următorul)")
        if q < 1.5:
            print("ATENȚIE: alinierea nu e clară (sub 1,5×); verifică pe figură și dă --decalaj-us")
    p_mw = [i * v / 1000 for i, v in zip(cap.ma, cap.mv)]
    t_us = cap.t0_us
    rows = []
    for k, (a, b) in enumerate(pages):
        ta, tb = (a + d - t_us) / 1e6, (b + d - t_us) / 1e6
        if ta < cap.t[0] or tb > cap.t[-1]:
            continue
        e = integrate(cap.t, p_mw, ta, tb)
        rows.append({"pagina": k, "start_s": ta, "durata_ms": (tb - ta) * 1000, "energie_mj": e,
                     "ma_medie": integrate(cap.t, cap.ma, ta, tb) / (tb - ta)})
    if not rows:
        raise SystemExit("nicio întoarcere în captură")
    es = sorted(r["energie_mj"] for r in rows)
    print(f"{len(rows)} întoarceri: energia pe pagină median {ro(pct(es, 0.5), 2)} mJ, "
          f"p95 {ro(pct(es, 0.95), 2)}, min {ro(es[0], 2)}, max {ro(es[-1], 2)} mJ")
    if args.output:
        with open(args.output, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            for r in rows:
                w.writerow({k: (f"{v:.6g}" if isinstance(v, float) else v) for k, v in r.items()})
    return 0


def cmd_captura(args) -> int:
    import termios

    fd = os.open(args.port, os.O_RDONLY | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{args.baud}", None)
    if speed is None:
        raise SystemExit(f"viteza {args.baud} nu e suportată de termios")
    attrs[0] = 0                                   # iflag
    attrs[1] = 0                                   # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0                                   # lflag: fără ecou, fără linii
    attrs[4] = attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 2                    # 0,2 s
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)

    out = open(args.output, "w", encoding="utf-8")
    out.write(f"# captura,port={args.port},inceput={time.strftime('%Y-%m-%dT%H:%M:%S')}"
              + (f",nota={args.nota}" if args.nota else "") + "\n")
    buf, have_header, first_t, n, last_print = b"", False, None, 0, time.monotonic()
    deadline = time.monotonic() + args.durata + 30   # dacă nu vine nimic, nu stă la nesfârșit
    try:
        while time.monotonic() < deadline:
            chunk = os.read(fd, 65536)
            if not chunk:
                continue
            buf += chunk
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode("utf-8", "replace").strip()
                if line.startswith("# eroare"):
                    print(line, file=sys.stderr)
                if line.startswith("# ina226,"):
                    have_header = True
                if not have_header:
                    continue                       # zgomotul de la pornire, înaintea primului antet
                m = ROW.match(line)
                if m:
                    t = int(m.group(1))
                    first_t = t if first_t is None else first_t
                    if (t - first_t) / 1e6 >= args.durata:
                        raise StopIteration
                    n += 1
                if line.startswith("#") or m:
                    out.write(line + "\n")
            if time.monotonic() - last_print > 5:
                print(f"  {n} eșantioane", file=sys.stderr)
                last_print = time.monotonic()
    except StopIteration:
        pass
    except KeyboardInterrupt:
        print("oprit", file=sys.stderr)
    finally:
        out.close()
        os.close(fd)
    if not have_header:
        print("niciun antet de la înregistrator: portul, viteza sau firmware-ul?", file=sys.stderr)
        return 1
    print(f"scris {args.output}: {n} eșantioane")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("captura", help="citește înregistratorul și scrie captura")
    c.add_argument("port")
    c.add_argument("-o", "--output", required=True, type=pathlib.Path)
    c.add_argument("--durata", type=float, default=60, help="secunde de date (implicit 60)")
    c.add_argument("--baud", type=int, default=921600, help="pentru ESP32 cu UART; pe C3 (USB) nu contează")
    c.add_argument("--nota", help="text scris în antet: placa, starea, commitul firmware-ului")
    c.set_defaults(fn=cmd_captura)

    r = sub.add_parser("rezumat", help="cifrele fiecărei capturi")
    r.add_argument("fisiere", nargs="+", type=pathlib.Path)
    r.add_argument("--tabel", type=pathlib.Path, help="scrie rezumatele ca CSV")
    r.add_argument("--de", type=float, help="de la secunda")
    r.add_argument("--pana", type=float, help="până la secunda")
    r.set_defaults(fn=cmd_rezumat)

    f = sub.add_parser("figura", help="curentul în timp, SVG")
    f.add_argument("fisier", type=pathlib.Path)
    f.add_argument("-o", "--output", required=True, type=pathlib.Path)
    f.add_argument("--de", type=float)
    f.add_argument("--pana", type=float)
    f.add_argument("--coloane", type=int, default=600)
    f.add_argument("--titlu")
    f.set_defaults(fn=cmd_figura)

    p = sub.add_parser("pagini", help="energia pe întoarcere de pagină")
    p.add_argument("fisier", type=pathlib.Path)
    p.add_argument("--pagini", required=True, type=pathlib.Path, help="CSV-ul de la perf.py --pagini")
    p.add_argument("--fisier-perf", help="pornirea din CSV, dacă sunt mai multe (coloana fisier)")
    p.add_argument("--decalaj-us", type=int, help="decalajul dat de mână, în loc de corelație")
    p.add_argument("-o", "--output", type=pathlib.Path, help="scrie energia fiecărei pagini ca CSV")
    p.set_defaults(fn=cmd_pagini)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
