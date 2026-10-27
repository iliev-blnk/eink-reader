#!/usr/bin/env bash
# Descarcă corpusul de test în corpus/files/ (nu e în depozit; vezi README.md).
# Sare fișierele care există deja. Se poate rula din nou oricând.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p files

get() {   # get NAME URL
  local name=$1 url=$2
  [ -s "files/$name" ] && return
  curl -sSL --fail -A "eink-reader-corpus/0.1" -o "files/$name.part" "$url"
  if head -c 512 "files/$name.part" | grep -qi "<!doctype html\|<html"; then
    rm "files/$name.part"
    echo "FAILED $name: got an HTML page from $url" >&2
    return 1
  fi
  mv "files/$name.part" "files/$name"
  echo "ok $name"
  sleep 2          # cu blândețe față de gutenberg.org
}

gb() { get "pg$1.epub" "https://www.gutenberg.org/ebooks/$1.epub3.images"; }

# Engleză
gb 11      # Alice's Adventures in Wonderland: scurtă, poezii în proză
gb 84      # Frankenstein: scrisori, multe capitole
gb 1342    # Pride and Prejudice: EPUB mare (ilustrații), lungă
gb 2701    # Moby-Dick: lungă, cuvinte grecești/ebraice (verificarea glifelor lipsă)
gb 1322    # Leaves of Grass: versuri lungi, 378 de intrări în cuprins
# Română
gb 35323   # Eminescu, Poezii: versuri în <p>+<br>, tastate fără diacritice
gb 64597   # Caragiale, Nuvele
gb 62916   # Slavici, Povești
gb 65565   # Regina Maria, Țara mea
# Rusă
gb 14741   # Derjavin, Духовные оды: versuri, titluri cu majuscule mici
get pg16527-8.txt https://www.gutenberg.org/files/16527/16527-8.txt     # text simplu CP1251
get libru-dubrovsky.txt "http://lib.ru/LITRA/PUSHKIN/dubrowskij.txt_Ascii.txt"   # KOI8-R, antet lib.ru
# Versiunile TXT ale două dintre EPUB-urile de mai sus, pentru compararea cititoarelor pe aceeași carte
get pg2701.txt https://www.gutenberg.org/ebooks/2701.txt.utf-8
get pg64597.txt https://www.gutenberg.org/ebooks/64597.txt.utf-8

# Același text de Pușkin în CP1251, ca să se testeze că detectarea îl deosebește de KOI8-R
if [ ! -s files/libru-dubrovsky-cp1251.txt ]; then
  iconv -c -f koi8-r -t cp1251 files/libru-dubrovsky.txt > files/libru-dubrovsky-cp1251.txt
  echo "ok libru-dubrovsky-cp1251.txt"
fi
