#!/usr/bin/env bash
# Construiește lucrarea: thesis/build/teza.pdf
# Are nevoie de LuaLaTeX și BibTeX (TeX Live). Iese cu eroare dacă o citare sau
# o trimitere rămâne nedefinită.
set -euo pipefail
AICI=$(cd "$(dirname "$0")" && pwd)
OUT=$AICI/build
mkdir -p "$OUT/capitole"
cd "$AICI"
latex() { lualatex -interaction=nonstopmode -halt-on-error -output-directory="$OUT" teza.tex >/dev/null; }
latex
(cd "$OUT" && BIBINPUTS=$AICI: bibtex teza >/dev/null)
latex
latex
if grep -E "Citation .* undefined|Reference .* undefined|There were undefined" "$OUT/teza.log"; then
  echo "eroare: citări sau trimiteri nedefinite" >&2
  exit 1
fi
if grep -q "^Warning--" "$OUT/teza.blg"; then grep "^Warning--" "$OUT/teza.blg" >&2; fi
echo "$OUT/teza.pdf"
