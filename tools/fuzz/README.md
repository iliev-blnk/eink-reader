# fuzz: fișierele de pe card citite fără încredere

Tot ce citește dispozitivul vine de pe cardul SD: cărțile `.ibv`, fonturile
`.ibf` și fișierele pe care și le scrie singur (indexul paginilor `.pg`,
poziția `.pos`, semnele de carte `.bm`, lista `recent`, setările). Un card poate
avea fișiere stricate (o cădere a curentului, un card vechi) sau făcute anume.
Regula: orice fișier se deschide corect sau se refuză, fără nicio citire sau
scriere în afara memoriei, fără să blocheze cititorul și fără să ceară memorie
peste ce are cipul.

Aici sunt trei fuzzere (libFuzzer) care încearcă să încalce regula pe codul
adevărat din `firmware/components`, neschimbat, plus o verificare exhaustivă a
decodorului UTF-8.

    tools/fuzz/run.sh                     # regresiile, apoi câte 60 s pe fuzzer
    FUZZ_TIME=1800 tools/fuzz/run.sh ibv  # doar cărțile, 30 de minute
    tools/fuzz/build/utf8_toate           # după run.sh (sau build.sh); ~25 s

Are nevoie de `clang` cu suport `-m32` (libFuzzer, ASan, UBSan pe 32 de biți),
zlib pe 32 de biți și Python 3. Rezultatele (jurnale, corpusul strâns, intrările
care au produs o problemă) rămân în `build/`, care nu intră în git.

## Cum

- **Codul e cel din firmware.** `memfile.h` e inclus forțat (`-include`) și
  înlocuiește `fopen`: o cale sub `/fuzz/` deschide intrarea fuzzerului din
  memorie (`fmemopen`), copiată exact cât e, ca ASan să prindă orice citire de
  după capăt. Altfel nimic din cod nu știe că e testat.
- **Pe 32 de biți**, ca ESP32-S3: `size_t`, `long` și pointerii au 4 octeți, deci
  un calcul de mărime care dă peste cap pe cip dă peste cap și aici. `FUZZ_M32=`
  compilează pe 64 de biți.
- **ASan + UBSan, fără recuperare**: o depășire de buffer, o depășire de întreg
  cu semn, o deplasare greșită sau o aliniere greșită oprește rularea.
  `-rss_limit_mb=2048` prinde cererile de memorie absurde, `-timeout=10` buclele
  care nu se mai termină.
- **CRC-urile sunt oprite** (`FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`, în
  `crc32_ok()` din `crc32.h`). Un CRC apără de un fișier stricat întâmplător, nu
  de unul făcut anume, care are CRC-ul corect. Fără asta, fuzzerul s-ar opri la
  verificarea CRC-ului și n-ar vedea nimic din ce urmează. În firmware și în
  simulator `crc32_ok()` e o simplă comparație.
- **Fiecare fișier acceptat e și folosit**, nu doar deschis, așa cum îl folosește
  cititorul:
  - `fuzz_ibv`: META, coperta, toate înregistrările (până la 4096) cu stilurile
    și deplasările lor, cuprinsul, paginarea a 64 de pagini în două configurații
    (Knuth–Plass justificat și greedy aliniat la stânga), desenarea fiecărei
    pagini într-un framebuffer 240×416, pagina înapoi și căutarea unei poziții,
    alegerea fontului după limbă.
  - `fuzz_font`: căutarea glifelor (inclusiv cod-puncte în afara Unicode),
    kerningul, desenul tuturor glifelor și a unor texte (cu UTF-8 invalid) în
    poziții tăiate de marginea ecranului, apoi paginarea și desenarea a 6 pagini
    dintr-o carte adevărată (`FUZZ_BOOK`) cu fontul fuzzat pe toate stilurile.
  - `fuzz_state`: primul octet alege fișierul (`.pg`, `.pos`, `.bm`, `recent`,
    setările), restul e conținutul. Un fișier acceptat se folosește (căutare în
    index, adăugare și ștergere de semne, o carte nouă în `recent`, citirea
    fiecărei setări).
- **Semințele** (`seeds.py`) sunt fișiere valide, cu CRC-urile corecte: cărți mici
  (comprimată, necomprimată, în rusă, goală) plus `sim/test/layout.ibv`, un font
  mic făcut pe loc plus fonturi ale dispozitivului, câte un exemplu din fiecare
  fișier de stare.
- **`regresii/`** are intrările care au găsit cele patru greșeli de mai jos
  (minimizate de libFuzzer) și două fișiere făcute de mână care le arată pe cele
  două depășiri de buffer. `run.sh` le rulează pe toate înainte de fuzzing.

## Ce a găsit

Patru greșeli, toate reparate în firmware. Primele două sunt depășiri de buffer
în heap pe cip, declanșate de un fișier de pe card.

| fișierul | greșeala | ce se întâmpla | regresia |
|---|---|---|---|
| `.pg` (`pageidx.c`) | numărul de pagini din antet nu era comparat cu mărimea fișierului | `n` uriaș: `malloc` de gigaocteți; `n` = 0x20000001: pe 32 de biți `n * 8` dă 8 octeți, iar bucla scrie mai departe. ASan: *heap-buffer-overflow WRITE of size 8* | `state/pg-malloc-urias`, `state/pg-n-peste-cap` |
| `.ibv` (`ibv.c`, `load_block`) | numărul de înregistrări dintr-un bloc vine din `first_para` al blocului următor și nu era limitat | un `first_para` mare: secunde de lucru și megaocteți pe bloc; pe 32 de biți `2 * nrec` dă peste cap și tabelul de deplasări iese mai mic decât bucla. ASan: *heap-buffer-overflow WRITE of size 2* | `ibv/nrec-mare-timeout`, `ibv/nrec-peste-cap` |
| `.ibf` (`layout.c`, `badness`) | cubul din badness pe 64 de biți | un font cu kerning negativ uriaș sau avansuri de sute de px face `over` și `room` atât de mari încât `100·over³` iese din `int64_t` (UBSan) | `font/badness-int64` |
| `.ibf` (`gfx.c`, `gfx_glyph`) | biții de completare de după lățimea glifei, din ultimul octet al fiecărui rând, erau desenați | un font cu biții aceia puși desena în afara cutiei glifei, iar o glifă pe ultimul rând al ecranului scria după framebuffer (ASan) | `font/glifa-biti-completare` |

Cele două depășiri pe 32 de biți au fost reproduse și pe codul de dinainte de
reparație, cu fișiere făcute de mână, cu CRC-uri corecte: un `.pg` de 48 de
octeți și un `.ibv` de 154 de octeți. Pe dispozitiv, un astfel de fișier pe card
strică heap-ul la deschiderea cărții. Fișierul `.pg` se citește singur, la fiecare
deschidere a cărții (`app_reader.c`).

Reparațiile:

- `pageidx_load`: înainte de `malloc`, `n · 8` trebuie să fie exact mărimea
  fișierului minus antetul și CRC-ul (calculat pe 64 de biți).
- `load_block`: o înregistrare are cel puțin 4 octeți, deci un bloc nu poate
  avea mai mult de `raw_len / 4` înregistrări (`raw_len` ≤ 32 KB, deci cel mult 8.192). Altfel
  `IBV_ERR_FORMAT`.
- `badness`: dacă `over ≥ 47 · room`, raportul e cel puțin 47 și
  `100 · 47³ = 10.382.300` depășește deja plafonul de 10⁷, deci rezultatul e
  plafonul, exact ca înainte. Altfel, numai dacă `over ≥ 2¹⁸` sau
  `room ≥ 2²⁰`, amândouă se înjumătățesc până încap. Pe un rând de 240 px
  (15.360 în unitățile motorului) nu se ajunge acolo, deci rezultatul e același
  bit cu bit.
- `gfx_glyph`: ultimul octet al fiecărui rând se maschează la lățimea glifei.
  Fonturile făcute de `tools/fonttool` au biții aceia zero, deci nu se schimbă
  niciun pixel.

Că rezultatele normale nu s-au schimbat s-a verificat: `sim/test/run.sh` trece
cu aceleași imagini de referință, `tools/texref` dă pe tot corpusul exact
același rezultat JSON ca în `tools/texref/rezultate/v4.json`, firmware-ul se
compilează, iar `emu/test.sh` trece cadru cu cadru.

## Testul diferențial: firmware-ul față de validator

Fuzzerele arată că firmware-ul nu citește în afara memoriei, nu că citește ce
trebuie. `conformanta.py` compară cititorul din firmware (`ibv.c`, prin
`ibv_dump.c`, cu ASan + UBSan, pe 32 de biți și cu CRC-urile verificate) cu
validatorul de referință (`tools/converter/ibv.py`), pe aceleași fișiere:

    tools/fuzz/conformanta.py            # 5000 de cazuri, ~2 min
    tools/fuzz/conformanta.py 20000 --samanta 7

Fișierele sunt cărți valide stricate (un câmp de structură, un octet din text,
octeți inserați sau scoși), apoi resigilate: CRC-urile blocurilor, ale
secțiunilor, al directorului și al antetului se refac, iar de cele mai multe
ori și `book_id` și numerele din antet. Fără asta, aproape orice stricăciune
s-ar opri la primul CRC. Printre semințe sunt și cazuri pe care specificația le
permite și o stricăciune întâmplătoare nu le nimerește: 14 secțiuni necunoscute
puse înaintea celor cunoscute, un antet de 60 de octeți, chei META și tipuri de
înregistrări necunoscute, biți de stil rezervați.

Contractul: (1) ce acceptă validatorul, firmware-ul citește întreg, cu aceleași
înregistrări, stiluri, deplasamente de text, metadate, cuprins și copertă;
(2) validatorul acceptă sau refuză cu un mesaj, nu cade; (3) cititorul nu cade.
Cazurile refuzate de validator și deschise de firmware sunt permise (firmware-ul
nu verifică NFC, spațiile, `book_id`, ordinea cuprinsului) și se afișează grupate
după motiv. Încălcările ajung în `build/conformanta/`.

Ce a găsit, la prima rulare (3000 de cazuri, 2026-11-22):
- **Validatorul cădea** cu o excepție Python (`struct.error`, `zlib.error`,
  `UnicodeDecodeError`) pe 324 de fișiere: META sau TOC tăiate, DEFLATE stricat,
  UTF-8 invalid în META. `parse()` verifică acum fiecare lungime.
- **Validatorul accepta ce refuza firmware-ul** în 38 de cazuri: un BLKS cu
  octeți în plus după ultima intrare. Acum META, TOC și BLKS trebuie să se
  termine exact după ultima intrare.
- **Firmware-ul refuza o carte validă cu mai mult de 16 secțiuni**, deși
  specificația cere ca secțiunile necunoscute să fie sărite. Directorul se
  citește acum pe bucăți și se țin doar etichetele cunoscute.
- Din citirea codului, nu din test: un varint de 5 octeți ≥ 2³² era trunchiat
  în firmware și acceptat de validator ca număr mai mare; acum ambele îl refuză,
  iar validatorul cere și forma cea mai scurtă (de ea depinde `book_id`). O
  copertă mai mare decât ecranul ar fi cerut sute de KB; firmware-ul o refuză
  acum, ca validatorul. Un tip de înregistrare necunoscut fără text era refuzat
  de validator, deși specificația spune că se sare.

După reparații: 20.000 de cazuri, 3140 acceptate de amândouă cu exact același
conținut, 10.540 refuzate de amândouă, 6320 refuzate doar de validator, 0
încălcări. Toate cele 26 de cărți din corpus trec în continuare validatorul.

## Cât s-a rulat

După reparații, `FUZZ_TIME=600 tools/fuzz/run.sh` (2026-11-17): regresiile
trec, apoi câte 10 minute pe fuzzer, pe 32 de biți cu ASan + UBSan, fără nicio
problemă:

| fuzzer | rulări | ramuri (cov) | trăsături (ft) | corpus | rulări/s |
|---|---:|---:|---:|---|---:|
| `fuzz_ibv` | 539.920 | 683 | 3.419 | 568 fișiere, 680 KB | 898 |
| `fuzz_font` | 1.705.178 | 568 | 2.795 | 380 fișiere, 956 KB | 2.837 |
| `fuzz_state` | 63.344.506 | 168 | 315 | 94 fișiere, 4 KB | 105.398 |

`fuzz_ibv` e cel mai lent pentru că fiecare carte acceptată se paginează și se
desenează. Intrările care găsiseră cele patru greșeli (înainte de minimizare) au
fost rulate din nou pe codul reparat și trec.

Decodorul UTF-8 (`utf8_next_n` din `font.c`) e verificat exhaustiv de
`utf8_toate`: toate cele 4.311.810.304 secvențe de 1–4 octeți, cu sfârșitul
textului după fiecare lungime, plus toți cei 1.112.064 scalari Unicode
codificați și decodificați înapoi. Pentru fiecare secvență: consumă între 1 și
câți octeți are voie; dacă nu întoarce U+FFFD, octeții consumați sunt exact
codificarea cea mai scurtă a unui scalar valid (nu un surogat, nu peste
U+10FFFF). 0 greșeli.

## Ce nu spune

- Fuzzingul arată că nu s-au găsit probleme în cât s-a rulat, nu că nu există.
  Acoperirea e a ramurilor atinse, iar unele (erori de citire de pe card, lipsă
  de memorie) nu sunt atinse deloc, pentru că fișierul din memorie nu eșuează.
- **Decomprimarea pe dispozitiv nu e fuzzată.** Firmware-ul folosește `tinfl`
  din ROM-ul ESP32-S3, care nu se poate compila aici. Fuzzerele folosesc
  `sim/inflate_zlib.c`, ca simulatorul. Codul din jurul lui (limita de 16 KB,
  verificarea lungimii decomprimate) e același; `tinfl` însuși nu e verificat.
- Cu CRC-urile oprite se testează un atacator care știe să calculeze un CRC.
  Fișierele stricate întâmplător sunt un caz mai ușor.
- Verificarea modelelor mărginită (CBMC sau ESBMC pe funcțiile mici de
  analiză) n-a fost făcută: niciunul nu e instalat din pachete pe mașina de
  lucru, iar un binar CBMC descărcat n-a fost rulat, pentru că instalarea lui
  n-a fost aprobată. E un pas posibil după ce se instalează.
- Greșelile din `badness` și `gfx_glyph` cer un font absurd sau făcut anume. Le
  repar pentru că fontul vine de pe card, ca orice altceva.
