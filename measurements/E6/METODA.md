# E6: ajută cu adevărat o rupere mai bună în rânduri?

Metoda v1, scrisă 2026-11-17, înainte de rularea de aici. Cifre de explorare
există deja din dezvoltarea LAY-5 (jurnalul, 2026-10-27: pe *Pride and
Prejudice* r mediu 1,57 → 1,14). Predicțiile de mai jos țin cont de ele;
rularea de aici e cea care intră în lucrare, pe tot corpusul și pe toate
mărimile.

**Întrebarea.** Cât îmbunătățește încadrarea totală (Knuth–Plass, LAY-5)
spațierea rândurilor justificate față de ruperea greedy, cu și fără despărțire
în silabe, la fiecare mărime de font de pe dispozitiv? Și cât costă în timp?

**Predicția.**
1. La treapta implicită, cu despărțirea pornită, pe cărțile în proză,
   Knuth–Plass scade r mediu cu cel puțin 15 % și r95 cu cel puțin 20 % față
   de greedy.
2. Fără despărțire, spațierea e mult mai proastă cu amândoi algoritmii (r
   mediu cel puțin de 1,5 ori mai mare), iar Knuth–Plass nu poate compensa
   lipsa despărțirii.
3. Knuth–Plass are mai puține cratime consecutive decât greedy.
4. Cu cât fontul e mai mare față de rând, cu atât e mai proastă spațierea, la
   toate variantele: cea mai proastă la treapta cea mai mare (4).
5. Pe PC, paginarea cu Knuth–Plass ține de cel mult 2 ori cât cea greedy.

**Montajul.** Simulatorul (`sim/`), care compilează pe PC exact nucleul de
paginare al firmware-ului; fonturile dispozitivului (`fonttool family`),
alese după limba cărții (Libron pentru latină, PT Serif pentru chirilică).
Zona de text e cea a dispozitivului: 216 × 382 px.

**Corpusul.** Cărțile din `corpus/`, câte un fișier pe carte: 12 cărți. Se
exclud copiile aceleiași cărți (`libru-dubrovsky-cp1251`, `pg2701-txt`,
`pg64597-txt`). Subsetul principal, **proza**, e stabilit dinainte: toate în
afară de versuri (`pg1322` Whitman, `pg35323` Eminescu, `pg14741` Derjavin) și
de culegerea de probleme (`pg16527-8`): 8 cărți, în engleză, română și rusă.
Când corpusul crește spre cele ~20 de cărți din MEAS-1, se rulează din nou.

**Variabile.** Independente: algoritmul (greedy, Knuth–Plass) × despărțirea
(pornită, oprită) × treapta de mărime (0–4, implicit 2; Libron 15–20 px,
PT Serif 16–21 px) = 20 de condiții. Controlate:
fontul după limbă, marginile, alineatul, regulile de văduve și orfane.

**Procedura.** `run.sh`: pentru fiecare condiție, `sim breaks` scrie fiecare
rând al fiecărei cărți, iar `linii.py` îl rezumă pe carte; `sim stats` dă
paginile și timpul. Timpul se măsoară de 5 ori la treapta implicită și se ia
mediana. `analiza.py` face tabelele și figurile.

**Metrici**, pe rândurile justificate (fără ultimul rând al paragrafului, fără
titluri):
- r = spațiul adăugat pe un interval / lățimea spațiului simplu (0 = spațiu
  simplu, 1 = de două ori mai lat, negativ = strâns): media, mediana, p95 și
  % rânduri cu r > 0,5 și cu r > 1 (rânduri „cu găuri”);
- % rânduri cu cratimă la final și numărul de cratime consecutive la 1000 de
  rânduri;
- paginile cărții; timpul de paginare pe PC, în µs pe pagină.

Pe proză, metricile se calculează pe toate rândurile puse împreună (fiecare
rând cântărește la fel), nu ca medie a mediilor pe carte.

**Criteriul.** Predicția 1 trece dacă ambele praguri sunt atinse; 5 trece dacă
raportul timpilor e ≤ 2. Timpul pe dispozitiv (ms pe pagină) se măsoară
separat, pe prototip, din jurnalul `perf` (E2), și se adaugă aici atunci.

**Ce poate strica măsurătoarea.** Lățimile din simulator sunt cele de pe
dispozitiv doar dacă fonturile sunt aceleași: fonturile se construiesc din
aceleași surse cu `fonttool`, iar `date/versiune.txt` păstrează sumele lor de
control. `sim stats` tipărește un hash al pixelilor fiecărei cărți: o rulare
repetată trebuie să dea aceleași hash-uri. Timpul pe PC variază cu încărcarea mașinii: de aceea 5
rulări și mediana.

**Date.** `date/linii.csv` (un rând pe carte × condiție), `date/stats-*.txt`
(ieșirea brută a `sim stats`), `date/timp.csv`, `date/versiune.txt` (commitul,
fonturile, compilatorul). Fișierele `sim breaks` nu se păstrează: au zeci de MB
și se refac identic din aceleași intrări (paginarea e deterministă, hash-ul o
confirmă).
