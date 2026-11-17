# E6: rezultate

Rularea din 2026-11-17, după [METODA.md](METODA.md) v1. Cifrele vin din
[tabele.md](tabele.md), generat de `analiza.py` din `date/`. Totul a rulat în
simulator, cu nucleul de paginare al firmware-ului și fonturile dispozitivului.
Timpul pe dispozitiv lipsește încă: vine din E2, pe prototip.

## Pe scurt

Pe proză (8 cărți, ~128.000 de rânduri justificate), la mărimea implicită, cu
despărțirea pornită:

| | greedy | Knuth–Plass | schimbare |
|---|---:|---:|---:|
| r mediu | 1,53 | 1,12 | −27 % |
| r p95 | 4,23 | 2,80 | −34 % |
| rânduri cu r > 1 | 55,3 % | 49,7 % | |
| cratime consecutive / 1000 de rânduri | 47,6 | 22,1 | −54 % |
| pagini | 9.755 | 9.579 | −1,8 % |
| timp de paginare pe PC | 162 ms | 376 ms | ×2,32 |

r e cât de lat iese spațiul dintre cuvinte într-un rând justificat față de un
spațiu simplu: 0 = la fel, 1 = de două ori mai lat.

![Distribuția lui r](figuri/distributia-r.svg)

## Predicțiile

| | predicția | rezultat |
|---|---|---|
| 1 | K–P scade r mediu ≥ 15 % și r p95 ≥ 20 % | trece: −27 % și −34 % |
| 2 | fără despărțire, r mediu de ≥ 1,5 ori mai mare; K–P nu compensează | trece: ×1,89 (greedy), ×1,90 (K–P) |
| 3 | K–P are mai puține cratime consecutive | trece: 22,1 față de 47,6 la 1000 de rânduri |
| 4 | spațierea e cea mai proastă la treapta 4 | trece, la toate cele patru variante |
| 5 | K–P cel mult de 2 ori mai lent decât greedy pe PC | **nu trece**: ×2,32 |

## Ce arată

**Încadrarea totală ajută, pe fiecare carte.** Toate cele 8 cărți în proză se
îmbunătățesc, cu 20–28 % la r mediu și 24–37 % la r p95. Și cărțile din afara
prozei se îmbunătățesc, cu o excepție la coadă: la Whitman (versuri lungi,
puține rânduri justificate) r p95 scade doar cu 8 %.

![r mediu pe carte](figuri/r-pe-carte.svg)

**Despărțirea contează mai mult decât algoritmul.** Greedy cu despărțire
(r 1,53) e mult mai bun decât Knuth–Plass fără despărțire (r 2,13). Cele două
împreună dau cel mai bun rezultat (1,12). Pe o coloană de 216 px, fără
despărțire, un cuvânt lung care nu încape lasă o gaură pe care nicio alegere a
rupturilor n-o mai poate umple.

**Cratimele consecutive scad la jumătate.** În cărțile românești scad cel mai
mult: Slavici 61 → 5, Caragiale 81 → 15, Regina Maria 97 → 18 la 1000 de
rânduri. Excepția e *Дубровский* (rusă, PT Serif): 166 → 114. Cuvintele
rusești sunt lungi, iar 38 % din rânduri se termină cu cratimă, deci rămân
multe perechi orice ar alege algoritmul.

**Rândurile strânse.** Knuth–Plass strânge 10,8 % din rânduri (r < 0, până la
−0,34). Greedy nu strânge niciodată. Strângerea
e motivul pentru care încap mai multe cuvinte pe rând și cartea are cu 1,8 %
mai puține pagini.

**Fontul mai mare strică spațierea, la toate variantele.** De la treapta 0 la
4, r mediu crește de 1,5–1,7 ori. Knuth–Plass cu despărțire la treapta 4
(1,45) e cam cât greedy cu despărțire la treapta 1 (1,39).

![r pe trepte](figuri/r-pe-trepte.svg)

**Chiar și cu tot ce ajută, coloana e îngustă.** Și la cea mai bună variantă,
jumătate din rânduri au spațiile mai mult decât dublate. E limita unui ecran de
3,7″ cu text justificat. Opțiunea „Aliniere: la stânga” din setări rămâne
justificată de date.

**Timpul.** Predicția 5 nu trece: pe PC, Knuth–Plass ține de 2,32 ori cât
greedy (de la ×2,03 la ×2,64 pe carte). Fără despărțire, raportul e doar ×1,07:
costul vine din rupturile posibile în plus pe care le aduce despărțirea, nu din
algoritm în sine. În cifre absolute e mic: 376 ms pentru 9.579 de pagini, deci
~39 µs pe pagină pe PC. Pe ESP32-S3 (240 MHz) va fi de câteva zeci de ori mai
mult, dar paginarea rulează în fundal, între întoarceri. Cât costă de fapt se
vede în E2 (componenta „paginare”); dacă trece de 10 % din timpul întoarcerii,
se revine aici.

## Comparație cu cifrele de explorare

În timpul dezvoltării LAY-5 (2026-10-27), pe *Pride and Prejudice*: r mediu
1,57 → 1,14. Aici, cu LAYOUT_VERSION 4 (după verificarea cu TeX): 1,57 → 1,13.
Diferența de 0,01 e în limita schimbărilor făcute de atunci în motor.

## Reproducere

    (cd corpus && ./fetch.sh)
    (cd tools/fonttool && ./fetch-fonts.sh \
        && python3 fonttool.py family libron --sizes 15,16,17,18,20 -o out/fam \
        && python3 fonttool.py family ptserif --sizes 16,17,18,19,21 -o out/fam)
    measurements/E6/run.sh
    python3 measurements/E6/analiza.py

`date/versiune.txt` are commitul, compilatorul și sumele de control ale
cărților și fonturilor. Hash-urile paginării au fost identice la toate cele 5
rulări repetate.
