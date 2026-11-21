#!/usr/bin/env bash
# Placa, de la capăt: schema, așezarea și trasarea, verificările, fișierele pentru
# fabrică (JLCPCB) în fab/ și imaginile din README.
#
#   hardware/pcb/make.sh            # tot (~2 min)
#   hardware/pcb/make.sh --fara-trasare   # doar verificările și fișierele
#
# Se oprește la prima eroare: ERC sau DRC cu vreo eroare, vreo legătură lipsă sau
# vreo diferență între schemă și placă.
set -euo pipefail
cd "$(dirname "$0")"
P="$PWD/eink-reader-pcb"   # absolută: cu o cale relativă, paritatea afișează un assert
mkdir -p build fab

if [[ "${1:-}" != --fara-trasare ]]; then
    python3 gen_sch.py
    # trasarea ține câteva sute de MB; pe mașina de lucru (15 GB) merge sub o limită
    if command -v systemd-run >/dev/null && systemd-run --user --scope -q true 2>/dev/null; then
        systemd-run --user --scope -q -p MemoryMax=4G python3 gen_pcb.py
    else
        python3 gen_pcb.py
    fi
fi

echo "== ERC"
kicad-cli sch erc --severity-all --format json -o build/erc.json $P.kicad_sch >/dev/null
echo "== DRC (cu paritatea schemă–placă)"
kicad-cli pcb drc --severity-all --schematic-parity --format json -o build/drc.json $P.kicad_pcb >/dev/null
python3 - <<'EOF'
import json, sys
erc = json.load(open('build/erc.json'))
drc = json.load(open('build/drc.json'))
e = [v for s in erc['sheets'] for v in s['violations']]
errs = [v for v in drc['violations'] if v['severity'] == 'error']
warns = [v for v in drc['violations'] if v['severity'] != 'error']
unc, par = drc['unconnected_items'], drc['schematic_parity']
print(f'ERC: {len(e)}; DRC: {len(errs)} erori, {len(warns)} avertismente, '
      f'{len(unc)} nelegate, {len(par)} diferențe față de schemă')
for v in warns:
    print('   avertisment:', v['type'], '-', v['items'][0]['description'])
bad = e + errs + unc + par
for v in bad[:20]:
    print('   ', v['type'], '-', v['description'])
sys.exit(1 if bad else 0)
EOF

echo "== verificările de proiectare (REVIEW.md)"
python3 verifica.py

echo "== fab/"
rm -rf fab/gerber && mkdir -p fab/gerber
kicad-cli pcb export gerbers -o fab/gerber/ --subtract-soldermask --check-zones --use-drill-file-origin \
    --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
    $P.kicad_pcb >/dev/null
kicad-cli pcb export drill -o fab/gerber/ --format excellon --excellon-units mm \
    --excellon-separate-th --drill-origin plot --generate-map --map-format gerberx2 $P.kicad_pcb >/dev/null
(cd fab/gerber && rm -f ../gerber.zip && zip -q ../gerber.zip ./*)
kicad-cli pcb export pos -o build/pos.csv --format csv --units mm --side both --exclude-dnp \
    --use-drill-file-origin $P.kicad_pcb >/dev/null
kicad-cli sch export bom -o build/bom.csv --exclude-dnp \
    --fields 'Value,Reference,Footprint,LCSC,MPN,${QUANTITY}' --group-by 'Value,Footprint,LCSC' \
    $P.kicad_sch >/dev/null
# formatul JLCPCB: CPL (Designator, Mid X, Mid Y, Layer, Rotation) și BOM
# (Comment, Designator, Footprint, LCSC Part #), coordonatele față de colțul din
# stânga jos al plăcii. Piesele THT și cele fără cod LCSC rămân în BOM; JLCPCB le
# arată ca nepotrivite și se aleg sau se scot de mână.
# CPL-ul primește corecțiile din jlc_corectii.csv (jlc_rotatii.py): JLC așază după
# amprenta EasyEDA a codului LCSC, cu altă orientare și altă origine decât KiCad.
python3 - <<'EOF'
import csv, math, re, sys
cor = {r['ref']: r for r in csv.DictReader(open('jlc_corectii.csv', encoding='utf-8'))}
bom = list(csv.DictReader(open('build/bom.csv')))
cod = {}
for r in bom:   # „R21-R24” → R21, R22, R23, R24
    for d in r['Reference'].split(','):
        m = re.fullmatch(r'([A-Z]+)(\d+)-[A-Z]+(\d+)', d.strip())
        refs = [m[1] + str(i) for i in range(int(m[2]), int(m[3]) + 1)] if m else [d.strip()]
        for x in refs:
            cod[x] = (r['LCSC'], r['Footprint'].split(':')[-1])
vechi = [x for x, (c, fp) in cod.items() if c and (x not in cor or cor[x]['lcsc'] != c
                                                    or cor[x]['amprenta_kicad'] != fp)]
if vechi:
    sys.exit(f'jlc_corectii.csv nu corespunde plăcii pentru {", ".join(sorted(vechi))}: '
             'rulează python3 jlc_rotatii.py --descarca')
with open('build/pos.csv') as f, open('fab/cpl.csv', 'w', newline='') as o:
    w = csv.writer(o)
    w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    for r in csv.DictReader(f):
        x, y, rot = float(r['PosX']), float(r['PosY']), float(r['Rot'])
        c = cor.get(r['Ref'])
        if c:
            if r['Side'] != 'top':
                sys.exit(f"{r['Ref']}: corecțiile acoperă doar fața de sus")
            # originea EasyEDA din cadrul amprentei (Y în jos) pe placă; pos.csv are Y în sus
            a = math.radians(-rot)
            ox, oy = float(c['origine_x_mm']), float(c['origine_y_mm'])
            x += ox * math.cos(a) - oy * math.sin(a)
            y -= ox * math.sin(a) + oy * math.cos(a)
            rot += int(c['rotatie'])
        w.writerow([r['Ref'], f'{x:.4f}mm', f'{y:.4f}mm', r['Side'].capitalize(), f'{rot % 360:g}'])
with open('fab/bom.csv', 'w', newline='') as o:
    w = csv.writer(o)
    w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #', 'MPN', 'Qty'])
    for r in bom:
        w.writerow([r['Value'], r['Reference'], r['Footprint'].split(':')[-1], r['LCSC'], r['MPN'], r['QUANTITY']])
EOF
ls fab

echo "== imagini"
mkdir -p img
for side in top bottom; do
    kicad-cli pcb render -o img/placa-$side.png --side $side --width 1600 --height 900 \
        --quality basic --background opaque --zoom 1.75 $P.kicad_pcb >/dev/null 2>&1
done
kicad-cli pcb export svg -o build/cupru.svg --mode-single --page-size-mode 2 --exclude-drawing-sheet \
    --layers F.Cu,B.Cu,F.Fab,Edge.Cuts $P.kicad_pcb >/dev/null
ls img
