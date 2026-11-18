# ibv-convert

Transformă cărți EPUB, FB2 și TXT în fișiere `.ibv` pentru cititor (formatul: `../../spec/book-format.md`). E o aplicație Python 3 în linia de comandă care are nevoie de Pillow și numpy (`pip install pillow numpy`).

    python3 convert.py convert book.epub                 # -> book.ibv alături
    python3 convert.py convert *.epub *.txt -o out/      # un lot; o carte stricată nu-l oprește
    python3 convert.py convert old.txt --lang ro --title "Moara cu noroc" --author "Ioan Slavici"
    python3 convert.py validate out/*.ibv
    python3 convert.py dump out/book.ibv --records 40    # antet, META, TOC, blocuri, primele înregistrări
    python3 convert.py preview-cover out/book.ibv cover.png

Alte opțiuni pentru `convert`: `--no-hyphenate`, `--no-cover`, `--block BYTES` (mărimea țintă a blocului, implicit 16384) și `--stored` (fără compresie, ca hexul să se poată citi).

Fiecare carte e recitită și validată înainte de a fi scrisă. Raportul arată codificarea (TXT), cum s-au găsit paragrafele, mărimea cuprinsului, câte cuvinte s-au despărțit în silabe, caracterele înlocuite sau lipsă din font și avertismentele.

## Fișiere

| Fișier | Sarcină | Ce face |
|---|---|---|
| `convert.py` | | linia de comandă |
| `txt.py` | CONV-1 | TXT: detectarea codificării, antete Gutenberg/lib.ru, modul paragrafelor, titluri, separatoare, versuri, cuprinsuri tipărite |
| `epub.py` | CONV-2 | EPUB 2/3: OPF, spine, cuprins nav/NCX (completat cu titlurile din fișier când e prea sumar), XHTML → înregistrări, bold/italic din CSS, copertă, fără paginile adăugate de Gutenberg și ws-export |
| `fb2.py` | CONV-9 | FB2 și `.fb2.zip` |
| `doc.py` | | `Builder`-ul comun pe care îl umplu cititoarele și `finish()` (mai jos) |
| `textnorm.py` | CONV-3 | NFC, ş/ţ românești → ș/ț (și tastarea veche ã/º/þ), ghilimele după limbă, linii de dialog, puncte de suspensie, spații |
| `hyphenate.py` | CONV-4 | algoritmul lui Liang cu modelele TeX din `patterns/` |
| `glyphs.py` | CONV-5 | acoperirea fontului după intervalele din `../fonttool/fonttool.py`, înlocuiri, U+FFFD |
| `cover.py` | CONV-6 | copertă → încadrare în 240×416 → întinderea contrastului → Floyd–Steinberg → 1 bpp |
| `ibv.py` | CONV-7, SPEC-1 | formatul: scriere, citire, validare, afișare |
| `test_ibv.py`, `test_convert.py` | CONV-10 | teste |

## Fluxul de lucru

Un cititor parsează intrarea într-un `doc.Builder`: text cu stiluri, capete de paragraf cu tipul lor, separatoare, ancore și intrări de cuprins. `doc.finish()` face restul, la fel pentru orice intrare:

1. Curățarea textului: normalizare, înlocuirea glifelor lipsă, ordonarea rupturilor de rând.
2. Un paragraf legat cu `<br>` ale cărui rânduri sunt toate scurte (≤ 80 de caractere, în medie ≤ 55) e o strofă, deci devine rânduri VERSE cu BREAK între strofe.
3. Structura: se scot paragrafele goale, se unesc separatoarele repetate, se scot separatoarele de la margini și de lângă titluri, fără alineat după un titlu sau separator.
4. Despărțirea în silabe: cratime moi doar în PARA și QUOTE. Versurile, titlurile și adresele web sau de e-mail nu se despart.
5. Paragrafele de peste 8 KB se împart în înregistrări CONT. Stilurile devin intervale de octeți, iar țintele cuprinsului devin numere de paragraf.

Apoi `ibv.write()` și `ibv.validate()`.

## Teste

    python3 -m unittest test_ibv test_convert

66 de teste, cam 20 s. `test_convert.py` își construiește intrările EPUB și FB2 în cod, deci nu are nevoie de fișiere. `TestCorpus` convertește și fiecare carte din `../../corpus/files/` când dosarul există; vezi `../../corpus/README.md` și `fetch.sh`.

## Modele de despărțire în silabe

Sunt modelele TeX hyph-utf8. `patterns/README.md` arată sursa, drepturile de autor și licența fiecărui fișier. Pe scurt: en-gb e MIT, en-us e permisivă cu păstrarea notei, iar ru e LPPL. ro nu are licență declarată (rezerva e dicționarul LibreOffice). Modelele nu se copiază în fișierele `.ibv`, ci doar cratimele moi pe care le produc.

Algoritmul e scris aici (cam 120 de rânduri) în loc să fie luat din pyphen. Așa rămâne destul de mic ca să fie portat în firmware dacă dispozitivul va trebui vreodată să despartă singur, și citește direct fișierele TeX.
