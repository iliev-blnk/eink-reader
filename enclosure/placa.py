#!/usr/bin/env python3
"""Scoate din placa KiCad ce-i trebuie carcasei și scrie `placa.scad`.

    python3 enclosure/placa.py

Citește `hardware/pcb/eink-reader-pcb.kicad_pcb` prin API-ul pcbnew (KiCad 10):
conturul plăcii, găurile de prindere și, pentru piesele de pe margini, poziția
și dreptunghiul de curte (courtyard). Carcasa (`carcasa.scad`) folosește doar
fișierul generat, deci se poate deschide și fără KiCad.

Coordonatele din `placa.scad` sunt ale carcasei: X ca în KiCad, Y în sus
(Y = H − y), originea în colțul stânga-jos al plăcii, privită din față. Piesele
sunt pe fața F, care e spre ecran, deci vederea din față a carcasei e vederea F
din KiCad.
"""
import os
import sys

import pcbnew
from pcbnew import ToMM

AICI = os.path.dirname(os.path.abspath(__file__))
PLACA = os.path.join(AICI, '..', 'hardware', 'pcb', 'eink-reader-pcb.kicad_pcb')
IESIRE = os.path.join(AICI, 'placa.scad')

# Piesele de care se lovește carcasa: cele de pe margini și cele înalte.
PIESE = {
    'J1': 'USB-C (GCT USB4105), marginea de jos',
    'J2': 'bateria (JST PH vertical)',
    'J3': 'bareta spre placa WeAct',
    'J4': 'slotul microSD (Molex 104031), marginea de sus',
    'J5': 'UART, baretă 1×3 verticală',
    'SW1': 'comutatorul pornit/oprit (PCM12), marginea de jos',
    'SW2': 'buton lateral ÎNAPOI, marginea din stânga',
    'SW3': 'buton lateral ÎNAINTE, marginea din stânga',
    'SW4': 'buton lateral MENIU, marginea de sus',
    'U7': 'ESP32-S3-WROOM-1, antena spre dreapta',
}


def contur(b):
    """Dreptunghiul de pe Edge.Cuts, fără grosimea liniei."""
    xs, ys = [], []
    for d in b.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts:
            continue
        for p in (d.GetStart(), d.GetEnd()):
            xs.append(ToMM(p.x))
            ys.append(ToMM(p.y))
    if not xs:
        sys.exit('placa nu are contur pe Edge.Cuts')
    return min(xs), min(ys), max(xs), max(ys)


def main():
    b = pcbnew.LoadBoard(PLACA)
    x0, y0, x1, y1 = contur(b)
    W, H = x1 - x0, y1 - y0

    def X(x):
        return round(ToMM(x) - x0, 3)

    def Y(y):
        return round(H - (ToMM(y) - y0), 3)

    gauri, piese = [], {}
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith('H'):
            pad = next(iter(fp.Pads()))
            gauri.append((ref, X(fp.GetPosition().x), Y(fp.GetPosition().y),
                          round(ToMM(pad.GetDrillSizeX()), 3)))
        elif ref in PIESE:
            if fp.GetLayer() != pcbnew.F_Cu:
                sys.exit(f'{ref} nu mai e pe fața F; carcasa presupune toate piesele spre ecran')
            c = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
            piese[ref] = (X(fp.GetPosition().x), Y(fp.GetPosition().y),
                          X(c.GetLeft()), Y(c.GetBottom()), X(c.GetRight()), Y(c.GetTop()))
    lipsa = sorted(set(PIESE) - set(piese))
    if lipsa or len(gauri) != 4:
        sys.exit(f'lipsesc de pe placă: {lipsa}, găuri: {len(gauri)}')
    gauri.sort()

    out = [
        '// Generat de placa.py din hardware/pcb/eink-reader-pcb.kicad_pcb. Nu se editează de mână.',
        '// Coordonate de carcasă: X la dreapta, Y în sus, originea în colțul stânga-jos al plăcii,',
        '// privită din față (fața F, cu piesele, e spre ecran).',
        '',
        f'pcb_w = {W:.3f};',
        f'pcb_h = {H:.3f};',
        '',
        '// găurile de prindere (aceleași ca pe placa WeAct): [X, Y, diametru]',
        'gauri = [',
    ]
    out += [f'    [{x}, {y}, {d}],  // {r}' for r, x, y, d in gauri]
    out += [
        '];',
        '',
        '// piesele: [X, Y, x0, y0, x1, y1]: centrul și dreptunghiul de curte (courtyard)',
    ]
    for ref in sorted(piese):
        v = ', '.join(str(n) for n in piese[ref])
        out.append(f'p_{ref} = [{v}];  // {PIESE[ref]}')
    with open(IESIRE, 'w') as f:
        f.write('\n'.join(out) + '\n')
    print(f'placa {W:.2f} × {H:.2f} mm, {len(gauri)} găuri, {len(piese)} piese → {os.path.relpath(IESIRE)}')


if __name__ == '__main__':
    main()
