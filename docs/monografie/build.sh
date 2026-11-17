#!/usr/bin/env bash
# Construiește monografia: docs/monografie/build/monografie.pdf
#
# Ce îi trebuie: LuaLaTeX (TeX Live), mpost, python3, git, curl.
# Ce aduce singur, în .dep/ (neurmărit în git):
#   - fiziko (GPL-3.0), la un commit fix;
#   - pachetele TeX Live care lipsesc dintr-o instalare minimă (luaotfload,
#     lualibs, lm, lm-math, lualatex-math,
#     metapost), din arhiva CTAN; doar dacă nu le găsește.
# Paginile din Planșa II se desenează cu simulatorul, dacă sim/build/sim,
# corpus/out/pg64597.ibv și fonturile construite există; altfel planșa are un
# loc gol.
set -euo pipefail

AICI=$(cd "$(dirname "$0")" && pwd)
RAD=$(cd "$AICI/../.." && pwd)
DEP=$AICI/.dep
OUT=$AICI/build
FIZIKO_URL=https://github.com/jemmybutton/fiziko
FIZIKO_COMMIT=54a63dba8e6700a5e70d3508838edebcbf0f45fe
TLNET=https://mirrors.ctan.org/systems/texlive/tlnet/archive

mkdir -p "$DEP" "$OUT/date" "$OUT/pagini"

# ---- fiziko
if [ ! -f "$DEP/fiziko/fiziko.mp" ]; then
  echo "fiziko: se aduce $FIZIKO_COMMIT"
  rm -rf "$DEP/fiziko"
  git clone -q "$FIZIKO_URL" "$DEP/fiziko"
  git -C "$DEP/fiziko" -c advice.detachedHead=false checkout -q "$FIZIKO_COMMIT"
fi

# ---- pachetele TeX care pot lipsi: se pun într-un arbore local, folosit ca TEXMFHOME
tl() {
  local p=$1 d=$DEP/tl/$1
  if [ ! -d "$d" ]; then
    echo "TeX Live: se aduce $p"
    mkdir -p "$d"
    curl -sSfL "$TLNET/$p.tar.xz" | tar -xJ -C "$d"
  fi
  if [ -d "$d/texmf-dist" ]; then d=$d/texmf-dist; fi
  mkdir -p "$DEP/texmf"
  for sub in fonts tex metapost scripts; do
    if [ -d "$d/$sub" ]; then cp -r "$d/$sub" "$DEP/texmf/"; fi
  done
}
local_tl=0
if ! kpsewhich luaotfload-main.lua >/dev/null; then tl luaotfload; tl lualibs; local_tl=1; fi
if ! kpsewhich lmroman10-regular.otf >/dev/null; then tl lm; local_tl=1; fi
if ! kpsewhich latinmodern-math.otf >/dev/null; then tl lm-math; local_tl=1; fi
if ! kpsewhich lualatex-math.sty >/dev/null; then tl lualatex-math; local_tl=1; fi
if ! kpsewhich -progname=mpost plain.mp >/dev/null; then tl metapost; local_tl=1; fi
if [ "$local_tl" = 1 ]; then export TEXMFHOME=$DEP/texmf; fi

# ---- Libron, fontul cititorului
if [ ! -f "$RAD/tools/fonttool/fonts/libron/Libron_R-Regular.ttf" ]; then
  "$RAD/tools/fonttool/fetch-fonts.sh"
fi

# ---- planșele
export MPINPUTS=.:$DEP/fiziko:
for mp in "$AICI"/planse/*.mp; do
  (cd "$OUT" && mpost -interaction=nonstopmode -halt-on-error -s 'outputtemplate="%j.mps"' "$mp" >/dev/null) \
    || { echo "mpost a eșuat la $mp; vezi $OUT/$(basename "${mp%.mp}").log" >&2; exit 1; }
done

# ---- datele figurilor, direct din măsurători
python3 "$AICI/date.py" "$OUT/date"

# ---- paginile din simulator (Planșa II): Caragiale, Nuvele, pagina 4
SIM=$RAD/sim/build/sim
CARTE=$RAD/corpus/out/pg64597.ibv
FONTURI=$RAD/tools/fonttool/out/fam
if [ -x "$SIM" ] && [ -f "$CARTE" ] && [ -d "$FONTURI" ]; then
  for v in kp lacom; do
    rm -rf "$OUT/pagini/$v"; mkdir -p "$OUT/pagini/$v"
    opt=(); if [ "$v" = lacom ]; then opt=(--greedy); fi
    "$SIM" pages "$CARTE" --fonts "$FONTURI" "${opt[@]}" -o "$OUT/pagini/$v" -n 4 >/dev/null
    cp "$OUT/pagini/$v/pg64597-0004.png" "$OUT/pagini/$v-4.png"
  done
else
  echo "simulatorul, cartea sau fonturile lipsesc: Planșa II rămâne goală (vezi README)"
  rm -f "$OUT/pagini/"*-4.png
fi

# ---- versiunea și calea depozitului, pentru text
ver=$(git -C "$RAD" rev-parse --short HEAD)
if [ -n "$(git -C "$RAD" status --porcelain --untracked-files=no -- . ':!docs/monografie/build' ':!docs/monografie/.dep')" ]; then
  ver="$ver, cu modificări necomise"
fi
printf '\\def\\RAD{%s}\n\\def\\versiune{commitul %s}\n' "$RAD" "$ver" > "$OUT/versiune.tex"

# ---- LuaLaTeX, de două ori (cuprinsul și trimiterile)
cd "$OUT"
for i in 1 2; do
  TEXINPUTS=.:$AICI: lualatex -interaction=nonstopmode -halt-on-error "$AICI/monografie.tex" >/dev/null \
    || { echo "lualatex a eșuat; vezi $OUT/monografie.log" >&2; grep -A3 '^!' monografie.log | head -20 >&2; exit 1; }
done
echo "$OUT/monografie.pdf"
