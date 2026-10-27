#!/bin/sh
# Golden-image test for the layout (LAY-6): renders layout.ibv (every record
# kind and special character, see make_fixture.py) with the built-in font in
# two configurations and compares each page with golden/. Then, if the corpus
# has been converted, paginates every book and fails on layout errors.
#
#   sim/test/run.sh            test
#   sim/test/run.sh --update   accept the current output as golden (look at it first)
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
        echo "golden/$cfg: $(ls "$out/$cfg" | wc -l) pages updated"
        continue
    fi
    if ! diff -r -q "$here/golden/$cfg" "$out/$cfg" >"$out/diff.txt"; then
        echo "FAIL $cfg:"
        sed "s|$out/||; s|$here/||" "$out/diff.txt"
        echo "  new pages are in $out/$cfg (kept)"
        trap - EXIT
        fail=1
    else
        echo "ok   $cfg: $(ls "$out/$cfg" | wc -l) pages match"
    fi
done

books=$(ls "$here"/../../corpus/out/*.ibv 2>/dev/null || true)
if [ -n "$books" ] && [ "$1" != --update ]; then
    # shellcheck disable=SC2086
    if "$sim" stats $books >"$out/stats.txt" 2>&1; then
        echo "ok   corpus: $(grep -c '\.ibv' "$out/stats.txt") books paginated"
    else
        echo "FAIL corpus:"; grep -E "FAILED|:" "$out/stats.txt"; fail=1
    fi
fi
exit $fail
