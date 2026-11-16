# Emulatorul: firmware-ul adevărat, fără hardware

Aici rulează binarul care se scrie în cip (`firmware/build`), neschimbat, pe o
placă emulată: ESP32-S3 din QEMU-ul Espressif plus ce e legat de el pe prototip.
Panoul e-paper, cardul SD, butoanele, divizorul bateriei și somnul adânc sunt
modele scrise pentru proiect. După fiecare reîmprospătare emulatorul scrie ce
s-ar vedea pe ecran, iar cadrele se compară pixel cu pixel cu cele ale
simulatorului de pe PC (`sim ui`) pentru același card și aceleași apăsări.

Simulatorul rulează doar nucleul portabil (fonturi, cititorul IBV, paginarea,
interfața) cu un panou și un card închipuite. Emulatorul rulează și tot ce e
dedesubt: driverul panoului, SPI cu DMA, FatFs peste driverul de card, butoanele
cu întreruperi, NVS în flash, ADC-ul, somnul adânc și trezirea. Dacă cele două
dau aceleași cadre, straturile de sub nucleu și-au făcut treaba.

## Folosire

    emu/build.sh                      # o dată: descarcă și compilează QEMU (câteva minute)
    (cd firmware && idf.py build)     # firmware-ul
    emu/test.sh                       # toate verificările, ~35 s

O rulare de mână:

    sim/make_card.sh /tmp/card                         # corpusul și fonturile
    emu/run.sh /tmp/card -o /tmp/rulare --keys "m n n n n m n n n M n"

În `/tmp/rulare` ajung cadrele `NNN.pbm`, `jurnal.txt` (ce a făcut placa, cu
timpul în ms), `uart.log` (jurnalul serial al firmware-ului) și `card.img`,
cardul așa cum l-a lăsat firmware-ul. `emu/rdcard.py card.img DOSAR [CALE]`
scoate fișierele de pe el (jurnalele `perf/`, pozițiile, indexurile de pagini).

Scenariul de apăsări are sintaxa din `sim ui`:

| | |
|---|---|
| `n` `p` `m` | ÎNAINTE, ÎNAPOI, MENIU, apăsare scurtă |
| `N` `P` `M` | aceleași, apăsare lungă |
| `i` | așteaptă până se liniștește dispozitivul |
| `t:SEC` | trec SEC secunde fără apăsări |
| `b:MV` | bateria are acum MV milivolți |

`--hold m` ține MENIU apăsat de la pornire până la primul ecran; așa se intră în
aplicația de punere în funcțiune. `--battery MV` dă tensiunea de la pornire.
Fără `--keys` emulatorul merge până la Ctrl-C. Implicit timpul se numără în
instrucțiuni (`-icount`), deci o rulare de un minut de cititor ține câteva
secunde; cu `--realtime` merge după ceasul calculatorului.

## Ce verifică `emu/test.sh`

Rezultatele de la 2026-11-16, firmware-ul de la acea dată:

| verificare | ce face | rezultat |
|---|---|---|
| `ui` | scenariul de interfață din `sim/test/run.sh`, cu două adormiri și treziri | 22 de cadre, 22 identice |
| `auto` | întoarcerea automată a paginii un minut, bateria care scade, oprirea | 29 de cadre, 29 identice |
| `carte` | cardul cu cele 15 cărți ale corpusului și fonturile dispozitivului: biblioteca, *Moby Dick* (indexul de 3699 de pagini se construiește pe cipul emulat), somn și trezire în carte, meniul cărții | 41 de cadre, 41 identice |
| `perf` | jurnalul de măsurători scris de firmware pe cardul emulat, citit de `tools/perf/perf.py` | 13 întoarceri (12 automate), oprirea de baterie |
| `bringup` | aplicația de punere în funcțiune: numărătoare, redesenare, somn, trezire din buton | 9 cadre, fără avertismente |
| `repetare` | același scenariu a doua oară | aceleași evenimente, aceleași cadre |

Un test trece doar dacă toate cadrele sunt identice **și** jurnalul emulatorului
nu are niciun rând „ATENȚIE”. Modelele se plâng când firmware-ul face ceva ce
hardware-ul adevărat n-ar ierta, chiar dacă imaginea iese bine: o comandă
trimisă panoului cât BUSY e jos, mai puțini octeți decât cere fereastra, o
reîmprospătare cu RAM-ul nescris, o comandă în somn adânc, o tranzacție SPI
pornită peste alta, SPI rutat pe alți pini decât ai plăcii.

### Prinde greșeli adevărate?

Șase greșeli puse dinadins în drivere, fiecare într-un firmware separat, rulate
pe scenariul `ui`:

| greșeala | ce a văzut emulatorul | prinsă |
|---|---|---|
| după o reîmprospătare parțială, RAM-ul „vechi” nu mai e adus la zi | 13 cadre din 22 diferă (urme pe ecran) | da |
| reîmprospătarea completă nu scrie RAM-ul „vechi” | 9 cadre diferă, 16 avertismente | da |
| fereastra parțială cu un rând în plus | cadrele ies la fel, 44 de avertismente (octeți mai puțini decât fereastra) | da |
| BUSY nu mai e așteptat | cadrele ies la fel, 69 de avertismente (comenzi cu BUSY jos) | da |
| butoanele fără rezistențe de pull-up | pinii în aer se citesc 0, firmware-ul crede că MENIU e ținut la pornire și intră în aplicația de punere în funcțiune; 1 cadru din 22 | da |
| reîmprospătarea completă nu mai trezește panoul adormit | nimic | **nu** |

A șasea nu e prinsă pentru că drumul acela nu e parcurs de niciun scenariu:
firmware-ul adoarme panoul doar chiar înainte de somnul adânc al cipului, iar
trezirea trece prin reset și `epd_init`. Ramura `s.asleep` din
`epd_full_refresh` e deci cod pe care cititorul de azi nu-l atinge.

Două dintre cele cinci prinse dau cadre corecte în emulator și ar fi dat cadre
greșite sau un panou blocat doar pe hardware; le prind avertismentele, nu
comparația de imagini.

### O îmbunătățire făcută firmware-ului

Rulările au arătat la fiecare pornire `W gpio: conflict found for GPIO[14]`.
Firmware-ul punea CS-ul cardului ca ieșire cu `gpio_config`, care trece pinul ca
ocupat, iar driverul SD îl configura apoi a doua oară. Acum se folosește
`gpio_set_direction` și avertismentul de la pornire a dispărut. A rămas unul la
adormire, care vine din ESP-IDF: la demontarea cardului driverul SD își trece
propriul pin pe intrare cu `gpio_config`. Apare și pe hardware și e inofensiv.

## Ce e modelat

Sursele sunt în `qemu/hw-ibv/` (~2400 de rânduri de C), fiecare cu presupunerile
ei scrise în antet. `qemu/integrare.patch` le leagă de QEMU.

| fișier | ce e |
|---|---|
| `ibv_reader.c` | placa (`-M ibv-reader`): legăturile din `firmware/boards/include/board_devkit.h` și scenariul de apăsări |
| `ibv_uc8253.c` | panoul e-paper de 3,7" cu controler UC8253, model de comportament: cele două RAM-uri de imagine, fereastra parțială, alimentarea, somnul adânc, linia BUSY |
| `ibv_s3_gpspi.c` | SPI2 al cipului, ca master: fazele de comandă, adresă și date, prin registre sau prin GDMA; o tranzacție durează cât ar dura pe fir |
| `ibv_s3_pins.c` | GPIO, IO_MUX și RTC IO cu niveluri electrice: ieșire, dispozitiv din afară, rezistență internă sau pin în aer |
| `ibv_s3_sens.c` | citirea „oneshot” a ADC-ului SAR, pentru divizorul bateriei |
| `ibv_s3_sleep.c` | somnul cipului, trezirea prin EXT1 și temporizator, resetul cu cauza „somn adânc” |
| `ibv_journal.c` | jurnalul rulării |

Cardul SD e modelul din QEMU, în modul SPI, cu două reparații (în
`integrare.patch`): CMD9 și CMD10 nu erau primite în starea de transfer, iar
CMD13 răspundea cu formatul greșit. Fără ele driverul ESP-IDF nu putea monta
cardul. `emu/mkcard.py` face imaginea de card (MBR + FAT32 cu nume lungi) fără
root și fără unelte din afară.

Reîmprospătarea parțială e modelată diferențial, ca pe panou: se mișcă doar
pixelii la care RAM-ul „vechi” diferă de cel „nou”. De aceea o greșeală de
ținere a evidenței în driver lasă urme și aici.

## Ce nu spune emulatorul

- **Timpii nu sunt măsurători.** Panoul durează cât i s-a spus (3 s complet,
  1 s complet rapid, 350 ms parțial, 45 ms alimentarea), iar procesorul e numărat
  în instrucțiuni, nu în cicluri. Timpii din `jurnal.txt` și din jurnalul `perf`
  arată ordinea și proporțiile, nu ce va ieși pe dispozitiv. E1, E2 și E6 rămân
  de măsurat pe prototip.
- **Timpii nu se repetă la milisecundă.** Cele două nuclee sunt emulate pe rând,
  iar locul în care QEMU trece de la unul la altul diferă puțin între rulări. O
  pornire poate ieși cu până la câteva milisecunde mai lungă sau mai scurtă.
  Evenimentele, ordinea lor și cadrele sunt aceleași la fiecare rulare, iar
  testul `repetare` asta verifică. Am micșorat diferența (trezirea din somn adânc
  lăsa nucleele să meargă puțin înaintea resetului), dar n-am eliminat-o.
- **Panoul e un model de comportament.** Nu are nuanțe, urme reale, temperatură
  sau îmbătrânire. Ce nu se poate afla fără panou e presupus și scris în antet:
  resetul software nu șterge RAM-urile de imagine, resetul hardware și somnul
  adânc le fac de nefolosit.
- **Electric nu se verifică nimic**: consum, zgomot pe linii, niveluri, timpi de
  front, divizorul bateriei (emulatorul dă direct tensiunea pe pin).
- **ADC-ul n-are calibrare**: eFuse-ul emulat e gol, firmware-ul folosește
  formula fără calibrare, iar modelul răspunde exact după ea. De aici rândurile
  `adc_cali` și `eFuse` din `uart.log`.
- `W rtcinit: o_code calibration fail` la pornire vine din modelul RTC al QEMU
  și nu are urmări.
- USB, Wi-Fi și Bluetooth nu sunt (firmware-ul nu le folosește încă).

## Cum e construit

`emu/build.sh` ia QEMU-ul Espressif la o versiune fixă (ramura `esp-develop`,
QEMU 9.2.2, commitul scris în script), aplică `integrare.patch`, copiază
`hw-ibv/` în el și compilează doar ținta `xtensa-softmmu`. Un QEMU deja
descărcat se dă prin `IBV_QEMU_SRC`. Are nevoie de git, ninja, meson, python3,
gcc, glib, pixman și libgcrypt; `compare.py` are nevoie de Pillow.

`emu/run.sh` pune la un loc memoria flash de 16 MB (bootloader, tabela de
partiții, aplicația), un eFuse gol cu versiunea cipului și cardul, apoi pornește
QEMU cu 8 MB de PSRAM (`-m 8M`; cu mai mult, firmware-ul nu-și mai poate mapa
partițiile).

Fișierele din `qemu/` sunt o lucrare derivată din QEMU și au licența
GPL-2.0-or-later.
