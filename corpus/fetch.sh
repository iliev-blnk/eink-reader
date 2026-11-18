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
    echo "EȘUAT $name: s-a primit o pagină HTML de la $url" >&2
    return 1
  fi
  mv "files/$name.part" "files/$name"
  echo "ok $name"
  sleep 2          # cu blândețe față de gutenberg.org
}

gb() { get "pg$1.epub" "https://www.gutenberg.org/ebooks/$1.epub3.images"; }
# Oglinda PGLAF, pentru cărțile adăugate după ce gutenberg.org a început să răspundă cu 503 (2026-11-18)
gbm() { get "pg$1.epub" "https://gutenberg.pglaf.org/cache/epub/$1/pg$1-images-3.epub"; }
# Wikisource prin ws-export (EPUB 3, fără imagini)
ws() {    # ws NUME LIMBA TITLU
  get "$1" "https://ws-export.wmcloud.org/?lang=$2&format=epub-3&images=false&page=$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))' "$3")"
}

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
# Adăugate 2026-11-18 (MEAS-1: de la 13 la 20 de cărți)
gbm 1524   # Shakespeare, Hamlet: teatru, replici cu numele personajului, didascalii
gbm 4300   # Joyce, Ulysses: foarte lungă, dialog cu linie de dialog, punctuație neobișnuită
ws ws-amintiri.epub ro "Amintiri din copilărie"         # Creangă: proză românească cu diacritice, regionalisme
ws ws-moara-cu-noroc.epub ro "Moara cu noroc"            # Slavici: nuvelă, ș/ț cu virgulă
ws ws-ion.epub ro "Ion (Rebreanu)"                      # roman românesc lung (~1 MB text), dialog regional
ws ws-luceafarul.epub ro "Luceafărul (Eminescu)"         # versuri: strofe scurte, rânduri centrate/indentate
ws ws-kapitanskaya.epub ru "Капитанская дочка (Пушкин)/1978 (СО)"  # proză rusă, ortografie modernă, epigrafe
get libru-dubrovsky.fb2.zip "http://lib.ru/LITRA/PUSHKIN/dubrowskij.txt.fb2.zip"   # FB2 real (generatorul lib.ru), același text ca TXT-urile
get libru-geroi.fb2.zip "http://lib.ru/LITRA/LERMONTOW/geroi.txt.fb2.zip"          # Lermontov: FB2 cu secțiuni imbricate

# Versiunile TXT ale două dintre EPUB-urile de mai sus, pentru compararea cititoarelor pe aceeași carte
get pg2701.txt https://www.gutenberg.org/ebooks/2701.txt.utf-8
get pg64597.txt https://www.gutenberg.org/ebooks/64597.txt.utf-8

# Același text de Pușkin în CP1251, ca să se testeze că detectarea îl deosebește de KOI8-R
if [ ! -s files/libru-dubrovsky-cp1251.txt ]; then
  iconv -c -f koi8-r -t cp1251 files/libru-dubrovsky.txt > files/libru-dubrovsky-cp1251.txt
  echo "ok libru-dubrovsky-cp1251.txt"
fi

# Caragiale TXT în codificările românești vechi: CP1250 nu are ș/ț cu virgulă, deci textul trece întâi
# la sedilă (ş/ţ), cum arătau fișierele din anii '90–2000; ISO-8859-16 le are pe cele corecte.
if [ ! -s files/pg64597-cp1250.txt ]; then
  sed 's/ș/ş/g; s/Ș/Ş/g; s/ț/ţ/g; s/Ț/Ţ/g' files/pg64597.txt | iconv -f utf-8 -t cp1250//TRANSLIT > files/pg64597-cp1250.txt
  echo "ok pg64597-cp1250.txt"
fi
if [ ! -s files/pg64597-iso8859-16.txt ]; then
  iconv -f utf-8 -t iso-8859-16//TRANSLIT files/pg64597.txt > files/pg64597-iso8859-16.txt
  echo "ok pg64597-iso8859-16.txt"
fi
