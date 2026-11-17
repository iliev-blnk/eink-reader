# E2: întoarcerea unei pagini, cap-coadă

Metoda v1, scrisă 2026-11-17, înainte de orice măsurătoare.

**Întrebarea.** Cât trece de la apăsarea butonului ÎNAINTE până când panoul a
terminat pagina nouă și din ce se compune timpul: citirea butonului, paginarea,
desenarea, transferul SPI, panoul?

**Predicția.** Domină panoul. La o întoarcere parțială: intrare ≤ 40 ms
(interogare la 10 ms + debounce de 30 ms), paginare + desenare ≤ 50 ms,
SPI ≤ 15 ms, panoul ~0,3 s, deci totalul median sub 0,5 s. La una completă
(o dată la N), ~3 s. Pagina e gata de desenat din cache-ul de pagini în
majoritatea cazurilor.

**Montajul.** Ca la E1, plus analizorul logic pe pinul butonului ÎNAINTE și
pe BUSY. A doua serie în mai, pe placa proprie, cu același firmware.

**Variabile.** Independentă: placa (prototip, placa proprie); felul
reîmprospătării (parțială, completă); somn ușor sau activ între pagini (după
FW-7). Controlate: cartea, fontul și mărimea, N complet = 10.

**Procedura.**
1. Card cu `/perf`, `pg1342.ibv` de la început, index construit.
2. 200 de pagini apăsând ÎNAINTE de mână, la cel puțin 3 s una de alta.
3. Aceeași serie cu întoarcere automată la 5 s (fără intrare), pentru
   descompunere fără variația mâinii.
4. Analizorul înregistrează frontul butonului și BUSY pe primele 50 de
   apăsări.

**Metrici.** Din `perf.py` (E2, `--pagini`): intrare, pregătire, paginare,
desenare, SPI, panou, total; medie, mediană, p95, separat pentru parțiale și
complete. Din analizor: frontul real → BUSY eliberat.

**Eșantion.** ≥ 150 de întoarceri parțiale și ≥ 15 complete pe fiecare placă.

**Criteriul.** Predicția trece dacă totalul median al întoarcerilor parțiale
e sub 0,5 s și p95 sub 0,8 s. Componentele de calcul (paginare + desenare)
trebuie să fie sub 10 % din total; altfel se caută cauza în E6.

**Ce poate strica măsurătoarea.** Apăsările prea dese (coada de evenimente):
de aceea 3 s între ele. Primele pagini după deschidere (indexul încă se
construiește): se exclud primele 10 pagini, în script. Precizia frontului din
jurnal e de 10 ms; analizorul dă frontul exact.

**Date.** `date/perf-NNNN.csv`, `date/analizor-*.csv`, `date/note.md` (placa,
commitul firmware-ului, tensiunea).
