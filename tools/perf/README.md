# perf: analiza jurnalelor de măsurători

Citește fișierele `perf/NNNN.csv` scrise de cititor (FW-13) și scoate cifrele pentru E1, E2, E4 și E6. Doar biblioteca standard Python.

```sh
tools/perf/perf.py /run/media/$USER/SD/perf/0003.csv
tools/perf/perf.py perf/*.csv --pagini pagini.csv --baterie baterie.csv
```

- **E1**: reîmprospătările, parțiale și complete: scrierea SPI, BUSY, totalul, zona parțială în kpx.
- **E2**: întoarcerea paginii pe bucăți, în ms: intrarea (frontul butonului → apăsarea), pregătirea, paginarea, desenarea, SPI, panoul și totalul de la front la BUSY eliberat. Separat pentru toate, parțiale și complete; medie, median, p95, min, max. Întoarcerile automate n-au front, deci totalul lor pornește de la eveniment.
- **E4**: pentru fiecare fișier cu întoarceri automate, câte pagini, câte ore, tensiunea de la început și de la sfârșit și cum s-a terminat (baterie descărcată, adormit sau jurnal tăiat).
- **E6**: ms pe pagină, la lectură și la index.
- Costul jurnalului: câte scrieri pe card, cât au durat, câte rânduri s-au pierdut.

`--pagini` scrie un rând pe întoarcere (E2), `--baterie` curba ore–mV–pagini (E4), ca CSV pentru grafice.

## Cum se fac măsurătorile

1. Pe card, un dosar gol `/perf`. De la următoarea pornire, fiecare pornire are fișierul ei.
2. **E1, E2**: carte deschisă, din meniul cărții „Întoarcere automată: la 5 s”, câteva sute de pagini. Apoi aceeași carte cu butonul ÎNAINTE, ca să intre și intrarea. Pentru frontul exact (citirea butoanelor are 10 ms) și pentru BUSY se leagă în paralel analizorul logic.
3. **E4**: bateria încărcată complet, „la 1 min”, se lasă până se oprește singur sub 3,40 V. Bateria se scrie în jurnal la fiecare minut.
4. Cardul în PC, `perf.py` pe fișierele noi.

Pe `sim ui` merge la fel, cu dosarul `CARD/perf`, dar panoul are acolo durata 0 și ceasul e simulat: e bun doar pentru încercat analiza (`sim/test/run.sh` o face).

Teste: `cd tools/perf && python3 -m unittest -v test_perf`.
