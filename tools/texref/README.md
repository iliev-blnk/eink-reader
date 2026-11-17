# texref: TeX ca arbitru al ruperii în rânduri

Paragrafele justificate se rup în rânduri cu încadrarea totală Knuth–Plass
(`firmware/components/core/layout.c`, LAY-5). Programul de aici ia rândurile
așezate de dispozitiv pe tot corpusul și le compară cu ce face, pe aceleași
paragrafe, cu aceleași lățimi și aceiași parametri, algoritmul din LuaTeX. Unde
diferă, spune și din ce cauză.

    cmake --build sim/build
    sim/build/sim breaks corpus/out/*.ibv --fonts tools/fonttool/out/fam > rupturi.txt
    python3 tools/texref/texref.py rupturi.txt --examples 5 --json rezultat.json

Are nevoie de `luatex` (TeX Live) și de biblioteca standard Python; cartea o
citește cu `tools/converter/ibv.py`, fonturile cu `tools/fonttool/ibf.py`.
O rulare pe tot corpusul ține ~25 s pe 6 nuclee. Codul de ieșire e 0 doar dacă toate
verificările de la punctele 1 și 2 de mai jos ies 100 %.

## Cum se știe că cele două părți rezolvă aceeași problemă

O comparație cu TeX spune ceva doar dacă TeX primește exact paragraful pe care
l-a avut dispozitivul. De aceea nimic nu se ia din codul C, în afară de
rândurile finale:

1. **Măsurarea.** `texref.py` reface fiecare paragraf din fișierul `.ibv` și din
   fișierele de font: cutii (literele dintre două puncte de rupere, cu
   kerning), spații elastice, cratime moi, ruperi după liniuță, U+200B, U+2028,
   U+00A0, alineatul. Lățimea fiecărui rând justificat calculată aici se
   compară cu cea a dispozitivului (lățimea rândului minus spațiul adăugat la
   justificare, din `sim breaks`). Pe corpus: **201.015 din 201.015 identice**.
2. **Algoritmul.** În `texref.py` e și un model în Python al algoritmului din TeX
   (tex.web, §813–§890), cu abaterile dispozitivului ca opțiuni. Fără abateri,
   modelul trebuie să dea exact rupturile și penalizarea totală pe care le
   raportează LuaTeX, la ambele toleranțe. Cu abaterile versiunii de motor din
   fișier, trebuie să dea exact rândurile dispozitivului. Pe corpus amândouă
   ies **100 %** (15.578 de paragrafe față de LuaTeX la toleranța 400 și la
   10000; 15.581 față de dispozitiv). Abia așa se poate spune de unde vine o
   diferență: modelul pornește câte o abatere și numără ce schimbă.

LuaTeX primește paragrafele prin `linebreak.lua` ca liste de noduri (`rule`,
`glue`, `disc`, `penalty`) în modul ini, fără niciun format. Lățimile sunt în
1/64 px, iar 1 px se ia drept 1 pt (1/64 px = 1024 sp), deci rotunjirile sunt
aceleași. Parametrii sunt cei ai dispozitivului: `\linepenalty` 10,
`\hyphenpenalty` și `\exhyphenpenalty` 100, `\doublehyphendemerits` 100000,
`\finalhyphendemerits` 5000, `\adjdemerits` 10000, fără `\pretolerance` și fără
`\emergencystretch`. Un spațiu se lățește până la de două ori lățimea lui și se
îngustează cu o treime.

Două ciudățenii ale lui TeX, găsite pe drum, sunt modelate:

- La ultima rupere din ultima trecere, dacă a rămas un singur nod activ, TeX îi
  pune „demerits artificiale” și nu adună costul ultimului rând în total. De
  aceea costurile de la punctul 4 se calculează rând cu rând pe ambele părți,
  nu se iau din totalul raportat de TeX.
- TeX aruncă spațiile și penalizările de după o rupere. Două U+2028 la rând nu
  dau deci un rând gol, iar un U+00A0 de după U+2028 dispare. Cele 5 paragrafe
  de felul ăsta din corpus nu se pot compara cu TeX; se compară doar cu modelul.

## Rezultatele

Corpusul: 15 cărți (11 în Libron 17 px, 4 în PT Serif 18 px), lățimea textului
216 px, despărțire în silabe pornită. 15.583 de înregistrări justificate.
Rapoartele complete sunt în `rezultate/` (`v3.txt` înainte, `v4.txt` după).

Dispozitivul rupe un paragraf în cel mult trei treceri: întâi la toleranța 400,
apoi, dacă nu se poate, acceptând orice rând care încape, iar dacă nici așa,
greedy. Prima trecere se compară cu LuaTeX la toleranța 400, a doua cu LuaTeX la
toleranța 10000.

| | motorul v3 (înainte) | motorul v4 (după) |
|---|---|---|
| treceri: 400 / orice rând / greedy | 12.103 / 3.475 / 5 | 12.128 / 3.453 / 2 |
| prima trecere, paragrafe identice cu TeX | 11.352 din 12.103 (93,79 %) | 12.050 din 12.128 (99,36 %) |
| prima trecere, rânduri identice | 97,60 % | 99,65 % |
| a doua trecere, paragrafe identice cu TeX la 10000 | 3.016 din 3.473 (86,84 %) | 3.312 din 3.448 (96,06 %) |
| toate, paragrafe identice | 92,24 % | 98,63 % |
| toate, rânduri identice | 97,59 % | 99,39 % |
| diferite în prima trecere, cât de scumpe în măsura lui TeX (mediană / p90 / max) | +3,5 % / +128,9 % / +71.630 % | +0,5 % / +7,1 % / +106,2 % |

A doua trecere față de TeX la toleranța 400 nu are sens: acolo TeX acceptă un
rând peste toleranță în 99,94 % din paragrafe (overfull sau underfull), adică
ajunge tot la „orice rând”.

Cele 2 paragrafe rămase greedy au o bucată fără niciun punct de rupere mai lată
decât rândul (pg64597, înregistrările 373 și 388); acolo și TeX ar lăsa rândul
să iasă din margine.

### Ce a găsit prima rulare

Motorul v3 avea cinci abateri de la TeX. Fiecare pornită singură peste modelul
TeX, câte paragrafe din prima trecere schimbă:

| abaterea | ce face | paragrafe |
|---|---|---|
| `one_node` | o singură stare pe punct de rupere, nu câte una pe clasă de potrivire | 450 |
| `fill_natural` | ultimul rând nu se îngustează, trebuie să încapă la lățimea lui | 229 |
| `exact_bad` | badness exact (100·r³, rotunjit în jos, fără plafon), nu aproximarea din TeX | 74 |
| `no_first_adj` | primul rând nu e comparat cu o clasă de potrivire dinainte | 58 |
| `shrink_total` | îngustarea e o treime din suma spațiilor, rotunjită o dată pe rând | 3 |
| toate cinci | | 751 |

Dintre paragrafele pe care prima trecere v3 nu le putea așeza, 27 erau așezate
de TeX la toleranța 400 fără niciun rând peste ea; 25 din cauza lui
`fill_natural`, 2 din cauza lui `exact_bad`.

Cea mai importantă e `one_node`, și e o greșeală, nu o alegere. Penalizarea unui
rând depinde de clasa de potrivire a rândului dinainte (foarte larg, larg,
potrivit, strâns: `\adjdemerits` dacă sar două clase). Motorul v3 ținea la
fiecare punct de rupere doar cea mai bună împărțire, oricare i-ar fi fost
clasa ultimului rând, deci programarea dinamică nu era optimă nici pentru
propria funcție de cost: o împărțire puțin mai scumpă până aici, dar cu un rând
de altă clasă, putea ieși mai ieftină la rândul următor și se pierduse. TeX
ține câte un nod activ pe clasă tocmai de aceea. `fill_natural` și
`no_first_adj` erau și ele nescrise nicăieri, doar moduri în care codul ieșise
altfel decât TeX.

### Ce s-a schimbat în motor (LAYOUT_VERSION 4)

- La fiecare punct de rupere se ține cea mai bună împărțire pentru fiecare
  clasă a rândului care se termină acolo, ca în TeX. O clasă cu mai mult de
  `ADJ_FITNESS` peste cea mai bună nu poate câștiga și nu se ține; de aceea
  diferența față de cea mai bună încape pe 16 biți. Zona de lucru
  `layout_fit_t` a crescut de la ~66 KB la ~92 KB (94.288 de octeți); în
  firmware rămân 114 KB de DRAM internă liberă la compilare.
- Începutul paragrafului ține loc de rând „potrivit”, ca în TeX.
- Ultimul rând se poate îngusta ca oricare altul (și se și desenează așa).
- Între două U+2028 la rând e admis un rând gol; înainte, astfel de
  înregistrări cădeau la greedy (3 în pg14741).

Pe corpus, motorul v4 dă exact trecerile prezise de model înainte de a fi scris
(12.128 / 3.453 / 2), iar modelul cu cele două abateri rămase reproduce toate
rândurile lui.

Cu metricile E6 din `sim stats` (fonturile dispozitivului, aceeași zonă de
text), v4 față de v3: spațiul justificat mediu e mai mic sau egal în toate cele
15 cărți și procentul de rânduri cu spații largi (r > 0,5) scade în toate 15
(cel mai mult în pg14741, 73,8 % → 61,8 %, unde înregistrările cu U+2028 dublu
nu mai cad la greedy, și în pg1322, 76,4 % → 71,5 %). Rândurile cu cratimă și
cratimele consecutive rămân practic la fel (5.466 → 5.457 de perechi pe
corpus). Corpusul are 20 de pagini mai puțin (20.911 → 20.891). Paginarea
întregului corpus durează pe PC ~5 % mai mult (382 → 403 ms). Paginile tuturor cărților se schimbă puțin (*Moby Dick* are 3.694
de pagini în loc de 3.699), deci indexurile de pagini făcute cu v3 se refac:
`LAYOUT_VERSION` intră în `layout_hash`.

### Ce a rămas diferit, cu intenție

- `exact_bad` (74 de paragrafe în prima trecere). TeX aproximează badness-ul și
  îl plafonează la 10000. A doua trecere a dispozitivului acceptă orice rând
  care încape și trebuie să deosebească un rând foarte larg de unul și mai larg,
  ceea ce plafonul nu permite (decizia e în Jurnal, 2026-10-27). Diferențele
  din prima trecere vin din rotunjirea aproximării, nu din plafon; costul lor de
  la tabel e măsurat cu badness-ul lui TeX, deci firesc iese în favoarea lui
  TeX. Cu formula lui TeX sub plafon ar mai rămâne 3 paragrafe diferite din
  12.128.
- `shrink_total` (3 paragrafe): îngustarea se rotunjește o dată pe rând, nu pe
  fiecare spațiu. Ar cere încă o sumă de ținut pe fiecare punct de rupere
  pentru o diferență de câteva unități de 1/64 px pe rând.

O a șasea diferență e posibilă în teorie, dar n-a apărut în corpus: TeX scoate
definitiv un nod activ după ce un rând pornit din el iese prea lung, iar
motorul doar încetează să mai caute înapoi la punctul curent.

## Ce nu spune

TeX e aici arbitrul faptului că motorul găsește optimul funcției de cost pe
care o declară, nu al frumuseții paginii. Cât de bune sunt rândurile față de
greedy (spații lățite, cratime, cratime consecutive) măsoară `sim stats`, iar
cât durează pe cip se măsoară pe prototip (E6).
