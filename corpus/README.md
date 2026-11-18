# Corpusul de test (MEAS-1)

Cărțile cu care se testează convertorul (CONV-10) și, mai târziu, paginarea și dispozitivul. Cărțile în sine nu sunt urmărite în git. `./fetch.sh` le descarcă în `files/` și le sare pe cele care există deja. Rezultatul conversiei merge în `out/` (tot neurmărit):

    ./fetch.sh
    cd ../tools/converter
    python3 convert.py convert ../../corpus/files/* -o ../../corpus/out
    python3 -m unittest test_convert      # TestCorpus convertește și validează fiecare fișier din files/

Toate cărțile sunt în domeniul public. Textele Project Gutenberg sunt în domeniul public în SUA. Licența lor cere ca numele „Project Gutenberg” să fie scos dacă textul e redistribuit, iar convertorul scoate antetul și finalul PG (`pg-boilerplate`). Pușkin (m. 1837), Lermontov (m. 1841), Creangă (m. 1889), Eminescu (m. 1889), Slavici (m. 1925) și Rebreanu (m. 1944) sunt în domeniul public peste tot; lib.ru și Wikisource doar îi găzduiesc. Exporturile Wikisource (`ws-*.epub`) vin de la ws-export.wmcloud.org, sub CC BY-SA 3.0. Convertorul scoate pagina lor „Despre această ediție” (licența și lista contribuitorilor), la fel ca antetul PG. Fișierele rămân doar local și nu se redistribuie.

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
| `libru-dubrovsky.fb2.zip` | aceeași, FB2 | ru | [lib.ru](http://lib.ru/LITRA/PUSHKIN/dubrowskij.txt.fb2.zip) | FB2 real (generatorul lib.ru), în ZIP; FB2 față de TXT pe același text |
| `libru-geroi.fb2.zip` | Lermontov, *Герой нашего времени* | ru | [lib.ru](http://lib.ru/LITRA/LERMONTOW/geroi.txt.fb2.zip) | FB2 cu secțiuni imbricate, fără `<lang>` |
| `pg1524.epub` | Shakespeare, *Hamlet* | en | [PG 1524](https://www.gutenberg.org/ebooks/1524) | teatru: replici, didascalii, acte și scene |
| `pg4300.epub` | Joyce, *Ulysses* | en | [PG 4300](https://www.gutenberg.org/ebooks/4300) | cea mai lungă carte; dialog cu linie, punctuație neobișnuită |
| `pg64597-cp1250.txt` | Caragiale, *Nuvele*, în CP1250 | ro | (făcută de `fetch.sh`) | TXT românesc vechi: ș/ț cu sedilă (ş/ţ), detectarea CP1250 |
| `pg64597-iso8859-16.txt` | aceeași, în ISO-8859-16 | ro | (făcută de `fetch.sh`) | detectarea ISO-8859-16, ș/ț cu virgulă |
| `ws-amintiri.epub` | Creangă, *Amintiri din copilărie* | ro | [Wikisource](https://ro.wikisource.org/wiki/Amintiri_din_copil%C4%83rie) | proză românească cu diacritice; EPUB ws-export |
| `ws-moara-cu-noroc.epub` | Slavici, *Moara cu noroc* | ro | [Wikisource](https://ro.wikisource.org/wiki/Moara_cu_noroc) | nuvelă; capitolele sunt titluri într-un singur fișier |
| `ws-ion.epub` | Rebreanu, *Ion* | ro | [Wikisource](https://ro.wikisource.org/wiki/Ion_(Rebreanu)) | roman lung (1,2 MB text), două volume |
| `ws-luceafarul.epub` | Eminescu, *Luceafărul* | ro | [Wikisource](https://ro.wikisource.org/wiki/Luceaf%C4%83rul_(Eminescu)) | versuri cu diacritice (pg35323 nu le are) |
| `ws-kapitanskaya.epub` | Pușkin, *Капитанская дочка* | ru | [Wikisource](https://ru.wikisource.org/wiki/Капитанская_дочка_(Пушкин)/1978_(СО)) | proză rusă; titluri în majuscule; `dc:creator` cu „автор” în față |

26 de fișiere, 20 de cărți distincte, cât cere planul (MEAS-1). Cele 6 copii (aceeași carte în alt format sau altă codificare) verifică cititoarele unul față de altul. FB2 e acoperit acum de două fișiere reale de la lib.ru, iar TXT-ul românesc vechi de cele două codificări ale lui Caragiale.

Gutenberg.org a răspuns cu 503 pe 2026-11-18. Cărțile adăugate atunci se iau de pe oglinda PGLAF (`gbm` în `fetch.sh`), care are aceleași fișiere.

## Rezultate (2026-11-18)

Convertorul ibv-convert 0.1, blocuri de 16 KB, DEFLATE, despărțirea în silabe pornită. Timpii sunt pe laptopul de dezvoltare și includ parsarea, despărțirea, scrierea și validarea. Cele 15 fișiere din 2026-10-26 dau aceleași înregistrări, cuprinsuri și cuvinte despărțite ca atunci; doar timpii diferă puțin.

| Fișier | Intrare | Ieșire | Ieșire/intrare | Înregistrări | Blocuri | Cuprins | Cuvinte despărțite | Copertă | Timp |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|
| libru-dubrovsky-cp1251.txt | 135 KB | 90 KB | 67 % | 429 | 19 | 21 | 12.549 | – | 0,62 s |
| libru-dubrovsky.fb2.zip | 57 KB | 90 KB | 159 % | 434 | 19 | 22 | 12.555 | – | 0,43 s |
| libru-dubrovsky.txt | 135 KB | 90 KB | 67 % | 429 | 19 | 21 | 12.549 | – | 0,60 s |
| libru-geroi.fb2.zip | 114 KB | 183 KB | 160 % | 1.315 | 36 | 13 | 24.219 | – | 0,79 s |
| pg11.epub | 185 KB | 74 KB | 40 % | 1.034 | 11 | 16 | 4.091 | 240×331 | 0,38 s |
| pg1322.epub | 840 KB | 361 KB | 43 % | 14.550 | 47 | 378 | 328 | 240×320 | 1,13 s |
| pg1342.epub | 24.282 KB | 344 KB | 1 % | 2.430 | 52 | 63 | 28.971 | 240×373 | 1,68 s |
| pg14741.epub | 138 KB | 82 KB | 60 % | 4.041 | 13 | 53 | 779 | 240×360 | 0,37 s |
| pg1524.epub | 347 KB | 100 KB | 29 % | 5.693 | 13 | 30 | 1.323 | 240×360 | 0,47 s |
| pg16527-8.txt | 167 KB | 77 KB | 46 % | 1.005 | 18 | 0 | 12.436 | – | 0,63 s |
| pg2701.epub | 795 KB | 600 KB | 75 % | 2.954 | 86 | 145 | 42.792 | 240×378 | 2,51 s |
| pg2701.txt | 1.246 KB | 589 KB | 47 % | 2.823 | 86 | 137 | 42.940 | – | 2,48 s |
| pg35323.epub | 468 KB | 458 KB | 98 % | 22.951 | 71 | 282 | 14.443 | 240×360 | 1,76 s |
| pg4300.epub | 1.459 KB | 780 KB | 53 % | 7.708 | 111 | 87 | 54.573 | 240×324 | 3,37 s |
| pg62916.epub | 415 KB | 143 KB | 34 % | 1.559 | 23 | 10 | 22.214 | 240×370 | 0,81 s |
| pg64597-cp1250.txt | 208 KB | 106 KB | 51 % | 967 | 17 | 15 | 16.200 | – | 0,77 s |
| pg64597-iso8859-16.txt | 210 KB | 107 KB | 51 % | 967 | 17 | 15 | 16.244 | – | 0,86 s |
| pg64597.epub | 176 KB | 113 KB | 64 % | 977 | 17 | 12 | 16.240 | 240×360 | 0,66 s |
| pg64597.txt | 230 KB | 107 KB | 47 % | 967 | 17 | 15 | 16.244 | – | 0,53 s |
| pg65565.epub | 369 KB | 158 KB | 43 % | 1.079 | 25 | 16 | 24.607 | 240×355 | 0,79 s |
| pg84.epub | 430 KB | 202 KB | 47 % | 850 | 30 | 32 | 17.715 | 240×368 | 1,13 s |
| ws-amintiri.epub | 67 KB | 76 KB | 112 % | 445 | 12 | 6 | 11.606 | – | 0,40 s |
| ws-ion.epub | 379 KB | 486 KB | 128 % | 2.857 | 78 | 15 | 76.569 | – | 2,53 s |
| ws-kapitanskaya.epub | 4.359 KB | 159 KB | 4 % | 844 | 32 | 18 | 20.984 | – | 0,76 s |
| ws-luceafarul.epub | 16 KB | 7 KB | 41 % | 582 | 1 | 2 | 105 | – | 0,05 s |
| ws-moara-cu-noroc.epub | 87 KB | 104 KB | 119 % | 851 | 18 | 18 | 16.937 | – | 0,57 s |

Raportul ieșire/intrare se calculează față de fișierul sursă, care la EPUB e deja arhivat, deci spune puțin. Copertele sunt de 1 bpp, cel mult 240×416, și ocupă o parte din ieșire.

Aceeași carte din două surse iese aproape de aceeași mărime (Moby-Dick 600 / 589 KB, Caragiale 113 / 107 KB; versiunile EPUB au și copertă). Ambele codificări ale lui Dubrovski dau cărți identice la nivel de octet. Versiunea FB2 a lui Dubrovski are 434 de înregistrări față de 429 și o intrare în plus în cuprins. Textul e același; FB2 marchează altfel câteva titluri.

Caragiale în ISO-8859-16 dă exact ce dă TXT-ul UTF-8 (967 de înregistrări, 16.244 de cuvinte despărțite). În CP1250 ies 16.200: ediția PG are 1.225 de litere à/ì/ù (ortografia veche), pe care CP1250 nu le are și `iconv` le scrie a/i/u. ş/ţ cu sedilă ies ș/ț cu virgulă.

Cărțile de versuri despart puține cuvinte (Whitman 328, Derjavin 779), pentru că înregistrările VERSE nu se despart niciodată. Eminescu are multe înregistrări pentru că fiecare vers e una.

## Probleme cunoscute ale surselor (nu erori ale convertorului)

- **Eminescu (pg35323)** e tastat fără diacritice („si” în loc de „și”). Convertorul nu le poate pune la loc.
- **Derjavin (pg14741)** nu are spații între strofe în sursă, deci poeziile ies ca o singură strofă lungă.
- **Moby-Dick** are 7 litere grecești și ebraice pe care fontul nu le acoperă. Devin U+FFFD și apar ca MISSING în raport.
- **Racinski (pg16527-8.txt)** nu are titluri recunoscute, deci cartea nu are cuprins.
- **Caragiale TXT (pg64597.txt)**: rândurile paginii de titlu scrise cu majuscule („SCRIITORII ROMÂNI”) arată ca titluri și ajung în cuprins (15 intrări față de 12 în EPUB). TXT nu are marcaje, deci e de așteptat.
- **Exporturile Wikisource** n-au copertă. Pagina de titlu generată („Exportat de la Wikisource pe …”) rămâne în carte, ca prima intrare din cuprins. Cuprinsul lor are o intrare pe fișier, iar capitolele sunt titluri în fișier, așa că convertorul le adaugă ca subintrări (`_inner_headings` în `epub.py`). La *Ion*, capitolele I și VII încep exact unde încep volumele și rămân sub numele volumului.
- **Luceafărul (ws-luceafarul)** are un link „▲ Începutul paginii” din pagina wiki. ▲ nu e în font (o glifă lipsă).
- **Ulysses (pg4300)** are 11 ſ (s lung), înlocuite cu s, și 5 semne pe care fontul nu le are (†, ✠, bara combinantă U+0338).
- **lib.ru FB2** nu are `<lang>`, deci limba e ghicită (corect, ru). La *Герой нашего времени* „Часть первая” e paragraf, nu titlu, în sursă.
