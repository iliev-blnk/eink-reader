# Măsurătorile

Fiecare număr din capitolul de evaluare vine dintr-un dosar de aici. Un dosar
pe experiment:

| fișier | ce e |
|---|---|
| `METODA.md` | fișa de metodă: întrebarea, predicția, montajul, procedura, metricile și criteriul. Se scrie **înainte** de rulare și nu se mai schimbă după ce există date. O schimbare de metodă e o versiune nouă a fișei, datată, cu motivul. |
| `date/` | datele brute, așa cum ies din aparat sau din program. Nu se editează de mână. |
| scripturile | rulează experimentul sau analiza; fiecare spune în antet ce citește și ce scrie |
| `figuri/` | graficele generate de scripturi (SVG) |
| `tabele.md` | tabelele, generate de scriptul de analiză (nu se editează de mână) |
| `REZULTATE.md` | nota scurtă cu concluziile, scrisă pe baza tabelelor; intră în lucrare |

`common/svgplot.py` face graficele: SVG simplu, doar biblioteca standard Python.

## Experimentele

| ID | Întrebarea | Unde rulează | Stare |
|---|---|---|---|
| [E1](E1/METODA.md) | Cât durează fiecare mod de reîmprospătare? | prototip | metoda scrisă |
| [E2](E2/METODA.md) | Cât durează întoarcerea unei pagini și unde se duce timpul? | prototip, apoi placa proprie | metoda scrisă |
| [E3](E3/METODA.md) | Cât curent consumă fiecare stare? | prototip, apoi placa proprie | metoda scrisă |
| [E4](E4/METODA.md) | Cât ține de fapt bateria? | prototip, apoi placa proprie | metoda scrisă |
| [E5](E5/METODA.md) | Merită formatul propriu? | PC + dispozitiv | partea de pe PC rulată |
| [E6](E6/METODA.md) | Ajută cu adevărat o rupere mai bună în rânduri? | simulator + dispozitiv | partea din simulator rulată |
| [E7](E7/METODA.md) | Cât de des e nevoie de o reîmprospătare completă? | prototip | metoda scrisă |
| [E8](E8/METODA.md) | Cum se încarcă bateria? (opțional) | placa proprie | metoda scrisă |
| [E9](E9/METODA.md) | Le place cititorilor reali? (opțional) | placa proprie | metoda scrisă |

## Reguli

- Metoda înainte de date. Dacă o predicție iese greșită, rămâne scrisă așa și
  se discută în rezultate.
- Datele brute rămân brute. Ce se exclude se exclude în script, cu motivul
  scris lângă regulă.
- Fiecare rulare notează versiunea firmware-ului sau a uneltei (commitul git),
  placa, fontul și setările.
- Montajul se fotografiază (`docs/photos/`) înainte de fiecare serie de
  măsurători pe hardware.
- Un experiment cu predicția „trece” sau „nu trece” are criteriul scris în fișă,
  cu cifre.
