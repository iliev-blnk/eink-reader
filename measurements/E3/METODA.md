# E3: curentul fiecărei stări

Metoda v1, scrisă 2026-11-17, înainte de orice măsurătoare.

**Întrebarea.** Cât curent ia dispozitivul din baterie în fiecare stare (activ
în așteptare, paginare, reîmprospătare, somn ușor, somn adânc) și câtă energie
costă o pagină întoarsă?

**Predicția.**
- Prototipul (DevKit): somnul adânc rămâne de ordinul mA, din cauza
  regulatorului și a cipului USB-UART de pe DevKit (estimat 1–5 mA). Activ:
  30–60 mA; în timpul reîmprospătării panoul adaugă câțiva mA.
- Placa proprie: somnul adânc ~17,5 µA (bugetul din `hardware/pcb/README.md`,
  din foile de catalog), acceptabil până la 30 µA.
- Energia pe pagină parțială: sub 30 mJ.

**Montajul.**
- Stările de ordinul mA: modulul INA226 cu șunt de 0,1 Ω în serie cu bateria,
  citit de placa de înregistrare (MEAS-2) la cel puțin 500 de eșantioane pe
  secundă. Rezoluția e ~25 µA, deci nu ajunge pentru somnul adânc pe placa
  proprie.
- Somnul adânc pe placa proprie: multimetrul pe domeniul de µA în serie cu
  bateria, cu un jumper care îl scurtcircuitează la pornire și la trezire
  (căderea pe aparat ar reseta placa).

**Variabile.** Independentă: starea și placa. Controlate: tensiunea bateriei
(3,9–4,0 V pentru toate), cardul introdus, aceeași carte.

**Procedura.**
1. Pentru fiecare stare: 60 s de înregistrare continuă cu INA226; pentru
   întoarceri, 50 de pagini cu întoarcerea automată la 5 s.
2. Somnul adânc: după ce placa a adormit, se scoate jumperul, se așteaptă 60 s
   ca valoarea să se stabilizeze, se notează 5 citiri la 10 s.
3. Marcajele de timp din jurnalul `perf` (FW-13) se aliniază cu înregistrarea
   de curent prin prima reîmprospătare (vârful e ușor de văzut).

**Metrici.** Curentul mediu și maxim pe stare; energia pe pagină = integrala
V·I pe intervalul de la front la BUSY eliberat, mediană pe 50 de pagini.

**Eșantion.** 60 s pe stare; 50 de pagini; 5 citiri de somn adânc.

**Criteriul.** Placa proprie trece dacă somnul adânc e sub 30 µA. Bugetul E4
folosește valorile măsurate aici, nu cele estimate.

**Ce poate strica măsurătoarea.** Căderea pe șunt și pe multimetru (burden
voltage) scade tensiunea plăcii: se notează. LED-ul de alimentare al
modulului EPD, dacă există: se notează dacă e scos. USB-ul conectat: niciodată
în timpul măsurătorii.

**Date.** `date/ina226-<stare>.csv`, `date/somn-adanc.md`, `date/perf-NNNN.csv`.
