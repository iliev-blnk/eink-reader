# E8: încărcarea bateriei (opțional)

Metoda v1, scrisă 2026-11-17, înainte de orice măsurătoare.

**Întrebarea.** Cât durează încărcarea de la pragul de oprire (3,40 V) până la
plin pe placa proprie și cum arată curba curentului?

**Predicția.** BQ24074 cu R4 = 1,8 kΩ pe ISET: I = K_ISET / R_ISET =
890 AΩ / 1,8 kΩ ≈ 0,49 A (K_ISET tipic din foaia de catalog). Faza de curent
constant aduce bateria de 1500 mAh la ~80 % în ~2,5 h, iar tensiunea
constantă mai adaugă ~1 h: totalul prezis ~3,5 h, cu ipoteza ±25 %.

**Montajul.** Testerul USB-C între încărcător (5 V, ≥ 1 A) și dispozitiv;
fotografiat sau citit la fiecare minut. Dacă testerul are jurnal, se folosește
acela. Dispozitivul în somn adânc în timpul încărcării.

**Variabile.** Doar timpul. Controlate: același încărcător, același cablu,
temperatura camerei.

**Procedura.** Bateria descărcată până la oprire (după E4), încărcarea
pornește, se notează curentul și tensiunea pe VBUS la fiecare minut până când
LED-ul de încărcare se stinge.

**Metrici.** Timpul total, timpul în curent constant, curentul maxim, energia
din USB (Wh).

**Eșantion.** O încărcare completă.

**Criteriul.** Timpul în ±25 % din predicție; placa nu se încălzește peste
45 °C (termometru sau camera termică, dacă există).

**Ce poate strica măsurătoarea.** Căderea pe cablu; testerul adaugă și el
rezistență. Se notează cablul.

**Date.** `date/incarcare.csv`, `date/note.md`.
