# Corpusul de test (MEAS-1)

Cărțile cu care se testează convertorul (CONV-10) și, mai târziu, paginarea și dispozitivul. Cărțile în sine nu sunt urmărite în git. `./fetch.sh` le descarcă în `files/` și le sare pe cele care există deja. Rezultatul conversiei merge în `out/` (tot neurmărit):

    ./fetch.sh
    cd ../tools/converter
    python3 convert.py convert ../../corpus/files/* -o ../../corpus/out
    python3 -m unittest test_convert      # TestCorpus convertește și validează fiecare fișier din files/

Toate cărțile sunt în domeniul public. Textele Project Gutenberg sunt în domeniul public în SUA. Licența lor cere ca numele „Project Gutenberg” să fie scos dacă textul e redistribuit, iar convertorul scoate antetul și finalul PG (`pg-boilerplate`). Pușkin a murit în 1837, deci textul lui e în domeniul public peste tot; lib.ru doar îl găzduiește.

## Cărți

| Fișier | Carte | Limba | Sursa | Ce testează |
|---|---|---|---|---|
| `pg11.epub` | Carroll, *Alice's Adventures in Wonderland* | en | [PG 11](https://www.gutenberg.org/ebooks/11) | scurtă; poezii în proză |
| `pg84.epub` | Shelley, *Frankenstein* | en | [PG 84](https://www.gutenberg.org/ebooks/84) | scrisori, multe capitole |
| `pg1342.epub` | Austen, *Pride and Prejudice* | en | [PG 1342](https://www.gutenberg.org/ebooks/1342) | EPUB de 24 MB (ilustrații), lungă |
| `pg2701.epub` | Melville, *Moby-Dick* | en | [PG 2701](https://www.gutenberg.org/ebooks/2701) | lungă; cuvinte grecești și ebraice (glife lipsă) |
| `pg2701.txt` | aceeași, text simplu | en | [PG 2701](https://www.gutenberg.org/ebooks/2701) | cititorul TXT față de EPUB pe aceeași carte; cuprins tipărit |
| `pg1322.epub` | Whitman, *Leaves of Grass* | en | [PG 1322](https://www.gutenberg.org/ebooks/1322) | versuri lungi, 378 de intrări în cuprins |
| `pg35323.epub` | Eminescu, *Poezii* | ro | [PG 35323](https://www.gutenberg.org/ebooks/35323) | versuri în `<p>` + `<br>`; tastată fără diacritice |
| `pg64597.epub` | Caragiale, *Nuvele* | ro | [PG 64597](https://www.gutenberg.org/ebooks/64597) | proză românească, dialog |
| `pg64597.txt` | aceeași, text simplu | ro | [PG 64597](https://www.gutenberg.org/ebooks/64597) | TXT față de EPUB; rândurile paginii de titlu |
| `pg62916.epub` | Slavici, *Povești* | ro | [PG 62916](https://www.gutenberg.org/ebooks/62916) | proză românească, ș/ț |
| `pg65565.epub` | Regina Maria, *Țara mea* | ro | [PG 65565](https://www.gutenberg.org/ebooks/65565) | proză românească |
| `pg14741.epub` | Derjavin, *Духовные оды* | ru | [PG 14741](https://www.gutenberg.org/ebooks/14741) | versuri rusești, titluri în majuscule mici |
| `pg16527-8.txt` | Racinski, *1001 задача для умственного счёта* | ru | [PG 16527](https://www.gutenberg.org/ebooks/16527) | text simplu CP1251; numere, paragrafe scurte |
| `libru-dubrovsky.txt` | Pușkin, *Дубровский* | ru | [lib.ru](http://lib.ru/LITRA/PUSHKIN/dubrowskij.txt) | KOI8-R; antetul lib.ru și titluri `\x14…\x15` |
| `libru-dubrovsky-cp1251.txt` | aceeași, convertită cu `iconv` | ru | (făcută de `fetch.sh`) | detectarea CP1251 față de KOI8-R pe același text |

15 fișiere, 13 cărți distincte. Planul cere vreo 20. Încă lipsesc:

- o carte FB2 reală. FB2 e acoperit doar de un fișier sintetic din `test_convert.py`. Sursele obișnuite de FB2 (Flibusta, lib.rus.ec) au drepturi neclare, așa că se pune un `.fb2` / `.fb2.zip` propriu în `files/` și se rulează din nou.
- un TXT românesc în CP1250 sau ISO-8859-16. Detectarea CP1250 e acoperită doar de `TestEncoding`.

## Rezultate (2026-10-26)

Convertorul ibv-convert 0.1, blocuri de 16 KB, DEFLATE, despărțirea în silabe pornită. Timpii sunt pe laptopul de dezvoltare și includ parsarea, despărțirea, scrierea și validarea.

| Fișier | Intrare | Ieșire | Ieșire/intrare | Înregistrări | Blocuri | Cuprins | Cuvinte despărțite | Copertă | Timp |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|
| libru-dubrovsky.txt | 136 KB | 90 KB | 67 % | 429 | 19 | 21 | 12.549 | – | 0,73 s |
| libru-dubrovsky-cp1251.txt | 136 KB | 90 KB | 67 % | 429 | 19 | 21 | 12.549 | – | 0,70 s |
| pg11.epub | 188 KB | 74 KB | 40 % | 1.034 | 11 | 16 | 4.091 | 240×331 | 0,42 s |
| pg84.epub | 432 KB | 202 KB | 47 % | 850 | 30 | 32 | 17.715 | 240×368 | 1,12 s |
| pg1322.epub | 840 KB | 361 KB | 43 % | 14.550 | 47 | 378 | 328 | 240×320 | 1,27 s |
| pg1342.epub | 24.284 KB | 344 KB | 1 % | 2.430 | 52 | 63 | 28.971 | 240×373 | 1,87 s |
| pg14741.epub | 140 KB | 82 KB | 60 % | 4.041 | 13 | 53 | 779 | 240×360 | 0,40 s |
| pg16527-8.txt | 168 KB | 77 KB | 46 % | 1.005 | 18 | 0 | 12.436 | – | 0,74 s |
| pg2701.epub | 796 KB | 600 KB | 75 % | 2.954 | 86 | 145 | 42.792 | 240×378 | 2,81 s |
| pg2701.txt | 1.248 KB | 589 KB | 47 % | 2.823 | 86 | 137 | 42.940 | – | 3,21 s |
| pg35323.epub | 468 KB | 458 KB | 98 % | 22.951 | 71 | 282 | 14.443 | 240×360 | 2,26 s |
| pg62916.epub | 416 KB | 143 KB | 34 % | 1.559 | 23 | 10 | 22.214 | 240×370 | 0,98 s |
| pg64597.epub | 176 KB | 113 KB | 64 % | 977 | 17 | 12 | 16.240 | 240×360 | 0,76 s |
| pg64597.txt | 232 KB | 107 KB | 47 % | 967 | 17 | 15 | 16.244 | – | 0,66 s |
| pg65565.epub | 372 KB | 158 KB | 43 % | 1.079 | 25 | 16 | 24.607 | 240×355 | 1,00 s |

Raportul ieșire/intrare se calculează față de fișierul sursă, care la EPUB e deja arhivat, deci spune puțin. Copertele sunt de 1 bpp, cel mult 240×416, și ocupă o parte din ieșire.

Aceeași carte din două surse iese aproape de aceeași mărime (Moby-Dick 600 / 589 KB, Caragiale 113 / 107 KB; versiunile EPUB au și copertă). Ambele codificări ale lui Dubrovski dau cărți identice la nivel de octet.

Cărțile de versuri despart puține cuvinte (Whitman 328, Derjavin 779), pentru că înregistrările VERSE nu se despart niciodată. Eminescu are multe înregistrări pentru că fiecare vers e una.

## Probleme cunoscute ale surselor (nu erori ale convertorului)

- **Eminescu (pg35323)** e tastat fără diacritice („si” în loc de „și”). Convertorul nu le poate pune la loc.
- **Derjavin (pg14741)** nu are spații între strofe în sursă, deci poeziile ies ca o singură strofă lungă.
- **Moby-Dick** are 7 litere grecești și ebraice pe care fontul nu le acoperă. Devin U+FFFD și apar ca MISSING în raport.
- **Racinski (pg16527-8.txt)** nu are titluri recunoscute, deci cartea nu are cuprins.
- **Caragiale TXT (pg64597.txt)**: rândurile paginii de titlu scrise cu majuscule („SCRIITORII ROMÂNI”) arată ca titluri și ajung în cuprins (15 intrări față de 12 în EPUB). TXT nu are marcaje, deci e de așteptat.
