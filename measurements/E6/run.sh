#!/bin/sh
# E6: rulează cele 20 de condiții (vezi METODA.md) și scrie date/.
#
#   measurements/E6/run.sh
#
# Convertește din nou cele 20 de cărți distincte din corpus/files în carti/
# (neurmărit), ca toate să vină de la același convertor. Apoi, pentru fiecare
# algoritm × despărțire × treaptă: `sim breaks` | linii.py și `sim stats`.
# La treapta implicită, `sim stats` rulează de 5 ori, pentru timp.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
sim=$root/sim/build/sim
fonts=$root/tools/fonttool/out/fam
date=$here/date
carti=$here/carti

cmake -S "$root/sim" -B "$root/sim/build" >/dev/null
cmake --build "$root/sim/build" >/dev/null
[ -d "$fonts" ] || { echo "lipsesc fonturile: tools/fonttool, vezi README-ul lui"; exit 1; }

rm -rf "$date" "$carti"
mkdir -p "$date" "$carti"
cd "$root/corpus/files"
python3 "$root/tools/converter/convert.py" convert -o "$carti" \
    libru-dubrovsky.txt pg11.epub pg1322.epub pg1342.epub pg14741.epub pg16527-8.txt \
    pg2701.epub pg35323.epub pg62916.epub pg64597.epub pg65565.epub pg84.epub \
    libru-geroi.fb2.zip pg1524.epub pg4300.epub ws-amintiri.epub ws-ion.epub ws-kapitanskaya.epub \
    ws-luceafarul.epub ws-moara-cu-noroc.epub >/dev/null
books=$(ls "$carti"/*.ibv)
[ "$(echo "$books" | wc -l)" -eq 20 ] || { echo "se așteptau 20 de cărți"; exit 1; }

{
    echo "data $(date -I)"
    echo "commit $(git -C "$root" rev-parse HEAD)$(git -C "$root" diff --quiet HEAD -- sim firmware tools || echo ' (cu modificări necomise)')"
    echo "compilator $(cc --version | head -1)"
    echo "procesor $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
    echo "# sha256 cărți"
    (cd "$carti" && sha256sum ./*.ibv)
    echo "# sha256 fonturi"
    (cd "$fonts" && sha256sum ./*.ibf)
} >"$date/versiune.txt"

echo "alg,desp,treapta,rulare,carte,pagini,ms,us_pag,hash" >"$date/timp.csv"
for alg in kp greedy; do
    for desp in da nu; do
        opts=""
        [ $alg = greedy ] && opts="$opts --greedy"
        [ $desp = nu ] && opts="$opts --no-hyphen"
        for step in 0 1 2 3 4; do
            cond="$alg-$desp-$step"
            # shellcheck disable=SC2086
            "$sim" breaks $books --fonts "$fonts" --size $step $opts | python3 "$here/linii.py" $alg $desp $step
            runs=1
            [ $step = 2 ] && runs=5
            for run in $(seq 1 $runs); do
                # shellcheck disable=SC2086
                "$sim" stats $books --fonts "$fonts" --size $step $opts >"$date/stats.tmp"
                [ "$run" = 1 ] && cp "$date/stats.tmp" "$date/stats-$cond.txt"
                awk -v c="$alg,$desp,$step,$run" '$1 ~ /\.ibv$/ {sub(/\.ibv$/, "", $1); print c "," $1 "," $2 "," $4 "," $5 "," $18}' \
                    "$date/stats.tmp" >>"$date/timp.csv"
            done
            echo "$cond gata"
        done
    done
done
rm -f "$date/stats.tmp"
