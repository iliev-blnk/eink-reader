#!/usr/bin/env python3
"""Corecțiile de rotație și de origine pentru CPL-ul JLCPCB.

JLC nu așază piesa după amprenta KiCad, ci după amprenta EasyEDA a codului LCSC
ales. Cele două amprente pot fi desenate cu altă orientare (pinul 1 în alt colț)
și cu altă origine, așa că rotația și poziția din KiCad nu ajung direct în CPL.

Pentru fiecare piesă cu cod LCSC, scriptul suprapune padurile din KiCad peste cele
din EasyEDA și găsește rotația (multiplu de 90°) și deplasarea care le aduc unele
peste altele:

- dacă ambele amprente numerotează la fel, padurile se împerechează după număr;
  rotația vine din potrivirea Kabsch 2D, rotunjită la 90°, iar dacă numerele nu
  se potrivesc piesa e refuzată (altă configurație de pini, de ex. SOT-23 -CW);
- la LED-uri și diode, KiCad numerotează catodul 1, iar EasyEDA nu întotdeauna:
  catodul se ia după semnul „+” din amprenta EasyEDA (stratul de polaritate),
  după care se ia și previzualizarea JLC, iar fără semn după simbol (K/C și A);
- altfel (butoane, conectoare și pachete numerotate diferit) se încearcă cele
  patru rotații geometric; dintre cele care trec, câștigă cea la care cad cele mai
  multe numere pe ele însele.

O rotație trece dacă fiecare pad se suprapune cu perechea lui, în ambele sensuri:
centrul unuia cade în cuprul celuilalt. Pragul de distanță nu merge: EasyEDA
desenează des padurile mai lungi, cu centrele mutate cu 0,2–0,4 mm.

Rezultatul merge în jlc_corectii.csv (urmărit în git), pe care make.sh îl aplică
la scrierea fab/cpl.csv, fără rețea. Datele EasyEDA stau doar în build/easyeda/
(bibliotecă EasyEDA/JLCEDA, https://easyeda.com, https://lceda.cn), nu în depozit.
test_jlc_rotatii.py verifică metoda pe o placă cu rotațiile confirmate de JLC.

    python3 hardware/pcb/jlc_rotatii.py              # din build/easyeda/
    python3 hardware/pcb/jlc_rotatii.py --descarca   # aduce întâi ce lipsește

Iese cu cod 1 dacă vreo piesă nu se potrivește sau se potrivește în oglindă.
"""
import csv
import json
import math
import re
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
EPS = 1e-3             # mm
PRAG_ORIGINE = 0.05    # mm: sub atât, deplasarea originii e zgomot de desen
CATOD = {"K", "C", "CATHODE", "KATHODE"}
ANOD = {"A", "ANODE"}

# Un pad e (număr, x, y, lățime, înălțime): centrul și cutia lui, în cadrul
# amprentei, în mm, cu Y în jos.


def roteste(p, grade):
    """Rotește (x, y) cu +X spre +Y (în cadrul KiCad, cu Y în jos, sensul acelor de ceas)."""
    a = math.radians(grade)
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c)


def cutie_rotita(w, h, grade):
    """Lățimea și înălțimea cutiei unui dreptunghi w×h rotit."""
    a = math.radians(grade)
    c, s = abs(math.cos(a)), abs(math.sin(a))
    return (w * c + h * s, w * s + h * c)


def centru(puncte):
    n = len(puncte)
    return (sum(p[0] for p in puncte) / n, sum(p[1] for p in puncte) / n)


def centru_cutie(paduri):
    """Centrul cutiei care cuprinde padurile, cu tot cu mărimea lor."""
    x0 = min(p[1] - p[3] / 2 for p in paduri)
    x1 = max(p[1] + p[3] / 2 for p in paduri)
    y0 = min(p[2] - p[4] / 2 for p in paduri)
    y1 = max(p[2] + p[4] / 2 for p in paduri)
    return ((x0 + x1) / 2, (y0 + y1) / 2)


def descarca(cod):
    tinta = CACHE / f"{cod}.json"
    if tinta.exists() and tinta.stat().st_size:
        return
    req = urllib.request.Request(URL.format(cod), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        tinta.write_bytes(r.read())
    time.sleep(0.5)


def citeste_easyeda(cod, cache=None):
    """Întoarce (pachet, paduri, {număr pin: nume pin}) din răspunsul EasyEDA clasic."""
    r = json.loads(((cache or CACHE) / f"{cod}.json").read_text(encoding="utf-8"))
    if not r.get("success") or not r.get("result"):
        raise ValueError(f"{cod}: EasyEDA nu are piesa")
    d = r["result"]["packageDetail"]["dataStr"]
    ox, oy = float(d["head"]["x"]), float(d["head"]["y"])
    paduri = []
    for s in d["shape"]:
        if not s.startswith("PAD~"):
            continue
        # PAD~formă~x~y~lățime~înălțime~strat~rețea~număr~gaură~puncte~rotație~...
        f = s.split("~")
        w, h = cutie_rotita(float(f[4]) * MIL10, float(f[5]) * MIL10, float(f[11] or 0))
        paduri.append((f[8].lstrip("0") or f[8], (float(f[2]) - ox) * MIL10,
                       (float(f[3]) - oy) * MIL10, w, h))
    plus = semn_plus(d["shape"], ox, oy)
    pini = {}
    for s in r["result"].get("dataStr", {}).get("shape", []):
        if s.startswith("P~"):
            # P~afișare~tip~număr~...^^...^^...^^1~x~y~0~NUME~...
            parti = s.split("^^")
            try:
                pini[parti[0].split("~")[3].lstrip("0")] = parti[3].split("~")[4].strip().upper()
            except IndexError:
                pass
    return r["result"]["packageDetail"]["title"], paduri, pini, plus


def semn_plus(forme, ox, oy):
    """Centrul semnului „+” din stratul de polaritate (12) al amprentei EasyEDA, sau None.

    Semnul e desenat din bare pline: „+” e o bară verticală peste una orizontală,
    „−” doar una orizontală. Se ia centrul barelor verticale."""
    verticale = []
    for s in forme:
        f = s.split("~")
        if f[0] != "SOLIDREGION" or f[1] != "12":
            continue
        v = [float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", f[3])]
        xs, ys = v[0::2], v[1::2]
        w, h = max(xs) - min(xs), max(ys) - min(ys)
        if h > 2 * w:
            verticale.append((((max(xs) + min(xs)) / 2 - ox) * MIL10, ((max(ys) + min(ys)) / 2 - oy) * MIL10))
    return centru(verticale) if len(verticale) == 1 else None


def paduri_kicad(fp):
    """Padurile cu cupru, în cadrul propriu al amprentei (fără rotația de pe placă)."""
    paduri = []
    for pad in fp.Pads():
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            continue
        if not (pad.IsOnLayer(pcbnew.F_Cu) or pad.IsOnLayer(pcbnew.B_Cu)):
            continue
        p, m = pad.GetFPRelativePosition(), pad.GetSize()
        w, h = cutie_rotita(pcbnew.ToMM(m.x), pcbnew.ToMM(m.y),
                            pad.GetOrientationDegrees() - fp.GetOrientationDegrees())
        paduri.append((pad.GetNumber(), pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), w, h))
    if fp.IsFlipped():   # amprenta de pe spate, oglindită înapoi în cadrul ei
        paduri = [(n, x, -y, w, h) for n, x, y, w, h in paduri]
    return paduri


def catod_intai(amprenta, jlc, pini, plus):
    """La LED-uri și diode, renumerotează padurile EasyEDA cu catodul 1, ca în KiCad.

    Previzualizarea JLC se ia după semnul „+” (anodul) din amprenta EasyEDA; numele
    pinilor din simbol sunt doar a doua sursă și uneori greșesc (D12 din
    test_jlc_rotatii.py). Întoarce (paduri, catod găsit?, notă)."""
    if not amprenta.startswith(("LED_", "D_")):
        return jlc, True, ""
    numere = sorted({p[0] for p in jlc})
    if len(numere) != 2:
        return jlc, False, "nu are două paduri"
    k_simbol = [n for n, nume in pini.items() if nume in CATOD and n in numere]
    k_simbol = k_simbol[0] if len(k_simbol) == 1 else None
    k_semn = None
    if plus is not None:
        anod = min(jlc, key=lambda p: math.hypot(p[1] - plus[0], p[2] - plus[1]))[0]
        k_semn = next(n for n in numere if n != anod)
    k = k_semn or k_simbol
    if k is None:
        return jlc, False, "nici semn „+”, nici catod în simbol"
    nota = "catodul după semnul „+”" if k_semn else "catodul după simbol (fără semn „+”)"
    if k_semn and k_simbol and k_semn != k_simbol:
        nota += f", simbolul zice pinul {k_simbol}"
    if k == "1":
        return jlc, True, nota + ": pinul 1, ca în KiCad"
    return ([("1" if n == k else "2", x, y, w, h) for n, x, y, w, h in jlc], True,
            nota + f": pinul {k} în EasyEDA, împerecheat după catod")


def aseaza(paduri, unghi, t):
    """Padurile rotite (multiplu de 90°) și mutate."""
    rez = []
    for n, x, y, w, h in paduri:
        r = roteste((x, y), unghi)
        if unghi % 180:
            w, h = h, w
        rez.append((n, r[0] + t[0], r[1] + t[1], w, h))
    return rez


def acopera(a, b):
    """Centrul unuia dintre paduri cade în cuprul celuilalt."""
    def in_pad(p, q):
        return abs(p[1] - q[1]) <= q[3] / 2 + EPS and abs(p[2] - q[2]) <= q[4] / 2 + EPS
    return in_pad(a, b) or in_pad(b, a)


def comaseaza(kicad, jlc):
    """Un număr cu alt număr de paduri în cele două amprente (padul expus, cu vias
    termice desenate diferit) devine o singură cutie de fiecare parte."""
    def cate(paduri):
        c = {}
        for p in paduri:
            c[p[0]] = c.get(p[0], 0) + 1
        return c
    ck, cj = cate(kicad), cate(jlc)
    diferite = {n for n in ck if n in cj and ck[n] != cj[n]}

    def cu_cutii(paduri):
        rez = [p for p in paduri if p[0] not in diferite]
        for n in diferite:
            g = [p for p in paduri if p[0] == n]
            x0 = min(p[1] - p[3] / 2 for p in g)
            x1 = max(p[1] + p[3] / 2 for p in g)
            y0 = min(p[2] - p[4] / 2 for p in g)
            y1 = max(p[2] + p[4] / 2 for p in g)
            rez.append((n, (x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0))
        return rez
    return cu_cutii(kicad), cu_cutii(jlc)


def se_suprapun(kicad, jlc, pe_nume):
    """Fiecare pad are o pereche care îl acoperă, în ambele sensuri; întoarce (da/nu, ratare).

    pe_nume: perechea trebuie să aibă același număr (și padurile EasyEDA cu numere
    pe care KiCad nu le are rămân pe dinafară)."""
    if pe_nume:
        numere = {p[0] for p in kicad}
        jlc = [q for q in jlc if q[0] in numere]
        kicad, jlc = comaseaza(kicad, jlc)
    ratare = 0.0
    for sursa, tinta in ((kicad, jlc), (jlc, kicad)):
        for p in sursa:
            bune = [q for q in tinta if (not pe_nume or q[0] == p[0]) and acopera(p, q)]
            if not bune:
                return False, None
            ratare = max(ratare, min(math.hypot(p[1] - q[1], p[2] - q[2]) for q in bune))
    return True, ratare


def kabsch(perechi):
    """Unghiul (grade) care duce centrele KiCad peste cele EasyEDA, cu +X spre +Y."""
    ck = centru([a for a, _ in perechi])
    cj = centru([b for _, b in perechi])
    sin = cos = 0.0
    for (ax, ay), (bx, by) in perechi:
        ax, ay, bx, by = ax - ck[0], ay - ck[1], bx - cj[0], by - cj[1]
        sin += ax * by - ay * bx
        cos += ax * bx + ay * by
    return math.degrees(math.atan2(sin, cos))


def translatie(kicad, jlc, unghi):
    """Mută cutia padurilor KiCad rotite peste cutia padurilor EasyEDA.

    Cutiile, nu centrele de greutate: un SOT-23 are doi pini pe o parte și unul pe
    cealaltă, iar padurile mai lungi din EasyEDA ar muta centrul de greutate."""
    ck, cj = centru_cutie(aseaza(kicad, unghi, (0, 0))), centru_cutie(jlc)
    return (cj[0] - ck[0], cj[1] - ck[1])


def rezolva(kicad, jlc):
    """Întoarce (corecție, metodă, ratare_mm, translație, notă); corecție None = nu se potrivește.

    Dacă ambele amprente numerotează la fel (fiecare număr din KiCad există în EasyEDA,
    cu același număr de paduri, iar KiCad nu are paduri fără număr), numerele decid:
    se potrivesc sau piesa e refuzată. Altfel (padul termic numerotat 2 într-o parte și
    4 în alta, butoane 1,1,2,2 față de 1,2,3,4, paduri de prindere fără număr) se
    încearcă cele patru rotații geometric; dintre cele care trec, câștigă cea la care
    cele mai multe numere unice în ambele amprente cad pe ele însele."""
    def cate(paduri):
        c = {}
        for p in paduri:
            c[p[0]] = c.get(p[0], 0) + 1
        return c

    def grupe(paduri):
        g = {}
        for n, x, y, *_ in paduri:
            g.setdefault(n, []).append((x, y))
        return g
    ck, cj = cate(kicad), cate(jlc)
    la_fel = len(ck) >= 2 and "" not in ck and all(
        n in cj and (ck[n] == cj[n] or min(ck[n], cj[n]) > 1) for n in ck)
    if la_fel:
        gk, gj = grupe(kicad), grupe(jlc)
        unghi = kabsch([(centru(gk[n]), centru(gj[n])) for n in gk])
        cor = round(unghi / 90) * 90 % 360
        jc = [q for q in jlc if q[0] in gk]
        t = translatie(kicad, jc, cor)
        ok, rat = se_suprapun(aseaza(kicad, cor, t), jlc, True)
        if ok:
            nota = f"unghi brut {unghi:.1f}°" if abs((unghi - cor + 180) % 360 - 180) > 5 else ""
            return cor, "numere", rat, t, nota
        # aceleași numere, în oglindă: o constatare despre amprentă, nu o rotație
        og = [(n, x, -y, w, h) for n, x, y, w, h in kicad]
        go = grupe(og)
        co = round(kabsch([(centru(go[n]), centru(gj[n])) for n in go]) / 90) * 90 % 360
        if len(gk) > 2 and se_suprapun(aseaza(og, co, translatie(og, jc, co)), jlc, True)[0]:
            return None, "oglindă", None, None, "numerotarea e în oglindă față de EasyEDA"
        # aceleași numere, alte poziții: altă configurație de pini (de ex. SOT-23 -CW);
        # potrivirea geometrică ar ignora numerele și ar da o rotație greșită
        return None, "nepotrivit", None, None, "aceleași numere, alte poziții (altă configurație de pini?)"
    unice = {n for n in ck if n and ck[n] == 1 and cj.get(n) == 1}
    bune = []
    for cor in (0, 90, 180, 270):
        t = translatie(kicad, jlc, cor)
        k = aseaza(kicad, cor, t)
        ok, rat = se_suprapun(k, jlc, False)
        if ok:
            acord = {p[0] for p in k if p[0] in unice
                     and any(q[0] == p[0] and acopera(p, q) for q in jlc)}
            bune.append((len(acord), cor, rat, t, acord))
    if not bune:
        return None, "nepotrivit", None, None, "nicio rotație nu pune padurile pe paduri"
    # între rotațiile care trec geometric, câștigă cea la care cad cele mai multe
    # numere pe ele însele (numerele unice în ambele amprente)
    maxim = max(b[0] for b in bune)
    bune = [b for b in bune if b[0] == maxim]
    _, cor, rat, t, acord = bune[0]
    note = []
    if len(bune) > 1:
        note.append("simetric: trec " + "/".join(str(b[1]) for b in bune))
    if unice:
        note.append(f"numere pe ele însele: {len(acord)} din {len(unice)}")
        if acord != unice:
            note.append("altfel numerotate: " + ",".join(sorted(unice - acord, key=lambda n: (len(n), n))))
    return cor, "geometric", rat, t, "; ".join(note)


def rezolva_piesa(fp, cod, cache=None):
    """Întoarce (pachet, corecție, metodă, ratare, originea EasyEDA în cadrul KiCad, notă)."""
    pachet, jlc, pini, plus = citeste_easyeda(cod, cache)
    amprenta = fp.GetFPIDAsString().split(":")[-1]
    jlc, gasit, nota_catod = catod_intai(amprenta, jlc, pini, plus)
    if not gasit:
        return pachet, None, "polaritate", None, None, "nu se știe catodul: " + nota_catod
    cor, metoda, rat, t, nota = rezolva(paduri_kicad(fp), jlc)
    if cor is None:
        return pachet, None, metoda, None, None, nota
    # originea EasyEDA, adusă în cadrul amprentei KiCad: R(-cor) * (0 - t)
    o = roteste((-t[0], -t[1]), -cor)
    o = (0.0, 0.0) if math.hypot(*o) < PRAG_ORIGINE else (round(o[0], 3) + 0.0, round(o[1], 3) + 0.0)
    return pachet, cor, metoda, rat, o, "; ".join(x for x in (nota_catod, nota) if x)


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
        try:
            pachet, cor, metoda, rat, o, nota = rezolva_piesa(fp, cod)
        except (OSError, ValueError, KeyError) as e:
            print(f"EȘEC {ref} {cod}: {e}")
            probleme += 1
            continue
        if cor is None:
            print(f"EȘEC {ref} {cod} {pachet}: {nota}")
            probleme += 1
            continue
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
