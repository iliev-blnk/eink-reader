# E7: cât de des e nevoie de o reîmprospătare completă

Metoda v1, scrisă 2026-11-17, înainte de orice măsurătoare.

**Întrebarea.** După câte reîmprospătări parțiale la rând urmele paginilor
vechi (ghosting) devin vizibile, ca setarea „completă la fiecare N pagini” să
aibă o valoare implicită justificată?

**Predicția.** Urmele se văd în fotografie de la N ≈ 10 și deranjează la
lectură de la N ≈ 20. Valoarea implicită de acum: N = 10.

**Montajul.** Prototipul pe un suport fix; telefonul pe trepied la 30 cm,
perpendicular pe ecran, cu expunerea, focalizarea și balansul de alb blocate.
O singură lampă, aceeași poziție; fără lumină de la fereastră.

**Variabile.** Independentă: N = 1, 5, 10, 20 de reîmprospătări parțiale de la
ultima completă (valorile setării „Curățare completă”; implicit 10).
Controlate: cartea, fontul, lumina, camera.

**Procedura.**
1. O reîmprospătare completă, apoi fotografia de referință (N = 0) a unei
   pagini fixe.
2. N întoarceri parțiale, înapoi la aceeași pagină, fotografie.
3. Repetat pentru fiecare N, de 3 ori.
4. Analiza: în script, fotografiile se aliniază pe colțurile ecranului; în
   zonele albe ale paginii (marginile și între rânduri) se calculează
   abaterea standard a luminozității față de N = 0.

**Metrici.** Contrastul urmelor = abaterea standard în zonele albe, relativă
la N = 0. Plus o notă vizuală 0–3, dată de 3 oameni care nu știu N.

**Eșantion.** 4 valori de N × 3 repetări; 3 evaluatori.

**Criteriul.** N implicit = cea mai mare valoare la care nota mediană e ≤ 1
(„se vede doar dacă te uiți după ea”).

**Ce poate strica măsurătoarea.** Temperatura (urmele cresc la rece): se
notează. Reflexii pe ecran: lampa la 45°. Compresia JPEG: fotografiile în RAW
sau PNG, dacă telefonul poate.

**Date.** `date/foto/N<nn>-<rep>.png`, `date/note.md`, `date/note-vizuale.csv`.
