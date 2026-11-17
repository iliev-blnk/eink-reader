# Placa de prototipare (HW-6, CIORNĂ)

Desenul plăcii de prototipare: unde stă fiecare modul și pe unde merg firele,
ca prototipul să încapă în spatele afișajului WeAct (105,79 × 53,80 mm), prins
în găurile lui de M2.

    python3 hardware/proto/layout.py

Are nevoie de `kicad-cli` (pentru netlista schemei) și `rsvg-convert`.

| ieșire | ce e |
|---|---|
| `out/sablon.pdf` | șablonul, 1:1 pe A4 vedere: pagina 1 fața pieselor, pagina 2 fața de lipire (oglindită). Bara de 50 mm verifică scara la tipar. |
| `out/fata.png`, `out/spate.png` | aceleași pagini, de privit pe ecran |
| `FIRE.md` | piesele, lista numerotată a firelor (de unde, până unde, cât de lung), ce se măsoară înainte și verificările dinainte de prima alimentare |

## Cum e făcut

- Placa: 41 × 21 de găuri de 2,54 mm, tăiată dintr-o placă de 9 × 15 cm. Grila
  e aliniată pe bareta afișajului, ca soclul ei să cadă exact sub pini.
- DevKit-ul stă pe două socluri mamă 1×22, cu USB-ul spre stânga și antena
  spre dreapta, fără nimic sub antenă. Divizorul bateriei (R1, R2, C1) stă
  culcat sub el.
- TP4056 și TPS63020 se lipesc cu bandă dublu adezivă; padurile lor se leagă cu
  fire scurte în găurile de lângă. Cardul stă pe bareta lui de 6 pini.
- Butoanele sunt pe o plăcuță separată, legată la J_BTN cu 4 fire: unde stau se
  hotărăște odată cu carcasa.
- **Legăturile vin din schema KiCad** (`kicad-cli sch export netlist`), nu sunt
  scrise de mână: ce e pe placă nu se poate depărta de schemă.
- Firele (izolate) sunt trasate pe fața de lipire cu A* pe grila de găuri: nu
  trec peste gaura altei rețele, iar încrucișările costă, ca desenul să rămână
  ușor de urmărit. Întâi alimentarea, apoi semnalele.
- Scriptul se oprește cu eroare dacă: un pin nu cade pe o gaură, două piese se
  suprapun, o piesă stă peste un șurub sau sub antenă, o rețea din schemă nu e
  legată complet sau două rețele se ating.

## Ce nu e încă

Mărimile marcate „estimat” (modulul de card, TP4056, TPS63020, SS12D00) sunt
tipice, nu de pe piesele cumpărate. Poziția baretei afișajului e din desenul
WeAct. Lista e în `FIRE.md`, „De măsurat”; după măsurare valorile se trec în
`layout.py`, iar scriptul spune dacă totul mai încape. Nimic nu e încă lipit.
