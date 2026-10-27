#!/bin/sh
# Un card SD pentru `sim ui`: cărțile corpusului în books/ și fonturile
# dispozitivului în fonts/, ca legături simbolice, plus un /.ibv/ gol (indexuri,
# poziții, semne de carte, lista recentelor).
#
#   sim/make_card.sh [CARD]      implicit sim/out/card; se reface de la zero
set -e
here=$(cd "$(dirname "$0")" && pwd)
card=${1:-$here/out/card}
fam=$here/../tools/fonttool/out/fam
books=$here/../corpus/out
[ -f "$fam/libron-17-r.ibf" ] || { echo "lipsesc fonturile: rulează întâi tools/fonttool" >&2; exit 1; }
ls "$books"/*.ibv >/dev/null 2>&1 || { echo "lipsește corpusul: corpus/fetch.sh și convertorul" >&2; exit 1; }
rm -rf "$card"
mkdir -p "$card/books"
ln -s "$(cd "$fam" && pwd)" "$card/fonts"
for b in "$books"/*.ibv; do ln -s "$(cd "$(dirname "$b")" && pwd)/$(basename "$b")" "$card/books/"; done
echo "$card: $(ls "$card/books" | wc -l) cărți, fonturile din $fam"
