# E4: autonomia reală

Metoda v1, scrisă 2026-11-17, înainte de orice măsurătoare.

**Întrebarea.** Câte pagini și câte ore ține bateria la un ritm de citire
regulat, și cât de bine o prezice modelul făcut din E3?

**Predicția.** Modelul: autonomia = capacitatea / (curentul de așteptare +
energia pe pagină × ritmul). Cu bateria de 1500 mAh, la o pagină pe minut,
predicția numerică se scrie aici din valorile E3, **înainte** de a porni E4.
Ipoteza: măsurătoarea e în ±20 % din predicție.

**Montajul.** Dispozitivul, cu bateria încărcată complet (încărcătorul a
oprit), fără USB. Cititorul are întoarcerea automată „la 1 min” (FW-13), iar
bateria se scrie în jurnal la fiecare minut. Cameră la temperatură obișnuită.

**Variabile.** Independentă: placa (prototip în martie, placa proprie în mai).
Controlate: cartea, fontul, N complet = 10, ritmul de o pagină pe minut.

**Procedura.**
1. Încărcare completă, 30 min de odihnă, se notează tensiunea.
2. Cartea deschisă, „Întoarcere automată: la 1 min”, se lasă neatins.
3. Cititorul se oprește singur sub 3,40 V (ecranul de baterie descărcată).
4. Cardul în PC, `perf.py --baterie`.

**Metrici.** Pagini întoarse, ore, curba ore–mV–pagini; abaterea față de
predicție, în %.

**Eșantion.** O descărcare completă pe placă. Dacă abaterea depășește 20 %,
o a doua descărcare, ca să se vadă dacă e reproductibilă.

**Criteriul.** Modelul trece dacă abaterea e în ±20 %.

**Ce poate strica măsurătoarea.** Jurnalul tăiat de o oprire bruscă (se vede
în `perf.py`: „jurnal tăiat”). Bateria nouă face mai puțin în primele cicluri:
se notează numărul de cicluri. La placa proprie, MAX17048 dă și procentul,
care se compară cu tensiunea.

**Date.** `date/perf-NNNN.csv`, `date/note.md` (tensiunea de pornire,
temperatura, ciclurile bateriei, predicția din E3).
