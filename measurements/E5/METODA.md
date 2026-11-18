# E5: merită formatul propriu?

Metoda v1, scrisă 2026-11-17, înainte de rularea de aici. Tabelul de
conversie din `corpus/README.md` (2026-10-26) există deja; el compară ieșirea
cu fișierul sursă, care la EPUB e deja arhivat. Aici comparația e cu textul
simplu și cu variantele formatului.

**Întrebarea.** Formatul IBV (blocuri DEFLATE, index de blocuri, despărțirea
făcută pe PC) ține cărțile mici, le deschide ieftin și dă orice poziție cu RAM
mărginit? Ce mărime de bloc e cea bună? Și cât costă conversia?

**Predicția.**
1. Secțiunea TEXT comprimată e cel mult 50 % din textul ei necomprimat, pe
   fiecare carte în proză.
2. Cratimele moi (despărțirea făcută pe PC) adaugă cel mult 10 % la fișierul
   comprimat.
3. Deschiderea: antetul, directorul și secțiunile mici (META, TOC, BLKS)
   încap în primii 4 KB la cel puțin 9 din 12 cărți. Excepțiile prezise sunt
   cărțile cu cuprins foarte lung (Whitman, Eminescu).
4. Mărimea blocului: trecerea de la 16 KB la 32 KB câștigă sub 5 % din
   fișier, iar coborârea la 4 KB costă peste 15 %. Deci 16 KB rămâne
   implicit.
5. Conversia pe PC ține sub 5 s pe fiecare carte.

**Montajul.** Convertorul (`tools/converter`), importat direct din Python, ca
să se măsoare fără pornirea interpretorului. Laptopul de dezvoltare, fără alte
sarcini grele. Partea de pe dispozitiv (timpul de deschidere, de salt la o
poziție și de decomprimare a unui bloc cu `tinfl` din ROM, RAM-ul ocupat) se
măsoară pe prototip, cu jurnalul `perf`, și se adaugă aici atunci.

**Corpusul.** Cele 12 cărți distincte din `corpus/files` (fără copiile
`libru-dubrovsky-cp1251.txt`, `pg2701.txt`, `pg64597.txt`). Proza e
subsetul din E6. Planul cere ~20 de cărți (MEAS-1); când corpusul crește,
experimentul se rulează din nou, cu aceleași scripturi.

**Variabile.** Independente: compresia (DEFLATE, stocat); coperta (cu, fără);
despărțirea (pornită, oprită); mărimea țintă a blocului (4, 8, 16, 32 KB).
Restul convertorului rămâne la implicit.

**Procedura.** `run.py`: fiecare carte în fiecare variantă, în memorie;
fișierul rezultat se validează (`ibv.validate`) și se măsoară pe secțiuni.
Timpul de conversie: 3 rulări la setările implicite, mediana.

**Metrici.**
- mărimea fișierului și a fiecărei secțiuni; textul simplu = suma octeților
  UTF-8 din înregistrări, fără cratimele moi;
- raportul de compresie al secțiunii TEXT;
- octeții până la sfârșitul ultimei secțiuni mici (deschiderea);
- pe bloc: octeții citiți de pe card (stocat) și RAM-ul pentru decomprimare
  (brut); mediana și maximul;
- timpul de conversie.

**Criteriul.** Fiecare predicție are pragul ei mai sus. Limita formatului
(un bloc brut ≤ 32 KB) trebuie respectată la toate variantele; altfel e o
eroare a convertorului.

**Ce poate strica măsurătoarea.** Timpul pe PC depinde de mașină: se notează
procesorul. Mărimea fișierului EPUB nu e o bază bună (e arhivă și are
imagini): de aceea comparația principală e cu textul simplu.

**Date.** `date/carti.csv` (carte × variantă), `date/blocuri.csv` (carte ×
mărimea blocului), `date/timp.csv`, `date/versiune.txt`.

## Metoda v2 (2026-11-18), scrisă înainte de a doua rulare

Corpusul a crescut la 20 de cărți distincte (MEAS-1, `corpus/README.md`), deci
experimentul se rulează din nou, cu aceleași scripturi. Se schimbă doar:

- **Cărțile.** Se adaugă 8: `libru-geroi.fb2.zip` (Lermontov, primul FB2),
  `pg1524` (*Hamlet*), `pg4300` (*Ulysses*), `ws-amintiri`, `ws-ion`,
  `ws-kapitanskaya`, `ws-luceafarul`, `ws-moara-cu-noroc`. Se exclud și
  copiile noi (`libru-dubrovsky.fb2.zip`, `pg64597-cp1250.txt`,
  `pg64597-iso8859-16.txt`).
- **Proza** e cea din E6 v2: cele 8 de dinainte, plus Lermontov, *Ulysses*,
  Creangă, Rebreanu, Pușkin și Slavici (Wikisource): 14 cărți. În afara
  prozei: *Hamlet* (teatru, mai ales versuri) și *Luceafărul* (versuri).
- **Predicția 3** se scalează: deschiderea în 4 KB la cel puțin 3/4 din cărți
  (pragul vechi, 9 din 12, era tot 3/4). Celelalte praguri rămân neschimbate,
  inclusiv cele două care n-au trecut la v1 (2 și 4): v2 le verifică pe un
  corpus mai mare, nu le mută.

Predicțiile sunt făcute știind tabelul de conversie din `corpus/README.md`
(numărul de intrări din cuprins, mărimile fișierelor), dar nu octeții
secțiunilor și nici rezultatele pe variante.

Rezultatele v1 (12 cărți, rulate la commitul `0f30481` pe 2026-11-17) rămân
în istoricul git; rezumatul lor e în REZULTATE.md.
