#!/usr/bin/env python3
"""Compară cadrele emulatorului cu ale simulatorului, pixel cu pixel.

    emu/compare.py DOSAR_EMULATOR DOSAR_SIMULATOR [--diff DOSAR]

Emulatorul scrie ce a ajuns pe panou (NNN.pbm), simulatorul ce a desenat
interfața (NNN.png). Cadrele se potrivesc după număr. Cu --diff, pentru fiecare
cadru diferit se scrie o imagine: emulatorul, simulatorul și pixelii diferiți.

Iese cu 0 dacă toate cadrele sunt identice și jurnalul emulatorului n-are
avertismente, altfel cu 1.
"""
import argparse
import os
import sys

from PIL import Image, ImageChops


def frames(d, ext):
    return {f[:-len(ext)]: os.path.join(d, f) for f in os.listdir(d) if f.endswith(ext) and f[:-len(ext)].isdigit()}


def main():
    ap = argparse.ArgumentParser(description="Compară cadrele emulatorului cu ale simulatorului.")
    ap.add_argument("emu")
    ap.add_argument("sim")
    ap.add_argument("--diff", help="dosar pentru imaginile cu diferențe")
    a = ap.parse_args()

    emu, sim = frames(a.emu, ".pbm"), frames(a.sim, ".png")
    bad = 0
    for n in sorted(set(emu) | set(sim)):
        if n not in emu or n not in sim:
            print("%s  lipsește din %s" % (n, "emulator" if n not in emu else "simulator"))
            bad += 1
            continue
        e, s = Image.open(emu[n]).convert("L"), Image.open(sim[n]).convert("L")
        if e.size != s.size:
            print("%s  mărimi diferite: %s și %s" % (n, e.size, s.size))
            bad += 1
            continue
        d = ImageChops.difference(e, s)
        box = d.getbbox()
        if not box:
            continue
        bad += 1
        count = e.size[0] * e.size[1] - d.histogram()[0]
        print("%s  %d pixeli diferiți, în zona %d,%d %dx%d" % (n, count, box[0], box[1], box[2] - box[0], box[3] - box[1]))
        if a.diff:
            os.makedirs(a.diff, exist_ok=True)
            w, h = e.size
            sheet = Image.new("L", (3 * w + 20, h), 128)
            sheet.paste(e, (0, 0))
            sheet.paste(s, (w + 10, 0))
            sheet.paste(ImageChops.invert(d), (2 * w + 20, 0))
            sheet.save(os.path.join(a.diff, n + ".png"))

    warnings = 0
    journal = os.path.join(a.emu, "jurnal.txt")
    if os.path.exists(journal):
        with open(journal, encoding="utf-8") as f:
            warnings = sum("ATENȚIE" in line for line in f)
    total = len(set(emu) | set(sim))
    print("%d cadre, %d identice, %d avertismente în jurnalul emulatorului" % (total, total - bad, warnings))
    sys.exit(1 if bad or warnings else 0)


if __name__ == "__main__":
    main()
