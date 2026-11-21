#!/usr/bin/env python3
"""Verifică jlc_rotatii.py pe placa de test a proiectului kicad-jlcpcb-tools.

Placa `tests/fixtures/jlcfootprint/boards/corner_case` are 47 de piese, iar
`truth.csv` are, pentru fiecare, rotația la care a ajuns previzualizarea JLCPCB
(sau un câmp gol pentru piesele care nu trebuie să primească nicio rotație).
Răspunsurile EasyEDA sunt înregistrate lângă ea. Din depozitul acela se citesc
doar date (placa, CSV-ul, JSON-urile), nu se rulează cod.

    git clone --depth 1 https://github.com/Bouni/kicad-jlcpcb-tools /tmp/kjt
    python3 hardware/pcb/test_jlc_rotatii.py /tmp/kjt

Trei verdicte: corect, refuzat (scriptul nu dă nicio rotație, deci make.sh se
oprește: sigur, dar de lucrat de mână) și GREȘIT (o rotație dată, alta decât a
JLCPCB). Iese cu cod 1 la orice GREȘIT.
"""
import csv
import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jlc_rotatii as jr  # noqa: E402

# D5 are anodul legat la pastila 1 intenționat; rotația ei din truth.csv vine din
# rețele, pe care previzualizarea nu le vede (nota din truth.csv).
PRIN_CONSTRUCTIE = {"D5"}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fx = Path(sys.argv[1]) / "tests" / "fixtures" / "jlcfootprint"
    placa = pcbnew.LoadBoard(str(fx / "boards" / "corner_case" / "corner_case.kicad_pcb"))
    truth = {r["reference"]: r for r in csv.DictReader(open(fx / "truth.csv", encoding="utf-8"))}
    numar = {"corect": 0, "refuzat": 0, "GREȘIT": 0}
    for fp in sorted(placa.GetFootprints(), key=lambda f: f.GetReference()):
        ref = fp.GetReference()
        t = truth.get(ref)
        cod = next((f.GetText().strip().upper() for f in fp.GetFields()
                    if f.GetName().upper().startswith(("LCSC", "JLC"))), "")
        if t is None or not cod or ref in PRIN_CONSTRUCTIE:
            continue
        try:
            pachet, cor, metoda, _, _, nota = jr.rezolva_piesa(fp, cod, fx / "easyeda")
        except (OSError, ValueError, KeyError) as e:
            pachet, cor, metoda, nota = "", None, "eroare", str(e)
        asezat = fp.GetOrientationDegrees() % 360
        if fp.IsFlipped():
            asezat = (180 - asezat) % 360
        jlc = t["observed_rotation"]
        if cor is None:
            v = "corect" if jlc == "" else "refuzat"
        elif jlc == "":
            v = "GREȘIT"
        else:
            # piesele nepolarizate cu două paduri sunt la fel la 0° și 180°
            mod = 180 if "unpolarized" in t["note"] else 360
            d = abs((asezat + cor) - float(jlc)) % mod
            v = "corect" if min(d, mod - d) < 1e-3 else "GREȘIT"
        numar[v] += 1
        if v != "corect":
            dat = "-" if cor is None else f"{(asezat + cor) % 360:g}"
            print(f"{v:8} {ref:5} {cod:9} {pachet[:40]:40} CPL {dat:>4}, JLC {jlc or '-':>4}  {metoda} {nota}")
    print(", ".join(f"{v} {n}" for v, n in numar.items()))
    sys.exit(1 if numar["GREȘIT"] else 0)


if __name__ == "__main__":
    main()
