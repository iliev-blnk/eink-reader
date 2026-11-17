# Înregistratorul de curent (MEAS-2)

Pentru E3 (curentul pe stări, energia pe pagină) și E4 (bateria). Un modul INA226 cu șunt de 0,1 Ω stă în serie cu bateria cititorului. O placă ESP32 separată, „înregistratorul”, îl citește la fiecare conversie și trimite eșantioanele pe USB către PC. Pe PC, `ina226.py` le scrie într-un fișier și le analizează.

Starea: firmware-ul se compilează pentru ESP32 și ESP32-C3. Analiza are teste pe date sintetice, iar captura a fost încercată cu un port serial simulat. **Nimic nu a rulat pe hardware**: modulul INA226 și placa de înregistrare nu sunt cumpărate încă (MASTER-PLAN, lista de cumpărături).

## Legăturile

```
baterie + ──── IN+ [INA226] IN- ──── B+ cititor
                        VBS ──┘                 (VBS legat la IN-: tensiunea de pe cititor)
baterie − ──────────── GND ───────── B− cititor ─── GND înregistrator
                        SDA, SCL, ALE ─── înregistrator     VCC ─── 3V3 înregistrator
```

| Semnal | ESP32-WROOM-32D (DevKit) | ESP32-C3 SuperMini |
|---|---|---|
| SDA | GPIO21 | GPIO6 |
| SCL | GPIO22 | GPIO7 |
| ALERT (ALE) | GPIO19 | GPIO5 |
| consola | UART0 prin convertorul de pe placă, 921600 | USB-ul nativ (USB Serial/JTAG) |

Pinii se schimbă în `idf.py menuconfig` → „Înregistratorul INA226”. Pe C3 sunt evitați pinii de configurare la pornire (2, 8, 9). Fără firul ALERT (`-1` în menuconfig), registrul de stare se citește în buclă și marcajul de timp e cel al citirii, nu al conversiei.

Cititorul nu stă pe USB în timpul măsurătorii (E3, „Ce poate strica măsurătoarea”). Înregistratorul e alimentat de PC și are masa comună cu bateria, nu cu USB-ul cititorului.

## Ce măsoară

- Un eșantion la 1,18 ms (~850 pe secundă): șuntul și tensiunea cu câte 588 µs de conversie fiecare, fără mediere. E3 cere cel puțin 500 pe secundă. Alte valori se aleg în menuconfig (timpul de conversie, media).
- Curentul: 2,5 µV pe bit pe 0,1 Ω, deci **25 µA pe bit**, până la ±819 mA. Somnul adânc pe placa proprie (~17,5 µA estimat) e sub rezoluție: se măsoară cu multimetrul (E3).
- Tensiunea: 1,25 mV pe bit.
- Ceasul: `esp_timer` al înregistratorului, în µs, luat în întreruperea de pe ALERT, adică la sfârșitul conversiei. Ceasul cititorului e altul; `pagini` le aliniază.

## Fluxul de pe consolă

```
# ina226,v=1,target=esp32,addr=0x40,shunt_mohm=100,ct=3,avg=0,alert=19,shunt_lsb_nv=2500,bus_lsb_uv=1250,coloane=t_us;shunt;bus
1203311,412,3127
1204487,409,3127
# pierdute,12
```

Rândurile de date sunt registrele brute (`shunt` cu semn). Antetul se repetă la 2 s, ca o captură pornită oricând să aibă constantele. `# pierdute` apare dacă PC-ul nu ține pasul (coada din înregistrator ține ~5 s), `# erori_i2c` dacă o citire a eșuat, iar `# eroare,...` dacă INA226 nu răspunde la pornire.

## Compilarea și scrierea în flash

```sh
. ~/esp/esp-idf/export.sh
cd tools/ina226/firmware
idf.py -B build-esp32   -D SDKCONFIG=build-esp32/sdkconfig   -D IDF_TARGET=esp32   build flash -p /dev/ttyUSB0
idf.py -B build-esp32c3 -D SDKCONFIG=build-esp32c3/sdkconfig -D IDF_TARGET=esp32c3 build flash -p /dev/ttyACM0
```

Fiecare țintă are directorul ei, cu configurația înăuntru (`build-*`, neurmărite în git). Setările comune sunt în `sdkconfig.defaults`, cele ale fiecărei ținte în `sdkconfig.defaults.<țintă>`.

## Pe PC: `ina226.py`

Doar biblioteca standard (portul serial e citit prin `termios`). Testele: `cd tools/ina226 && python3 -m unittest -v test_ina226`.

```sh
# 60 s de date; antetul capturii păstrează nota (placa, starea, commitul firmware-ului cititorului)
tools/ina226/ina226.py captura /dev/ttyUSB0 -o measurements/E3/date/ina226-asteptare.csv --durata 60 \
    --nota "devkit, asteptare, firmware df05a60"

# cifrele: rata, golurile, curentul (medie, median, p99, min, max), tensiunea, puterea, energia, sarcina
tools/ina226/ina226.py rezumat measurements/E3/date/ina226-*.csv --tabel rezumat.csv

# curentul în timp, cu media și maximul pe coloane (vârfurile scurte nu dispar)
tools/ina226/ina226.py figura measurements/E3/date/ina226-pagini.csv -o measurements/E3/figuri/curent.svg --de 10 --pana 20

# energia pe întoarcere: intervalele din jurnalul perf al aceleiași rulări
tools/perf/perf.py --pagini pagini.csv measurements/E3/date/perf-0001.csv
tools/ina226/ina226.py pagini measurements/E3/date/ina226-pagini.csv --pagini pagini.csv -o energie.csv
```

`captura` aruncă tot ce vine înainte de primul antet (mesajele de la pornire) și scrie apoi rândurile exact cum vin. Se oprește după `--durata` secunde din ceasul înregistratorului, nu al PC-ului. Dacă înregistratorul a repornit în timpul capturii (ceasul o ia de la capăt), analiza păstrează doar prima bucată și spune asta.

### Alinierea în `pagini`

Fiecare întoarcere din `perf.py --pagini` începe la frontul butonului (sau la eveniment, la întoarcerea automată) și se termină când BUSY e eliberat, în ceasul cititorului. Se caută decalajul `d` care pune aceste intervale peste curentul cel mai mare:

1. curentul se mediază pe bucăți de 1 ms și i se scade media;
2. pentru fiecare `d`, din 10 în 10 ms, se adună curentul de pe toate intervalele (cu sume prefix);
3. în jurul celui mai bun, se reia la 1 ms.

Calitatea e raportul dintre cel mai bun scor și cel mai bun din afara lobului lui. Lobul e cât cea mai lungă întoarcere, pentru că până acolo intervalele se suprapun cu ele însele. Sub 1,5 scriptul avertizează: atunci decalajul se citește de pe figură și se dă cu `--decalaj-us`. E3 cere aliniere „prin prima reîmprospătare”; corelația face același lucru cu toate întoarcerile deodată, deci rezistă și la una pierdută. Pe datele sintetice din teste găsește decalajul exact.

Energia unei pagini e integrala V·I prin trapeze pe interval, cu capetele interpolate între eșantioane. Un jurnal `perf` acoperă o singură pornire a cititorului: dacă CSV-ul are mai multe, se alege una cu `--fisier-perf`.

## Ce nu e verificat

- Nimic pe hardware. Rata reală, pierderile pe UART la 921600 și pe USB-ul C3, comportamentul firului ALERT și dacă modulul cumpărat are într-adevăr R100 se văd la prima pornire.
- Rata se verifică din `rezumat` (rata, golurile, pierdutele), înainte de orice serie de măsurători.
- Pe ESP32 (DevKit), scrierea pe consolă e prin UART fără driver, deci taskul de scriere așteaptă după fiecare octet. Taskul de citire are prioritate mai mare și nu e afectat, dar dacă apar `# pierdute`, se instalează driverul UART.
