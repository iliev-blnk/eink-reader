#!/bin/sh
# Compilează, rulează intrările din regresii/ (bugurile găsite deja), apoi
# fiecare fuzzer câte FUZZ_TIME secunde (implicit 60) pornind de la semințe și de
# la corpusul strâns în rulările anterioare (build/corpus). Se oprește la prima
# problemă; intrarea care a produs-o ajunge în build/crash/.
#   tools/fuzz/run.sh             # toate trei
#   FUZZ_TIME=1800 tools/fuzz/run.sh ibv
set -eu
cd "$(dirname "$0")"
./build.sh > /dev/null
FUZZ_BOOK=${FUZZ_BOOK:-$PWD/../../sim/test/layout.ibv}
export FUZZ_BOOK
for t in ibv font state; do
    for f in regresii/$t/*; do
        ./build/fuzz_$t "$f" > build/regresie.log 2>&1 || { cat build/regresie.log; echo "REGRESIE: $f"; exit 1; }
    done
done
echo "regresii: ok"
mkdir -p build/crash
for t in ${*:-ibv font state}; do
    case $t in ibv) max=8192 ;; font) max=40000 ;; *) max=4096 ;; esac
    mkdir -p build/corpus/$t
    ./build/fuzz_$t build/corpus/$t build/seeds/$t -max_len=$max -max_total_time="${FUZZ_TIME:-60}" -timeout=10 \
        -rss_limit_mb=2048 -artifact_prefix=build/crash/$t- -print_final_stats=1 > build/$t.log 2>&1 \
        || { grep -E "ERROR|runtime error|SUMMARY|Test unit" build/$t.log; exit 1; }
    echo "$t: $(grep -E '^#[0-9]+.*DONE' build/$t.log | sed 's/ *exec\/s.*//')"
done
