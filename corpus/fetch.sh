#!/usr/bin/env bash
# Downloads the test corpus into corpus/files/ (not tracked; see README.md).
# Skips files that are already there. Run again any time.
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
  sleep 2          # be gentle with gutenberg.org
}

gb() { get "pg$1.epub" "https://www.gutenberg.org/ebooks/$1.epub3.images"; }

# English
gb 11      # Alice's Adventures in Wonderland: short, poems inside prose
gb 84      # Frankenstein: letters, many chapters
gb 1342    # Pride and Prejudice: big EPUB (illustrations), long
gb 2701    # Moby-Dick: long, Greek/Hebrew words (missing-glyph check)
gb 1322    # Leaves of Grass: long verse lines, 378 TOC entries
# Romanian
gb 35323   # Eminescu, Poezii: verse in <p>+<br>, typed without diacritics
gb 64597   # Caragiale, Nuvele
gb 62916   # Slavici, Povești
gb 65565   # Regina Maria, Țara mea
# Russian
gb 14741   # Derzhavin, Духовные оды: verse, small caps headings
get pg16527-8.txt https://www.gutenberg.org/files/16527/16527-8.txt     # CP1251 plain text
get libru-dubrovsky.txt "http://lib.ru/LITRA/PUSHKIN/dubrowskij.txt_Ascii.txt"   # KOI8-R, lib.ru header
# TXT versions of two EPUBs above, for comparing readers on the same book
get pg2701.txt https://www.gutenberg.org/ebooks/2701.txt.utf-8
get pg64597.txt https://www.gutenberg.org/ebooks/64597.txt.utf-8

# Same Pushkin text in CP1251, to test that detection tells it from KOI8-R
if [ ! -s files/libru-dubrovsky-cp1251.txt ]; then
  iconv -c -f koi8-r -t cp1251 files/libru-dubrovsky.txt > files/libru-dubrovsky-cp1251.txt
  echo "ok libru-dubrovsky-cp1251.txt"
fi
