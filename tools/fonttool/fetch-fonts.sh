#!/usr/bin/env bash
# Downloads the candidate book fonts into tools/fonttool/fonts/ (not tracked).
# All four families are under the SIL Open Font License 1.1; each folder gets
# its OFL.txt. Noto Serif is copied from the system if it's installed.
set -euo pipefail
cd "$(dirname "$0")"
GF=https://github.com/google/fonts/raw/main/ofl

get() {   # get DIR NAME URL
  mkdir -p "fonts/$1"
  [ -s "fonts/$1/$2" ] && return
  curl -sSL --fail -o "fonts/$1/$2.part" "$3"
  mv "fonts/$1/$2.part" "fonts/$1/$2"
  echo "ok $1/$2"
}

# Literata: Google's e-book typeface (Play Books), variable (optical size + weight)
get literata "Literata[opsz,wght].ttf"        "$GF/literata/Literata%5Bopsz,wght%5D.ttf"
get literata "Literata-Italic[opsz,wght].ttf" "$GF/literata/Literata-Italic%5Bopsz,wght%5D.ttf"
get literata OFL.txt                          "$GF/literata/OFL.txt"
# PT Serif: ParaType, made for Cyrillic and Latin together
for s in Regular Bold Italic BoldItalic; do
  get ptserif "PT_Serif-Web-$s.ttf" "$GF/ptserif/PT_Serif-Web-$s.ttf"
done
get ptserif OFL.txt "$GF/ptserif/OFL.txt"
# Noto Serif: from the system (Arch: noto-fonts), else from Google Fonts
mkdir -p fonts/notoserif
for s in Regular Bold Italic BoldItalic; do
  src=/usr/share/fonts/noto/NotoSerif-$s.ttf
  if [ -s "$src" ]; then cp -n "$src" fonts/notoserif/; else
    echo "Noto Serif not installed: install noto-fonts or copy NotoSerif-$s.ttf here" >&2; fi
done
get notoserif OFL.txt "$GF/notoserif/OFL.txt"
# Libron: Nico Verbruggen's e-reader font (from Readerly / Newsreader). Latin
# only, no Cyrillic. The R release has the relaxed line spacing we want.
LIBRON=https://github.com/nicoverbruggen/libron/releases/download/v0.32
if [ ! -s fonts/libron/Libron_R-Regular.ttf ]; then
  mkdir -p fonts/libron
  curl -sSL --fail -o fonts/libron/Libron_R.zip "$LIBRON/Libron_R.zip"
  unzip -q -o -j fonts/libron/Libron_R.zip '*.ttf' -d fonts/libron
  rm fonts/libron/Libron_R.zip
  echo "ok libron"
fi
[ -s fonts/libron/OFL.txt ] || curl -sSL --fail -o fonts/libron/OFL.txt https://raw.githubusercontent.com/nicoverbruggen/libron/v0.32/LICENSE
