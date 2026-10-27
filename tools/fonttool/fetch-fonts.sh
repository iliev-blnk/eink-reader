#!/usr/bin/env bash
# Descarcă fonturile candidate pentru cărți în tools/fonttool/fonts/ (nu sunt în depozit).
# Toate cele patru familii sunt sub SIL Open Font License 1.1; fiecare dosar își
# primește OFL.txt. Noto Serif se copiază din sistem, dacă e instalat.
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

# Literata: fontul Google pentru cărți electronice (Play Books), variabil (mărime optică + grosime)
get literata "Literata[opsz,wght].ttf"        "$GF/literata/Literata%5Bopsz,wght%5D.ttf"
get literata "Literata-Italic[opsz,wght].ttf" "$GF/literata/Literata-Italic%5Bopsz,wght%5D.ttf"
get literata OFL.txt                          "$GF/literata/OFL.txt"
# PT Serif: ParaType, făcut pentru chirilic și latin împreună
for s in Regular Bold Italic BoldItalic; do
  get ptserif "PT_Serif-Web-$s.ttf" "$GF/ptserif/PT_Serif-Web-$s.ttf"
done
get ptserif OFL.txt "$GF/ptserif/OFL.txt"
# Noto Serif: din sistem (Arch: noto-fonts), altfel de pe Google Fonts
mkdir -p fonts/notoserif
for s in Regular Bold Italic BoldItalic; do
  src=/usr/share/fonts/noto/NotoSerif-$s.ttf
  if [ -s "$src" ]; then cp -n "$src" fonts/notoserif/; else
    echo "Noto Serif not installed: install noto-fonts or copy NotoSerif-$s.ttf here" >&2; fi
done
get notoserif OFL.txt "$GF/notoserif/OFL.txt"
# Libron: fontul pentru cititoare al lui Nico Verbruggen (din Readerly / Newsreader).
# Doar latin, fără chirilice. Varianta R are spațierea mai largă a rândurilor pe care o vrem.
LIBRON=https://github.com/nicoverbruggen/libron/releases/download/v0.32
if [ ! -s fonts/libron/Libron_R-Regular.ttf ]; then
  mkdir -p fonts/libron
  curl -sSL --fail -o fonts/libron/Libron_R.zip "$LIBRON/Libron_R.zip"
  unzip -q -o -j fonts/libron/Libron_R.zip '*.ttf' -d fonts/libron
  rm fonts/libron/Libron_R.zip
  echo "ok libron"
fi
[ -s fonts/libron/OFL.txt ] || curl -sSL --fail -o fonts/libron/OFL.txt https://raw.githubusercontent.com/nicoverbruggen/libron/v0.32/LICENSE
