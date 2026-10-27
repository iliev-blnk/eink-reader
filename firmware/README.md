# Firmware

Proiect ESP-IDF v6.1 pentru ESP32-S3. Compilează cititorul: biblioteca, lectorul, setările, somnul și trezirea (`components/ui`, aceeași interfață pe care o rulează `sim ui`). **Cu MENIU ținut apăsat la o pornire la rece** pornește în schimb aplicația de punere în funcțiune din Faza 0, care verifică fiecare parte a prototipului și arată rezultatele pe ecran.

```
firmware/
  boards/              hărțile pinilor, câte un header pe placă; placa se alege în menuconfig
  components/core/     C portabil (fonturi, grafică pe 1 bit, cărți IBV, paginare); sim/ compilează aceleași fișiere
  components/ui/       C portabil: managerul de aplicații și aplicațiile (acasă, biblioteca, lectorul, setările…)
  components/drivers/  epd_uc8253 (ecranul), sdcard, butoane, baterie
  main/                main.c: cititorul pe placă (panou, card, NVS, somn); bringup.c: punerea în funcțiune
```

## Compilare

ESP-IDF stă în `~/esp/esp-idf`. Se încarcă o dată pe terminal:

```sh
. ~/esp/esp-idf/export.sh
cd ~/DEV/eink-reader/firmware
idf.py build
```

Ținta ESP32-S3 și setările care contează (flash de 16 MB, PSRAM octal, tabela de partiții proprie) vin din `sdkconfig.defaults`. `sdkconfig` e generat și nu e urmărit în git. Pentru o pornire curată: `rm sdkconfig && idf.py set-target esp32s3`.

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

## Butoanele cititorului

- **ÎNAINTE / ÎNAPOI**: pagina următoare / precedentă, sau elementul următor / precedent dintr-o listă. Apăsate lung în lector: capitolul următor / precedent. Într-o listă, ÎNAINTE lung sare la pagina următoare a listei, iar ÎNAPOI lung iese din ea.
- **MENIU**: deschide elementul ales; în lector deschide meniul cărții (cuprins, semn de carte, mărimea textului, setări, bibliotecă, diagnostic).
- **MENIU ținut apăsat**: somn adânc, cu coperta cărții pe ecran. Orice buton trezește dispozitivul la aceeași pagină. Somnul vine și singur după inactivitate (setarea „Somn după”).

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

## Dacă ceva nu merge

- **„e-paper not answering” în jurnal**: ecranul nu a coborât niciodată BUSY. Se verifică BUSY, RST, CS, DC, SCK și MOSI după tabel, 3V3 și GND, și că padul de interfață al modulului e setat pe SPI cu 4 fire.
- **„BUSY still low”**: ecranul a început o reîmprospătare și nu a terminat-o. De obicei înseamnă o alimentare 3V3 slabă sau un GND slăbit.
- **SD „no card (timeout)”**: se verifică dacă cardul e bine introdus și dacă modulul SD primește 3V3. Unele module au regulator și merg doar de la 5V. Apoi CS=14 și MISO=13.
- **SD „not FAT32 formatted”**: cardul a răspuns, dar nu are volum FAT. Se reformatează în FAT32 pe un PC.
- **Bateria „none (…)”**: nu e nimic pe divizor. E de așteptat cât timp merge de la USB.
