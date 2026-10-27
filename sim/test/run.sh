#!/bin/sh
# Testul cu imagini de referință pentru paginare (LAY-6): desenează layout.ibv
# (fiecare tip de înregistrare și caracter special, vezi make_fixture.py) cu
# fontul încorporat în două configurații și compară fiecare pagină cu golden/.
# Apoi, dacă s-a convertit corpusul, paginează fiecare carte și eșuează la erori
# de paginare, o dată cu fontul încorporat și o dată cu fonturile dispozitivului,
# dacă le-a construit fonttool.
#
#   sim/test/run.sh            testul
#   sim/test/run.sh --update   acceptă ieșirea curentă ca referință (întâi uită-te la ea)
set -e
here=$(cd "$(dirname "$0")" && pwd)
sim=$here/../build/sim
[ -d "$here/../build" ] || cmake -S "$here/.." -B "$here/../build" >/dev/null
cmake --build "$here/../build" >/dev/null
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

fail=0
for cfg in default ragged; do
    opts=""
    [ $cfg = ragged ] && opts="--ragged --no-hyphen --no-chapter-page --para-gap 6"
    mkdir -p "$out/$cfg"
    # shellcheck disable=SC2086
    "$sim" pages "$here/layout.ibv" --cover -n 999 -o "$out/$cfg" $opts >/dev/null
    if [ "$1" = --update ]; then
        rm -rf "$here/golden/$cfg"
        mkdir -p "$here/golden"
        cp -r "$out/$cfg" "$here/golden/$cfg"
        echo "golden/$cfg: $(ls "$out/$cfg" | wc -l) pagini actualizate"
        continue
    fi
    if ! diff -r -q "$here/golden/$cfg" "$out/$cfg" >"$out/diff.txt"; then
        echo "EȘUAT $cfg:"
        sed "s|$out/||; s|$here/||" "$out/diff.txt"
        echo "  paginile noi sunt în $out/$cfg (păstrate)"
        trap - EXIT
        fail=1
    else
        echo "ok    $cfg: $(ls "$out/$cfg" | wc -l) pagini identice"
    fi
done

books=$(ls "$here"/../../corpus/out/*.ibv 2>/dev/null || true)
if [ -n "$books" ] && [ "$1" != --update ]; then
    # shellcheck disable=SC2086
    if "$sim" stats $books >"$out/stats.txt" 2>&1; then
        echo "ok    corpus: $(grep -c '\.ibv' "$out/stats.txt") cărți paginate"
    else
        echo "EȘUAT corpus:"; grep -E "EȘUAT|:" "$out/stats.txt"; fail=1
    fi
    # Fonturile dispozitivului, alese după limba fiecărei cărți (fontsel.c).
    # Fiecare caracter lăsat de convertor trebuie să fie în fontul primit de carte.
    fam=$here/../../tools/fonttool/out/fam
    if [ -f "$fam/libron-17-r.ibf" ] && [ -f "$fam/ptserif-18-r.ibf" ]; then
        # shellcheck disable=SC2086
        if "$sim" stats $books --fonts "$fam" >"$out/stats-fonts.txt" 2>&1 \
            && awk '/\.ibv/ && $17 != 0 { bad = 1; print "  " $1 ": " $17 " caractere lipsă din " $19 } END { exit bad }' \
                "$out/stats-fonts.txt"; then
            echo "ok    corpus: $(grep -c '\.ibv' "$out/stats-fonts.txt") cărți în fontul lor, nicio glifă lipsă"
        else
            echo "EȘUAT corpus în fonturile dispozitivului:"; grep -E "EȘUAT|:" "$out/stats-fonts.txt"; fail=1
        fi
    fi
fi
exit $fail
