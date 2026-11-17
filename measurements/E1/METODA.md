# E1: timpii modurilor de reîmprospătare

Metoda v1, scrisă 2026-11-17, înainte de orice măsurătoare.

**Întrebarea.** Cât durează o reîmprospătare completă, una rapidă și una
parțială pe panoul GDEY037T03 (UC8253) și cât din timp e transfer SPI, cât e
panoul?

**Predicția.** Din foaia de catalog și din notele modulului: completă ~3 s,
rapidă ~1 s, parțială ~0,2–0,4 s. Transferul SPI al unui cadru întreg
(12.480 B, două planuri) la 20 MHz: sub 15 ms, deci sub 5 % din orice mod.

**Montajul.** Prototipul (DevKitC-1 + modulul EPD), alimentat din baterie, la
temperatura camerei (notată). Firmware-ul cititorului cu jurnalul `perf`
(FW-13) pornit. Analizorul logic pe BUSY și pe CS, la cel puțin 1 MHz.

**Variabile.** Independentă: modul (complet, rapid, parțial) și, la parțial,
mărimea zonei (un rând, o jumătate, o pagină întreagă). Controlate: aceeași
carte și aceeași pagină de pornire, temperatura, tensiunea bateriei peste
3,7 V.

**Procedura.**
1. Card cu dosarul `/perf`, cartea `pg1342.ibv`, fontul implicit.
2. Întoarcere automată la 5 s, N complet = 10: cel puțin 300 de pagini.
3. Pentru modul rapid și zonele parțiale: aplicația de punere în funcțiune
   (`bringup.c`), 50 de repetări pe fiecare.
4. Analizorul logic înregistrează în paralel primele 50 de reîmprospătări ale
   fiecărui mod.

**Metrici.** Din `tools/perf/perf.py` (secțiunea E1): durata SPI, durata BUSY,
totalul, zona în kpx. Pe fiecare mod: n, medie, mediană, p95, min, max. Din
analizor: durata BUSY pe aceleași 50, ca verificare a jurnalului.

**Eșantion.** n ≥ 50 pe fiecare mod și zonă.

**Criteriul.** Jurnalul e de încredere dacă BUSY din jurnal și cel din
analizor diferă cu cel mult 2 ms (precizia citirii plus întreruperea). Predicția
trece dacă mediana fiecărui mod e în ±30 % din valoarea de mai sus.

**Ce poate strica măsurătoarea.** Temperatura (panoul are forme de undă pe
temperatură): se notează. Bateria aproape descărcată: sub 3,7 V se oprește
seria. Scrierea jurnalului pe card în timpul unei reîmprospătări: jurnalul se
scrie doar când bucla e liberă, iar costul lui apare separat în `perf.py`.

**Date.** `date/perf-NNNN.csv` (copiate de pe card), `date/analizor-*.csv`,
temperatura și tensiunea în `date/note.md`.
