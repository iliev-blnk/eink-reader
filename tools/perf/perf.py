#!/usr/bin/env python3
"""perf: analiza jurnalelor de măsurători ale cititorului (FW-13).

    perf.py perf/0003.csv [...] [--pagini PAGINI.csv] [--baterie BATERIE.csv]

Citește fișierele perf/NNNN.csv scrise de firmware (sau de `sim ui`, cu
dosarul CARD/perf) și scoate:

  E1  reîmprospătările panoului, după tip: scrierea pe SPI, BUSY, totalul;
  E2  întoarcerea unei pagini de la cap la coadă, pe bucăți: intrarea
      (frontul butonului → evenimentul), pregătirea, paginarea, desenarea,
      SPI, panoul;
  E4  descărcarea bateriei cu întoarcerea automată: tensiunea în timp,
      paginile întoarse, cât a ținut;
  E6  paginarea pe dispozitiv: ms pe pagină, la lectură și la index;
cât a stat cipul în somnul ușor (FW-7) și cât au costat scrierile jurnalului
însuși.

--pagini scrie câte un rând pe întoarcere (E2), --baterie curba (E4), ca CSV,
pentru graficele din lucrare. Doar biblioteca standard.

Rândurile sunt `t_us,event,a,b` (firmware/components/ui/include/perflog.h);
fiecare fișier e o pornire, cu ceasul de la zero.
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import statistics
import sys
from dataclasses import dataclass, field

KEY_PREV, KEY_NEXT, KEY_MENU = 0, 1, 2
SHORT, LONG = 0, 1
LOW_MV = 3400       # ca în ui.c


@dataclass
class Ev:
    t: int          # µs
    name: str
    a: int
    b: int
    i: int          # ordinea din fișier, la timpi egali


@dataclass
class Refresh:
    end: int
    full: bool
    total: int
    write: int
    busy: int
    rect: tuple[int, int, int, int]

    @property
    def start(self) -> int:
        return self.end - self.total


@dataclass
class Turn:
    file: str
    start: int                  # evenimentul `key` sau `auto_turn`
    trigger: str                # „ÎNAINTE”, „ÎNAPOI”, „auto”
    edge: int | None = None     # frontul butonului, dacă e în jurnal
    layout_begin: int | None = None
    layout_end: int | None = None
    lines: int = 0
    refresh: Refresh | None = None

    def complete(self) -> bool:
        return self.layout_end is not None and self.refresh is not None

    def parts(self) -> dict[str, float]:
        """Bucățile, în ms. `total` de la front (dacă e) sau de la eveniment până la BUSY eliberat."""
        r = self.refresh
        p = {
            "pregătire": (self.layout_begin - self.start) / 1000,
            "paginare": (self.layout_end - self.layout_begin) / 1000,
            "desenare": (r.start - self.layout_end) / 1000,
            "spi": r.write / 1000,
            "panou": r.busy / 1000,
            "total": (r.end - (self.edge if self.edge is not None else self.start)) / 1000,
        }
        if self.edge is not None:
            p["intrare"] = (self.start - self.edge) / 1000
        return p


@dataclass
class Log:
    file: str
    events: list[Ev] = field(default_factory=list)


def read(path: pathlib.Path) -> Log:
    log = Log(path.name)
    with open(path, newline="") as f:
        r = csv.reader(f)
        head = next(r, None)
        if head != ["t_us", "event", "a", "b"]:
            raise SystemExit(f"{path}: nu e un jurnal de măsurători (antetul e {head})")
        for i, row in enumerate(r):
            if len(row) != 4:
                continue    # un rând tăiat: cardul scos în timpul scrierii
            try:
                log.events.append(Ev(int(row[0]), row[1], int(row[2]), int(row[3]), i))
            except ValueError:
                continue
    # Frontul butonului intră în jurnal după evenimentele de dinaintea lui, cu timpul lui.
    log.events.sort(key=lambda e: (e.t, e.i))
    return log


def refreshes(log: Log) -> dict[int, Refresh]:
    """Reîmprospătările, după ordinea rândului `refresh` în fișier."""
    out = {}
    ev = log.events
    for k, e in enumerate(ev):
        if e.name != "refresh":
            continue
        write = busy = 0
        rect = (0, 0, 0, 0)
        for x in ev[k + 1:k + 4]:
            if x.t != e.t:
                break
            if x.name == "refresh_io":
                write, busy = x.a, x.b
            elif x.name == "refresh_rect":
                rect = (x.a >> 16, x.a & 0xFFFF, x.b >> 16, x.b & 0xFFFF)
        out[e.i] = Refresh(e.t, bool(e.a), e.b, write, busy, rect)
    return out


def turns(log: Log) -> list[Turn]:
    """Întoarcerile de pagină: ÎNAINTE / ÎNAPOI scurt sau o întoarcere automată, urmate de paginare și de o reîmprospătare."""
    out: list[Turn] = []
    rs = refreshes(log)
    cur: Turn | None = None
    edge: Ev | None = None
    for e in log.events:
        if e.name == "btn_edge":
            edge = e
        elif e.name in ("key", "auto_turn"):
            if cur and cur.complete():
                out.append(cur)
            cur = None
            if e.name == "auto_turn":
                cur = Turn(log.file, e.t, "auto")
            elif e.a in (KEY_PREV, KEY_NEXT) and e.b == SHORT:
                cur = Turn(log.file, e.t, "ÎNAINTE" if e.a == KEY_NEXT else "ÎNAPOI")
                # Frontul care a produs apăsarea: același buton, cel mult o secundă înainte.
                if edge and edge.a == e.a and edge.b == e.b and 0 <= e.t - edge.t < 1_000_000:
                    cur.edge = edge.t
            edge = None
        elif cur is None:
            continue
        elif e.name == "layout_begin" and cur.layout_begin is None:
            cur.layout_begin = e.t
        elif e.name == "layout_end" and cur.layout_begin is not None and cur.layout_end is None:
            cur.layout_end, cur.lines = e.t, e.a
        elif e.name == "refresh" and cur.layout_end is not None and cur.refresh is None:
            cur.refresh = rs[e.i]
            out.append(cur)
            cur = None
    if cur and cur.complete():
        out.append(cur)
    return out


def pct(xs: list[float], p: float) -> float:
    s = sorted(xs)
    if not s:
        return float("nan")
    k = (len(s) - 1) * p
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def stats_row(name: str, xs: list[float]) -> str:
    if not xs:
        return f"  {name:<12} —"
    return (f"  {name:<12} n={len(xs):<5} medie {statistics.fmean(xs):9.1f}  median {pct(xs, .5):9.1f}"
            f"  p95 {pct(xs, .95):9.1f}  min {min(xs):9.1f}  max {max(xs):9.1f}")


def report_e1(logs: list[Log]) -> None:
    print("E1  reîmprospătările panoului (ms)")
    allr = [r for log in logs for r in refreshes(log).values()]
    for full, name in ((False, "parțială"), (True, "completă")):
        rs = [r for r in allr if r.full == full]
        print(f" {name}: {len(rs)}")
        if rs:
            print(stats_row("spi", [r.write / 1000 for r in rs]))
            print(stats_row("busy", [r.busy / 1000 for r in rs]))
            print(stats_row("total", [r.total / 1000 for r in rs]))
        part = [r for r in rs if not r.full and r.rect[2]]
        if part:
            print(stats_row("zona, kpx", [r.rect[2] * r.rect[3] / 1000 for r in part]))


PARTS = ("intrare", "pregătire", "paginare", "desenare", "spi", "panou", "total")


def report_e2(ts: list[Turn]) -> None:
    print("E2  întoarcerea paginii, de la front la BUSY eliberat (ms)")
    for name, sel in (("toate", ts), ("parțiale", [t for t in ts if not t.refresh.full]),
                      ("complete", [t for t in ts if t.refresh.full])):
        if not sel:
            continue
        by = {}
        for t in sel:
            for k, v in t.parts().items():
                by.setdefault(k, []).append(v)
        trig = {}
        for t in sel:
            trig[t.trigger] = trig.get(t.trigger, 0) + 1
        print(f" {name}: {len(sel)} ({', '.join(f'{k} {v}' for k, v in sorted(trig.items()))})")
        for k in PARTS:
            if k in by:
                print(stats_row(k, by[k]))


def battery_curve(log: Log) -> list[tuple[float, int, int]]:
    """(ore de la prima întoarcere automată, mV, pagini întoarse până atunci)."""
    t0 = None
    pages = 0
    out = []
    for e in log.events:
        if e.name == "auto_turn":
            if t0 is None:
                t0 = e.t
            pages += 1
            if e.b:
                out.append(((e.t - t0) / 3.6e9, e.b, pages))
        elif e.name == "battery" and t0 is not None and e.a:
            out.append(((e.t - t0) / 3.6e9, e.a, pages))
    return out


def report_e4(logs: list[Log]) -> list[tuple[str, float, int, int]]:
    rows = []
    print("E4  bateria cu întoarcerea automată")
    for log in logs:
        c = battery_curve(log)
        if not c:
            continue
        rows += [(log.file, h, mv, p) for h, mv, p in c]
        slept = any(e.name == "sleep" for e in log.events)
        how = ("oprit, baterie descărcată" if slept and c[-1][1] < LOW_MV else "adormit" if slept
               else "jurnalul se termină fără somn")
        print(f" {log.file}: {c[-1][2]} pagini în {c[-1][0]:.2f} h, {c[0][1]} → {c[-1][1]} mV; {how}")
    if not rows:
        print(" (nicio întoarcere automată)")
    return rows


def report_e6(logs: list[Log], ts: list[Turn]) -> None:
    print("E6  paginarea (ms pe pagină)")
    print(stats_row("lectură", [(t.layout_end - t.layout_begin) / 1000 for t in ts]))
    idx = [e for log in logs for e in log.events if e.name == "index_step" and e.a]
    # Pe dispozitiv b e în ms (ui_now); în simulator ceasul e simulat, deci 0.
    print(stats_row("index", [e.b / e.a for e in idx]))


def report_light_sleep(logs: list[Log]) -> None:
    # Rândul se scrie o dată, la intrarea în somnul adânc: a = ms dormiți, b = de câte ori.
    print("Somnul ușor (FW-7)")
    found = False
    for log in logs:
        for e in log.events:
            if e.name == "light_sleep" and e.t > 0:
                found = True
                print(f" {log.file}: {e.a / 1000:.1f} s din {e.t / 1e6:.1f} s ({100 * e.a * 1000 / e.t:.0f}%),"
                      f" {e.b} adormiri")
    if not found:
        print(" (niciun rând; firmware fără somn ușor sau jurnal tăiat înainte de somnul adânc)")


def report_log(logs: list[Log]) -> None:
    fl = [e for log in logs for e in log.events if e.name == "perf_flush"]
    dropped = sum(e.a for log in logs for e in log.events if e.name == "perf_dropped")
    print("Jurnalul însuși")
    print(f"  {sum(len(l.events) for l in logs)} rânduri în {len(logs)} fișiere, {len(fl)} scrieri pe card,"
          f" {dropped} rânduri pierdute (memoria plină)")
    if fl:
        print(stats_row("scriere, ms", [e.b / 1000 for e in fl]))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Analiza jurnalelor perf/NNNN.csv (E1, E2, E4, E6).")
    ap.add_argument("files", nargs="+", type=pathlib.Path, metavar="FIȘIER")
    ap.add_argument("--pagini", type=pathlib.Path, help="scrie întoarcerile (E2) ca CSV")
    ap.add_argument("--baterie", type=pathlib.Path, help="scrie curba bateriei (E4) ca CSV")
    args = ap.parse_args(argv)

    logs = [read(p) for p in args.files]
    ts = [t for log in logs for t in turns(log)]
    report_e1(logs)
    print()
    report_e2(ts)
    print()
    bat = report_e4(logs)
    print()
    report_e6(logs, ts)
    print()
    report_light_sleep(logs)
    print()
    report_log(logs)

    if args.pagini:
        with open(args.pagini, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["fisier", "t_us", "declansare", "completa", "randuri"] + [k.replace("ă", "a") for k in PARTS])
            for t in ts:
                p = t.parts()
                w.writerow([t.file, t.start, t.trigger, int(t.refresh.full), t.lines]
                           + [f"{p[k]:.3f}" if k in p else "" for k in PARTS])
    if args.baterie:
        with open(args.baterie, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["fisier", "ore", "mv", "pagini"])
            for row in bat:
                w.writerow([row[0], f"{row[1]:.4f}", row[2], row[3]])
    return 0


if __name__ == "__main__":
    sys.exit(main())
