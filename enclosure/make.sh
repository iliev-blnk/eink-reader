#!/usr/bin/env bash
# Carcasa, de la capăt: geometria plăcii din KiCad (placa.scad), piesele de printat
# în stl/ și imaginile din README în img/.
#
#   enclosure/make.sh                # tot (~1 min)
#   enclosure/make.sh --fara-kicad   # fără placa.py: folosește placa.scad din git
#
# Se oprește dacă vreo piesă nu iese un singur corp închis sau dacă o verificare
# din carcasa.scad pică. Avertismentele („ATENȚIE”) se afișează la sfârșit.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p stl img build

if command -v openscad >/dev/null; then
    OS=(openscad)
elif flatpak info --user org.openscad.OpenSCAD >/dev/null 2>&1; then
    # imaginile au nevoie de un ecran X (pe Wayland, Xwayland)
    OS=(flatpak run --user --socket=x11 --env=DISPLAY="${DISPLAY:-:0}" --filesystem="$PWD" org.openscad.OpenSCAD)
else
    echo "lipsește OpenSCAD (pachet sau flatpak org.openscad.OpenSCAD)" >&2
    exit 1
fi
if command -v systemd-run >/dev/null && systemd-run --user --scope -q true 2>/dev/null; then
    OS=(systemd-run --user --scope -q -p MemoryMax=3G "${OS[@]}")
fi

[[ "${1:-}" == --fara-kicad ]] || python3 placa.py

scad() {  # scad <ieșire> <piesa> [opțiuni]: rulează și păstrează jurnalul
    local out=$1 piesa=$2; shift 2
    "${OS[@]}" -o "$out" -D "piesa=\"$piesa\"" "$@" carcasa.scad >"build/$piesa.log" 2>&1 || {
        cat "build/$piesa.log" >&2; echo "== $piesa: OpenSCAD a eșuat" >&2; exit 1; }
    if grep -q "^ERROR\|assert" "build/$piesa.log"; then
        cat "build/$piesa.log" >&2; echo "== $piesa: verificare picată" >&2; exit 1
    fi
}

echo "== piesele de printat"
for p in fata spate buton distantier; do
    scad "stl/$p.stl" "$p"
    if ! grep -q "Simple: *yes" "build/$p.log" || ! grep -q "Volumes: *2$" "build/$p.log"; then
        cat "build/$p.log" >&2; echo "== $p: nu e un singur corp închis" >&2; exit 1
    fi
    printf '   %-11s %s\n' "$p" "$(grep -o 'Facets: *[0-9]*' "build/$p.log")"
done

echo "== imaginile"
scad img/ansamblu.png ansamblu --imgsize=1400,900 --camera=52,27,12,58,0,-35,240 --colorscheme=Tomorrow
scad img/explodat.png explodat --imgsize=1400,1100 --camera=52,27,8,62,0,-35,300 --colorscheme=Tomorrow
scad img/spate.png ansamblu --imgsize=1400,900 --camera=52,27,12,125,0,200,240 --colorscheme=Tomorrow
scad img/sectiune.png sectiune --imgsize=1600,520 --camera=53,12,0,0,0,0,128 --projection=o --colorscheme=Tomorrow

echo "== cotele"
sed -n 's/^ECHO: "\(.*\)"$/   \1/p' build/fata.log
if grep -h "ATENȚIE" build/*.log | sort -u | grep .; then
    echo "== sunt avertismente (mai sus)"
fi
