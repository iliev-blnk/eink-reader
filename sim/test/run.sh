#!/bin/sh
# Testul cu imagini de referință pentru paginare (LAY-6): desenează layout.ibv
# (fiecare tip de înregistrare și caracter special, vezi make_fixture.py) cu
# fontul încorporat în două configurații și compară fiecare pagină cu golden/.
# Apoi interfața (`sim ui`): un scenariu de apăsări pe aceeași carte, comparat
# cadru cu cadru și cu jurnalul reîmprospătărilor, și întoarcerea automată,
# cu jurnalul de măsurători citit de tools/perf/perf.py. Apoi, dacă s-a convertit
# corpusul, paginează fiecare carte și eșuează la erori de paginare, o dată cu
# fontul încorporat și o dată cu fonturile dispozitivului, dacă le-a construit
# fonttool.
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

# Un scenariu `sim ui` pe layout.ibv, comparat cadru cu cadru și cu jurnalul
# reîmprospătărilor; cardul rămâne în $out/NAME-card.
ui_case() {
    name=$1 keys=$2
    mkdir -p "$out/$name-card/perf" "$out/$name"
    cp "$here/layout.ibv" "$out/$name-card/"
    "$sim" ui "$out/$name-card" -o "$out/$name" --idle --keys "$keys" >/dev/null
    if [ "$update" = 1 ]; then
        rm -rf "$here/golden/$name"
        cp -r "$out/$name" "$here/golden/$name"
        echo "golden/$name: $(ls "$out/$name" | wc -l) fișiere actualizate"
    elif ! diff -r -q "$here/golden/$name" "$out/$name" >"$out/diff.txt"; then
        echo "EȘUAT $name:"
        sed "s|$out/||; s|$here/||" "$out/diff.txt"
        echo "  cadrele noi sunt în $out/$name (păstrate)"
        trap - EXIT
        fail=1
    else
        echo "ok    $name: $(grep -c '^[0-9]' "$out/$name/jurnal.txt") reîmprospătări identice"
    fi
}
update=0
[ "$1" = --update ] && update=1

# Biblioteca, cartea, pagini, un semn de carte, somnul cu coperta, trezirea la
# aceeași pagină, lista semnelor și saltul la semn, somnul din nou.
ui_case ui "m m n n m n n m M n m n n n m m p M n"

# Conectarea la PC (FW-12), cu discul fals al simulatorului: fără somn cât
# cardul e la calculator, confirmarea la ieșire, scoaterea în siguranță care
# închide ecranul singură, apoi un card care nu pornește.
ui_case usb "n m u:h t:900 M n M u:p u:h u:r u:x m m"

# Întoarcerea automată la 5 s (FW-13): un minut de pagini, de la capăt din nou
# după ultima, o apăsare care o oprește, o pagină obișnuită, apoi bateria
# descărcată. Jurnalul de măsurători al acestei rulări trebuie să-l citească
# tools/perf/perf.py: 12 întoarceri automate și una de la buton.
ui_case auto "m m m n n n n n n m P t:60 n n b:3350 t:200"
if [ "$update" = 0 ]; then
    if python3 "$here/../../tools/perf/perf.py" "$out/auto-card"/perf/*.csv >"$out/perf.txt" 2>&1 \
        && grep -q "toate: 13 (auto 12, ÎNAINTE 1)" "$out/perf.txt" \
        && grep -q "oprit, baterie descărcată" "$out/perf.txt"; then
        echo "ok    perf: jurnalul simulatorului citit de perf.py"
    else
        echo "EȘUAT perf:"; sed 's/^/  /' "$out/perf.txt"; fail=1
    fi
fi

# Netezirea procentului bateriei (FW-8): media exponențială cu ponderea 1/4,
# luată de la capăt după un salt de peste 200 mV. Jurnalul de măsurători are
# citirea și tensiunea afișată; valorile de mai jos sunt calculate de mână.
ui_case baterie "b:3880 t:60 b:3860 t:60 b:3870 t:60 b:3500 t:60 b:3480 t:60 n"
if [ "$update" = 0 ]; then
    got=$(grep -h ',battery,' "$out/baterie-card"/perf/*.csv | cut -d, -f3,4 | tr '\n' ' ')
    if [ "$got" = "3880,3895 3860,3886 3870,3882 3500,3500 3480,3495 " ]; then
        echo "ok    baterie: media netezită ca la calcul"
    else
        echo "EȘUAT baterie: $got"; fail=1
    fi
fi

# Indexurile paginărilor vechi (FW-11, spec/book-format.md, secțiunea 6): de
# două ori spațierea, de două ori marginile, de două ori spațierea, din meniul
# Setări. Cu paginarea de la deschidere sunt 7, una peste LAYOUTS_MAX = 6:
# prima (cea implicită) iese din listă și indexul ei se șterge de pe card.
sp="m n n n n m n m P P" mg="m n n n n m n n m P P"
ui_case pg "m m $sp $sp $mg $mg $sp $sp"
if [ "$update" = 0 ]; then
    pgs=$(ls "$out/pg-card/.ibv" | grep -c '\.pg$' || true)
    if [ "$pgs" = 6 ] && [ ! -e "$out/pg-card/.ibv/9c64b95eb82dac65-adf3a378.pg" ] \
        && grep -q ',pg_prune,1,' "$out/pg-card"/perf/*.csv; then
        echo "ok    pg: 6 indexuri rămase pe card, cel mai vechi șters"
    else
        echo "EȘUAT pg: $pgs indexuri"; ls "$out/pg-card/.ibv" | sed 's/^/  /'; fail=1
    fi
fi

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
