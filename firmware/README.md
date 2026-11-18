# Firmware

Proiect ESP-IDF v6.1 pentru ESP32-S3. Compilează cititorul: biblioteca, lectorul, setările, somnul și trezirea (`components/ui`, aceeași interfață pe care o rulează `sim ui`). **Cu MENIU ținut apăsat la o pornire la rece** pornește în schimb aplicația de punere în funcțiune din Faza 0, care verifică fiecare parte a prototipului și arată rezultatele pe ecran.

```
firmware/
  boards/              hărțile pinilor, câte un header pe placă; placa se alege în menuconfig
  components/core/     C portabil (fonturi, grafică pe 1 bit, cărți IBV, paginare); sim/ compilează aceleași fișiere
  components/ui/       C portabil: managerul de aplicații și aplicațiile (acasă, biblioteca, lectorul, setările…)
  components/drivers/  epd_uc8253 (ecranul), sdcard, butoane, baterie, fuelgauge (MAX17048, doar pe placa proprie)
  main/                main.c: cititorul pe placă (panou, card, NVS, somn); bringup.c: punerea în funcțiune;
                       board_io.c: șinele cardului și panoului și magistralele lor SPI, comune celor două
```

## Compilare

ESP-IDF stă în `~/esp/esp-idf`. Se încarcă o dată pe terminal:

```sh
. ~/esp/esp-idf/export.sh
cd ~/DEV/eink-reader/firmware
idf.py build
```

Ținta ESP32-S3 și setările care contează (flash de 16 MB, PSRAM octal, tabela de partiții proprie) vin din `sdkconfig.defaults`. `sdkconfig` e generat și nu e urmărit în git. Pentru o pornire curată: `rm sdkconfig && idf.py set-target esp32s3`.

`idf.py build` compilează pentru prototipul de pe DevKit. Pentru placa proprie v1 (`hardware/pcb`) se compilează în alt director, cu `sdkconfig.pcb_v1` peste setările de bază:

```sh
idf.py -B build-pcb -D SDKCONFIG=build-pcb/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.pcb_v1" build
idf.py -B build-pcb -p /dev/ttyACM0 flash monitor
```

Placa se poate alege și din `idf.py menuconfig`, la „Placa cititorului e-ink”.

## Scrierea în flash

DevKitC-1 are două porturi USB-C. Se folosește cel marcat **UART**: rămâne conectat cât cipul e în somn adânc, deci se vede trezirea. Portul **USB** (USB nativ) poate și el scrie flash-ul, dar dispare de pe magistrală în timpul somnului.

1. O singură dată, pe Arch: `sudo usermod -aG uucp $USER`, apoi ieșire din sesiune și intrare din nou. Fără asta portul răspunde „permission denied”.
2. Găsirea portului: `ls /dev/ttyUSB* /dev/ttyACM*` înainte și după conectarea plăcii. Intrarea nouă e portul.
3. Scrierea și urmărirea jurnalului:

   ```sh
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

   Ieșirea din monitor: `Ctrl+]`. Dacă scrierea nu se poate conecta: se ține **BOOT**, se apasă scurt **RST**, apoi se eliberează **BOOT**.

## Conexiuni (prototip, `boards/include/board_devkit.h`)

| Semnal | GPIO | Note |
|---|---|---|
| SPI SCK / MOSI / MISO | 12 / 11 / 13 | comune pentru ecran și cardul SD |
| Ecran CS / DC / RST / BUSY | 10 / 9 / 8 / 18 | modulul ecranului trebuie setat pe SPI cu 4 fire |
| Card SD CS | 14 | |
| Butoane PREV / NEXT / MENU | 4 / 5 / 6 | fiecare buton leagă pinul la GND; rezistențele pull-up sunt interne |
| Măsurarea bateriei | 1 | divizor 100k/100k de la baterie, 100 nF la GND |

Totul merge pe **3V3**, niciodată pe 5V.

## Placa proprie v1 (`boards/include/board_pcb_v1.h`)

Pinii vin din `hardware/pcb/circuit.py`. `hardware/pcb/verifica.py` compară antetul cu placa la fiecare `make.sh` și dă eroare la orice pin diferit sau lipsă.

| Semnal | GPIO | Note |
|---|---|---|
| Card SCK / MOSI / MISO / CS | 12 / 11 / 13 / 14 | SPI2, ca pe DevKit |
| Ecran SCK / MOSI | 47 / 48 | SPI3: panoul are magistrala lui |
| Ecran CS / DC / RST / BUSY | 10 / 9 / 8 / 18 | ca pe DevKit |
| Alimentarea cardului / ecranului | 15 / 21 | comutatoarele U4 / U5 (TPS22917), 1 = pornit |
| Butoane PREV / NEXT / MENU | 4 / 5 / 6 | ca pe DevKit |
| VBUS (cablul USB pus) | 2 | divizor 470k/470k, 100 nF; discul USB vede când e scos cablul |
| Măsurarea bateriei | 1 | divizor 1M/1M, 100 nF |
| Indicatorul MAX17048: SDA / SCL / ALRT | 16 / 17 / 7 | pull-up-uri pe placă; ALRT nefolosit încă |

**Alimentarea cardului și a panoului.** La pornire, `board_power_on()` pornește cele două comutatoare, așteaptă 10 ms să urce tensiunea, apoi eliberează pinii. Înainte de somnul adânc, după ce panoul a primit comanda de somn și cardul e demontat, `board_power_off()` pune pe 0 toate liniile spre card și panou, oprește pull-up-ul de pe BUSY și abia apoi oprește șinele. Pinii rămân ținuți așa în somn (`gpio_hold_en`, iar pentru IO47/IO48, care nu sunt pini RTC, și `gpio_deep_sleep_hold_en`). O linie lăsată în 1 ar alimenta piesa oprită prin diodele ei de protecție (`hardware/pcb/REVIEW.md`). Pe DevKit, unde nu sunt comutatoare, funcțiile nu fac nimic.

**Somnul ușor (FW-7).** Între evenimente cipul doarme ușor, automat (`esp_pm` cu tickless idle, `sdkconfig.defaults`). Frecvența e fixă, la 160 MHz. Cât toate butoanele sunt libere, taskul lor așteaptă o întrerupere în loc să interogheze, iar butoanele sunt surse de trezire GPIO doar cât timp așteaptă. BUSY-ul panoului trezește cipul la sfârșitul unei reîmprospătări. Discul USB oprește somnul ușor cât e pornit. ESP-IDF deconectează în somn toți pinii. Liniile spre card și panou și comutatoarele șinelor sunt scoase din regula asta de `board_light_sleep_pins()`, ca să nu cadă în timpul unei reîmprospătări. Diagnosticul arată cât din timpul de la pornire a dormit cipul. Somnul adânc oprește întâi temporizatorul lăsat pornit de somnul ușor, altfel cipul s-ar trezi singur imediat.

Fără tensiune, panoul uită imaginea din memoria lui, dar nu și pe cea de pe ecran. Prima reîmprospătare după trezire e deci una completă, ca după orice pornire; driverul o face oricum după somn.

**Indicatorul de baterie.** Dacă MAX17048 răspunde, tensiunea afișată vine de la el, iar ecranul de diagnostic și punerea în funcțiune arată și procentul. Altfel rămâne ADC-ul. Firmware-ul doar citește registrele: nu scrie MODE, ca indicatorul să treacă singur în hibernare (~3 µA) în loc să se oprească.

**WiFi.** Firmware-ul nu are încă WiFi. Antetul are `BOARD_WIFI_MAX_TX_QDBM` (13 dBm), limita de emisie pentru regulatorul de ~400 mA (`hardware/pcb/REVIEW.md`, punctul 4), de aplicat cu `esp_wifi_set_max_tx_power` când va exista.

Nimic din acestea nu a rulat pe placă: placa nu e comandată. Codul compilează pentru ambele plăci, iar emulatorul (`emu/test.sh`) rulează varianta DevKit.

## Butoanele cititorului

- **ÎNAINTE / ÎNAPOI**: pagina următoare / precedentă, sau elementul următor / precedent dintr-o listă. Apăsate lung în lector: capitolul următor / precedent. Într-o listă, ÎNAINTE lung sare la pagina următoare a listei, iar ÎNAPOI lung iese din ea.
- **MENIU**: deschide elementul ales; în lector deschide meniul cărții (cuprins, semn de carte, mărimea textului, setări, bibliotecă, întoarcerea automată, diagnostic).
- **MENIU ținut apăsat**: somn adânc, cu coperta cărții pe ecran. Orice buton trezește dispozitivul la aceeași pagină. Somnul vine și singur după inactivitate (setarea „Somn după”).
- **Întors (setarea „Orientarea”)**: cititorul se ține cu butoanele laterale în dreapta, iar imaginea e rotită cu 180°. Butonul lateral de jos rămâne ÎNAINTE, deci cele două își schimbă rolurile; MENIU, acum jos, rămâne MENIU. Rotirea se face în interfață, la trimiterea cadrului spre panou, așa că și ecranul de somn și cel de baterie descărcată apar întoarse. Punerea în funcțiune nu se rotește.
- **Cardul scos**: biblioteca spune „Niciun card”. Cardul pus la loc se montează la următoarea intrare în bibliotecă, fără repornire.

## Conectare la PC (copierea cărților prin USB)

Din meniul principal, **Conectare la PC**: cardul SD apare pe calculator ca un stick (USB nativ, TinyUSB MSC). Cât timp ecranul e deschis, cardul e al calculatorului: cititorul îl demontează, nu scrie nimic pe el și nu adoarme de la sine. Se copiază cărțile `.ibv` în `books/`, apoi se scoate discul în siguranță de pe calculator (Eject): cititorul ia cardul înapoi, recitește biblioteca și revine singur la meniu. MENIU iese oricând; dacă discul e încă montat pe calculator, cere o a doua apăsare. Firmware-ul nu formatează cardul niciodată: un card fără FAT apare pe calculator așa cum e.

Pe prototip se folosește portul marcat **USB** al DevKit-ului (cel nativ, GPIO19/20), nu UART. Placa se alimentează atunci din cablu, deci **SW1 trebuie să fie oprit**, ca ieșirea TPS63020 să nu fie legată de regulatorul DevKit-ului. DevKit-ul nu are divizor pe VBUS (`BOARD_VBUS_SENSE_GPIO` = −1), așa că scoaterea cablului apare doar ca magistrală în repaus („Calculatorul nu răspunde”), iar ecranul se închide cu MENIU.

Încă neîncercat pe hardware: emulatorul nu are USB, iar în simulator discul e fals (`u:STARE` în scenariu). De verificat pe prototip: cum îl vede calculatorul, viteza de copiere, scoaterea în siguranță pe Linux și Windows și dacă portul USB-Serial-JTAG revine după ieșire.

Pe card: cărțile `.ibv` în rădăcină sau în `/books`, fonturile în `/fonts` (din `tools/fonttool/out/fam`). Dispozitivul își scrie starea în `/.ibv/` (specificația, secțiunea 6). Setările sunt în NVS. Pe UART ies linii `PERF,…` pentru apăsări, paginare, reîmprospătări și somn.

## Ce face aplicația de punere în funcțiune

Se pornește ținând MENIU apăsat în timp ce placa se alimentează sau după RST. La pornire citește cipul, flash-ul și PSRAM-ul, tensiunea bateriei și cardul SD, apoi desenează acest ecran cu o reîmprospătare completă. Simulatorul de pe PC desenează același ecran: vezi `sim/`.

- **PREV / NEXT**: numără în jos / în sus și redesenează doar caseta contorului cu o reîmprospătare parțială. O apăsare lungă sare cu 10. Caseta arată cât a durat reîmprospătarea anterioară.
- **MENU**: recitește bateria, reîncearcă cardul SD dacă a eșuat și redesenează tot cu o reîmprospătare completă.
- **MENU ținut apăsat**: arată „Sleeping”, pune ecranul și cipul în somn adânc. Orice buton îl trezește. „Wake” de pe ecran arată apoi butonul care l-a trezit, plus un contor de porniri care supraviețuiește somnului.

Fiecare pornire adaugă un rând în `bringup.txt` pe cardul SD. **Aplicația nu formatează niciodată cardul.** Cardul trebuie să fie FAT32, pentru că exFAT nu e suportat. Cardurile de peste 32 GB vin de obicei în exFAT, deci acestea se reformatează mai întâi în FAT32.

## Rânduri de cronometrare (experimentul E1)

Fiecare reîmprospătare tipărește în jurnalul serial un rând ușor de prelucrat automat:

```
PERF,epd_partial,x=8,y=282,w=224,h=56,write_us=…,on_us=…,refresh_us=…,off_us=…,total_us=…
PERF,epd_full,…
PERF,sd_mount,ok=1,us=…
PERF,battery,mv=…
```

Salvarea jurnalului într-un fișier: în monitor, `Ctrl+T` apoi `Ctrl+L`. Se scrie `log.eink-reader.<timp>.txt` în dosarul curent. Aceleași taste din nou opresc salvarea. Apoi se filtrează cu `grep ^PERF log.eink-reader.*.txt`.

## Jurnalul de măsurători (E1, E2, E4, E6)

Dacă pe card există dosarul `/perf`, fiecare pornire a cititorului scrie fișierul următor, `perf/0001.csv`, `perf/0002.csv`…, cu rândurile `t_us,event,a,b`. Timpul e `esp_timer`, în µs de la pornire. Evenimentele: frontul butonului (`btn_edge`), apăsarea (`key`), paginarea (`layout_begin`, `layout_end`), fiecare reîmprospătare (`refresh` cu durata totală, `refresh_io` cu scrierea SPI și BUSY, `refresh_rect`), bateria la fiecare minut, întoarcerile automate, indexul în fundal, somnul (cu `light_sleep`: ms dormiți ușor și de câte ori, scris la intrarea în somnul adânc). Rândurile stau în memorie (256) și se scriu pe card doar când bucla așteaptă o apăsare, ca scrierea să nu intre în timpii măsurați. Fiecare scriere are și ea un rând (`perf_flush`, câte rânduri și cât a durat). Fără dosarul `/perf` nu se scrie nimic. Starea jurnalului apare în Diagnostic.

**Întoarcerea automată**: din meniul cărții, „Întoarcere automată”, cu MENIU ciclezi între oprită, la 5 s și la 1 min. Pornește când ieși din meniu, cu „auto” în subsol. De la ultima pagină o ia de la început. Cât e pornită, dispozitivul nu adoarme de la sine, dar se oprește tot sub 3,40 V, adică la capătul experimentului E4. Prima apăsare în lector o oprește și nu face altceva. Nu se păstrează peste somn.

- **E1, E2**: la 5 s, câteva sute de pagini, apoi și cu butonul. Pentru timpii exacți ai frontului și ai BUSY se pune în paralel analizorul logic pe pinul butonului și pe BUSY: frontul din jurnal e cu precizia citirii butoanelor (10 ms).
- **E4**: încărcat complet, la 1 min, până se oprește singur.

Analiza: `tools/perf/perf.py /sd/perf/0003.csv --pagini pagini.csv --baterie baterie.csv` (vezi `tools/perf/README.md`).

## Dacă ceva nu merge

- **„e-paper not answering” în jurnal**: ecranul nu a coborât niciodată BUSY. Se verifică BUSY, RST, CS, DC, SCK și MOSI după tabel, 3V3 și GND, și că padul de interfață al modulului e setat pe SPI cu 4 fire.
- **„BUSY still low”**: ecranul a început o reîmprospătare și nu a terminat-o. De obicei înseamnă o alimentare 3V3 slabă sau un GND slăbit.
- **SD „no card (timeout)”**: se verifică dacă cardul e bine introdus și dacă modulul SD primește 3V3. Unele module au regulator și merg doar de la 5V. Apoi CS=14 și MISO=13.
- **SD „not FAT32 formatted”**: cardul a răspuns, dar nu are volum FAT. Se reformatează în FAT32 pe un PC.
- **Bateria „none (…)”**: nu e nimic pe divizor. E de așteptat cât timp merge de la USB.
