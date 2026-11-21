#!/usr/bin/env python3
"""Corecțiile de rotație și de origine pentru CPL-ul JLCPCB.

JLC nu așază piesa după amprenta KiCad, ci după amprenta EasyEDA a codului LCSC
ales. Cele două amprente pot fi desenate cu altă orientare (pinul 1 în alt colț)
și cu altă origine, așa că rotația și poziția din KiCad nu ajung direct în CPL.

Pentru fiecare piesă cu cod LCSC, scriptul suprapune padurile din KiCad peste cele
din EasyEDA și găsește rotația (multiplu de 90°) și deplasarea care le aduc unele
peste altele:

- padurile se împerechează după număr, dacă ambele amprente numerotează la fel;
  rotația vine din potrivirea Kabsch 2D, rotunjită la 90°;
- altfel (butoane, comutatoare numerotate diferit) se încearcă cele patru rotații
  fără nume: o rotație trece dacă fiecare pad cade pe un pad și invers.

Rezultatul merge în jlc_corectii.csv (urmărit în git), pe care make.sh îl aplică
la scrierea fab/cpl.csv, fără rețea. Datele EasyEDA stau doar în build/easyeda/
(bibliotecă EasyEDA/JLCEDA, https://easyeda.com, https://lceda.cn), nu în depozit.

    python3 hardware/pcb/jlc_rotatii.py              # din build/easyeda/
    python3 hardware/pcb/jlc_rotatii.py --descarca   # aduce întâi ce lipsește

Iese cu cod 1 dacă vreo piesă nu se potrivește sau se potrivește în oglindă.
"""
import csv
import json
import math
import sys
import time
import urllib.request
from pathlib import Path

import pcbnew

AICI = Path(__file__).resolve().parent
PLACA = AICI / "eink-reader-pcb.kicad_pcb"
CACHE = AICI / "build" / "easyeda"
IESIRE = AICI / "jlc_corectii.csv"
UA = "Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0"
URL = "https://easyeda.com/api/products/{}/components?version=6.4.19.5"
MIL10 = 0.254          # unitatea EasyEDA clasic: 10 mil, Y în jos (ca în KiCad)
TOL = 0.30             # mm: cât poate rata un pad centrul padului corespunzător
PRAG_ORIGINE = 0.05    # mm: sub atât, deplasarea originii e zgomot de desen


def roteste(p, grade):
    """Rotește (x, y) cu +X spre +Y (în cadrul KiCad, cu Y în jos, sensul acelor de ceas)."""
    a = math.radians(grade)
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c)


def centru(puncte):
    n = len(puncte)
    return (sum(p[0] for p in puncte) / n, sum(p[1] for p in puncte) / n)


def descarca(cod):
    tinta = CACHE / f"{cod}.json"
    if tinta.exists() and tinta.stat().st_size:
        return
    req = urllib.request.Request(URL.format(cod), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        tinta.write_bytes(r.read())
    time.sleep(0.5)


def paduri_easyeda(cod):
    """Întoarce (pachet, [(număr, x, y)]) în mm, față de originea amprentei EasyEDA."""
    r = json.loads((CACHE / f"{cod}.json").read_text(encoding="utf-8"))
    if not r.get("success") or not r.get("result"):
        raise ValueError(f"{cod}: EasyEDA nu are piesa")
    d = r["result"]["packageDetail"]["dataStr"]
    ox, oy = float(d["head"]["x"]), float(d["head"]["y"])
    paduri = []
    for s in d["shape"]:
        if not s.startswith("PAD~"):
            continue
        f = s.split("~")
        # PAD~formă~x~y~lățime~înălțime~strat~rețea~număr~...
        paduri.append((f[8].lstrip("0") or f[8], (float(f[2]) - ox) * MIL10, (float(f[3]) - oy) * MIL10))
    return r["result"]["packageDetail"]["title"], paduri


def paduri_kicad(fp):
    """Padurile cu cupru, în cadrul propriu al amprentei (fără rotația de pe placă), în mm."""
    paduri = []
    for pad in fp.Pads():
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            continue
        if not (pad.IsOnLayer(pcbnew.F_Cu) or pad.IsOnLayer(pcbnew.B_Cu)):
            continue
        p = pad.GetFPRelativePosition()
        paduri.append((pad.GetNumber(), pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)))
    return paduri


def pe_numere(kicad, jlc):
    """[(număr, centru KiCad, centru EasyEDA)] pentru numerele de pe ambele părți.

    Un număr cu mai multe paduri (padul expus cu vias termice, picioarele unui
    buton) intră cu centrul lor, deci contează și când numărul de paduri diferă."""
    def grupe(paduri):
        g = {}
        for n, x, y in paduri:
            if n:
                g.setdefault(n, []).append((x, y))
        return g
    gk, gj = grupe(kicad), grupe(jlc)
    return [(n, centru(gk[n]), centru(gj[n])) for n in gk if n in gj]


def ratare_numere(perechi, unghi, t):
    """Cea mai mare distanță dintre centrele unui număr, după rotație și translație (mm)."""
    rat = 0.0
    for _, a, b in perechi:
        r = roteste(a, unghi)
        rat = max(rat, math.hypot(r[0] + t[0] - b[0], r[1] + t[1] - b[1]))
    return rat


def kabsch(perechi):
    """Unghiul (grade) și translația care duc punctele KiCad peste cele EasyEDA."""
    ck = centru([a for a, _ in perechi])
    cj = centru([b for _, b in perechi])
    sin = cos = 0.0
    for (ax, ay), (bx, by) in perechi:
        ax, ay, bx, by = ax - ck[0], ay - ck[1], bx - cj[0], by - cj[1]
        sin += ax * by - ay * bx
        cos += ax * bx + ay * by
    unghi = math.degrees(math.atan2(sin, cos))
    r = roteste(ck, unghi)
    return unghi, (cj[0] - r[0], cj[1] - r[1])


def potrivire(kicad, jlc, unghi, t):
    """Cea mai mare ratare, în ambele sensuri, după rotație și translație (mm)."""
    puse = [roteste((x, y), unghi) for _, x, y in kicad]
    puse = [(x + t[0], y + t[1]) for x, y in puse]
    tinte = [(x, y) for _, x, y in jlc]
    d = lambda p, q: math.hypot(p[0] - q[0], p[1] - q[1])
    dus = max(min(d(p, q) for q in tinte) for p in puse)
    intors = max(min(d(q, p) for p in puse) for q in tinte)
    return max(dus, intors)


def translatie_geometrica(kicad, jlc, unghi):
    """Translația pentru o rotație fără nume: centrele cutiilor de paduri, apoi media ratărilor."""
    puse = [roteste((x, y), unghi) for _, x, y in kicad]
    tinte = [(x, y) for _, x, y in jlc]
    cutie = lambda pts: ((min(p[0] for p in pts) + max(p[0] for p in pts)) / 2,
                         (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2)
    ck, cj = cutie(puse), cutie(tinte)
    t = (cj[0] - ck[0], cj[1] - ck[1])
    dif = []
    for x, y in puse:
        p = (x + t[0], y + t[1])
        q = min(tinte, key=lambda q: math.hypot(p[0] - q[0], p[1] - q[1]))
        dif.append((q[0] - p[0], q[1] - p[1]))
    m = centru(dif)
    return (t[0] + m[0], t[1] + m[1])


def rezolva(kicad, jlc):
    """Întoarce (corecție, metodă, ratare_mm, translație, notă)."""
    perechi = pe_numere(kicad, jlc)
    numerotate = {n for n, _, _ in kicad if n}
    if len(perechi) >= 2 and len(perechi) == len(numerotate):
        p2 = [(a, b) for _, a, b in perechi]
        unghi, _ = kabsch(p2)
        cor = round(unghi / 90) * 90 % 360
        t = kabsch_t_fix(p2, cor)
        rat = ratare_numere(perechi, cor, t)
        # aceleași numere, dar în oglindă: o constatare despre amprentă, nu o rotație
        og = [(n, (a[0], -a[1]), b) for n, a, b in perechi]
        uo, _ = kabsch([(a, b) for _, a, b in og])
        co = round(uo / 90) * 90 % 360
        rat_og = ratare_numere(og, co, kabsch_t_fix([(a, b) for _, a, b in og], co))
        if len(perechi) > 2 and rat > TOL and rat_og <= TOL:
            return cor, "oglindă", rat, t, "numerotarea e în oglindă față de EasyEDA"
        if rat <= TOL:
            nota = ""
            if abs(((unghi - cor) + 180) % 360 - 180) > 5:
                nota = f"unghi brut {unghi:.1f}°"
            return cor, "numere", rat, t, nota
        # numerele nu se potrivesc ca poziții: se judecă doar geometric, mai jos
    # fără nume comune: cele patru rotații, judecate doar geometric
    bune = []
    for cor in (0, 90, 180, 270):
        t = translatie_geometrica(kicad, jlc, cor)
        rat = potrivire(kicad, jlc, cor, t)
        if rat <= TOL:
            bune.append((cor, rat, t))
    if not bune:
        return None, "nepotrivit", None, None, "nicio rotație nu pune padurile pe paduri"
    cor, rat, t = bune[0]
    nota = "" if len(bune) == 1 else "simetric: trec " + "/".join(str(b[0]) for b in bune)
    return cor, "geometric", rat, t, nota


def kabsch_t_fix(perechi, unghi):
    """Translația pentru un unghi fixat (rotunjit), prin centre."""
    ck = centru([a for a, _ in perechi])
    cj = centru([b for _, b in perechi])
    r = roteste(ck, unghi)
    return (cj[0] - r[0], cj[1] - r[1])


def main():
    placa = pcbnew.LoadBoard(str(PLACA))
    piese = []
    for fp in placa.GetFootprints():
        cod = fp.GetFieldText("LCSC") if fp.HasField("LCSC") else ""
        if cod:
            piese.append((fp.GetReference(), cod.strip().upper(), fp))
    piese.sort(key=lambda p: (p[0].rstrip("0123456789"), int(p[0][len(p[0].rstrip("0123456789")):] or 0)))

    CACHE.mkdir(parents=True, exist_ok=True)
    if "--descarca" in sys.argv:
        for cod in sorted({c for _, c, _ in piese}):
            descarca(cod)

    randuri, probleme = [], 0
    for ref, cod, fp in piese:
        kicad = paduri_kicad(fp)
        if fp.IsFlipped():   # amprenta de pe spate, oglindită înapoi în cadrul ei
            kicad = [(n, x, -y) for n, x, y in kicad]
        try:
            pachet, jlc = paduri_easyeda(cod)
        except (OSError, ValueError, KeyError) as e:
            print(f"EȘEC {ref} {cod}: {e}")
            probleme += 1
            continue
        cor, metoda, rat, t, nota = rezolva(kicad, jlc)
        if cor is None or metoda == "oglindă":
            print(f"EȘEC {ref} {cod} {pachet}: {nota}")
            probleme += 1
            continue
        # originea EasyEDA, adusă în cadrul amprentei KiCad: R(-cor) * (0 - t)
        o = roteste((-t[0], -t[1]), -cor)
        if math.hypot(*o) < PRAG_ORIGINE:
            o = (0.0, 0.0)
        randuri.append([ref, cod, fp.GetFPIDAsString().split(":")[-1], pachet, cor,
                        f"{o[0]:.3f}", f"{o[1]:.3f}", metoda, f"{rat:.3f}", nota])
        print(f"ok   {ref:5} {cod:9} {pachet:45} {cor:3}°  origine ({o[0]:+.2f}, {o[1]:+.2f}) mm"
              f"  {metoda}, ratare {rat:.2f} mm  {nota}")

    with open(IESIRE, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["ref", "lcsc", "amprenta_kicad", "pachet_easyeda", "rotatie", "origine_x_mm",
                    "origine_y_mm", "metoda", "ratare_mm", "nota"])
        w.writerows(randuri)
    print(f"{len(randuri)} piese în {IESIRE.name}, {probleme} probleme")
    sys.exit(1 if probleme else 0)


if __name__ == "__main__":
    main()
