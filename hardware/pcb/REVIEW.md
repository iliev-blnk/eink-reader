# Revizia plăcii v1 (PCB-4)

Revizia schemei și a plăcii, pe lista din `docs/MASTER-PLAN.md`:

- amprentele;
- polaritatea;
- pinii de configurare la pornire;
- USB-ul;
- antena;
- punctele de test.

Fiecare legătură e comparată cu foaia de catalog a piesei. Unde regula se poate scrie în cod, a intrat în `verifica.py`, care rulează acum în `make.sh` după DRC.

**Starea: prima trecere, de citit împreună.** Nimic nu e măsurat pe o placă reală. Concluziile sunt din foi de catalog și din fișierele KiCad. Ce rămâne de hotărât e la „De hotărât la citire”.

Semnele din tabele:

| semn | înseamnă |
|---|---|
| ✔ | corect față de sursă |
| ⚠ | merge, dar e de știut |
| ✎ | schimbat la revizie |
| ? | nu s-a putut verifica aici |

## Ce s-a verificat și cu ce

| Piesa | Sursa |
|---|---|
| ESP32-S3-WROOM-1-N16R8 | foaia modulului v1.8 (tabelul 3-1, pinii; tabelul 6-4, consumul) și ghidul de proiectare hardware Espressif (lista de verificare a schemei, așezarea pe placă) |
| BQ24074 | foaia TI, tabelele de funcționare și cap. 9–10 |
| TPS63900 | foaia TI, tabelele 7-2/7-3 (configurarea), 6.3 (condiții recomandate) și 8-2 (bobine) |
| TPS22917 | foaia TI, descrierea pinilor și 9.3 |
| MAX17048 | foaia Maxim, descrierea pinilor și modurile de somn |
| USBLC6-2SC6 | simbolul KiCad. Foaia ST nu s-a putut descărca: **de comparat la citire.** |
| microSD Molex 104031 | ordinea standard a pinilor microSD |

`python3 verifica.py --pini` scoate tabelul complet pin → rețea pentru toate circuitele, cu numele pinilor din simboluri.

## Ce verifică `verifica.py`

Erorile opresc `make.sh`.

- Fiecare pin neconectat al unui circuit e într-o listă, cu motivul lui. Un pin uitat iese ca eroare.
- ESP32-S3:
  - USB pe GPIO19/20, cu câte o rezistență serie de 22–33 Ω spre mufă;
  - cardul și panoul fără nicio linie comună (în afară de masă);
  - nimic pe GPIO35–37, ocupați de PSRAM-ul octal;
  - tensiunile citite pe ADC1;
  - butoanele și alerta pe pini RTC;
  - GPIO0 cu rezistență spre 3,3 V;
  - EN cu RC 10 kΩ / 1 µF.
- Fiecare pin din `board_pcb_v1.h` e pe rețeaua cu același nume, fiecare semnal al modulului are define-ul lui, iar divizorul bateriei se potrivește cu firmware-ul.
- Polaritatea:
  - LED-urile;
  - USBLC6;
  - rezistențele CC;
  - /CE, ILIM, ISET și TS la încărcător;
  - condensatorul CT la TPS22917.
- Pe placă:
  - distanța condensator–pin;
  - niciun cupru sub antenă, pe niciun strat;
  - lungimile perechii USB;
  - punctele de test pe fiecare șină.

Regulile au fost probate stricând intenționat circuitul în memorie: D+/D− inversate, VBAT_SENSE pe IO35, un LED întors, ILIM scos. Fiecare a ieșit ca eroare.

## Pe blocuri

### USB-C (J1, U1, R1/R2, R17/R18)

| | Ce | Sursa |
|---|---|---|
| ✔ | VBUS pe A4/A9/B4/B9; D+ pe A6/B6, D− pe A7/B7, legate în paralel pe ambele fețe ale fișei | pinii USB4105 |
| ✔ | 5,1 kΩ spre masă pe CC1 și pe CC2: sursa vede un consumator și dă 5 V | specificația USB Type-C |
| ✔ | U1: I/O1 pe 1/6 = D+, I/O2 pe 3/4 = D−, VBUS pe 5, masa pe 2 | simbolul KiCad; ? foaia ST |
| ✔ | D− pe GPIO19, D+ pe GPIO20, singurii pini ai USB-ului intern | WROOM-1, tabelul 3-1 |
| ✎ | R25/R26, 22 Ω (0603, ca restul plăcii), la ~4 mm de pinii 13/14 ai modulului; între ele și mufă rețelele se numesc USB_DP/USB_DN, între ele și modul ESP_DP/ESP_DN. Locurile pentru condensatori, pe care ghidul zice să le „rezervi”, nu sunt puse: la full speed nu se populează de obicei | lista de verificare, USB |
| ⚠ | Trasee lungi: D+ 67 mm cu 9 via, D− 69 mm cu 8 via. **Nu s-au scurtat**: pinii USB sunt pe latura de sus a modulului, mufa e pe marginea de jos, la 40 mm de el; scurtarea înseamnă mutarea mufei și a blocului de încărcare, deci și a carcasei. La full speed (12 Mbit/s, fronturi de ~4 ns) 70 mm nu contează electric. De făcut la v2 | `verifica.py` |
| ⚠ | Divizorul VBUS (100k/100k) pune 2,5 V pe IO2 și când ESP-ul e oprit (SW1 pe OPRIT, cablul pus). Curge ~40 µA prin dioda de protecție a pinului în șina de 3,3 V | calcul |

### Încărcătorul (U2 BQ24074, D1/D2)

| | Ce | Sursa |
|---|---|---|
| ✔ | EN2 = 0, EN1 = 1: modul USB500, maximum 500 mA din USB | tabelul modurilor |
| ✔ | ISET 1,8 kΩ → 890 AΩ / 1,8 kΩ = 0,49 A; e în plaja 590 Ω–8,9 kΩ | ecuația ISET |
| ✔ | ILIM 3,3 kΩ, în plaja 1,1–8 kΩ. Contează doar în modul EN2=1/EN1=0, dar trebuie legat: cu ILIM liber nu încarcă | descrierea ILIM |
| ✔ | TMR și ITERM libere: temporizatoarele implicite și terminarea la 10 % | 9.3.5, 9.3.6 |
| ✔ | /CE pe masă: încărcarea pornită | descrierea pinilor |
| ✔ | TS cu 10 kΩ fix, varianta din foaie pentru celule fără termistor. Riscul e deja în README (fără oprire la cald sau la rece) | 9.3.7 |
| ✔ | C1 1 µF pe IN; C2, C3 4,7 µF pe OUT și BAT, în plajele recomandate | cap. 10 |
| ✔ | OUT e reglat la 4,4 V, sub maximul de 5,5 V al TPS63900 | VO(REG) |
| ✔ | LED-urile: catodul pe /PGOOD și /CHG (open-drain), anodul prin 1 kΩ din VSYS, deci ~2,4 mA, sub limita de 15 mA | `verifica.py` |
| ⚠ | Protecția la supratensiune a BQ24074 e la 10,5 V. Până acolo IO2 ar primi 5,25 V, dar un încărcător C fără PD nu dă peste 5 V | calcul |

### Bateria și indicatorul (J2, JP1, U6 MAX17048)

| | Ce | Sursa |
|---|---|---|
| ✔ | CTG și QSTRT pe masă; VDD din baterie; CELL legat tot la baterie (la MAX17048 nu e legat intern, deci nu strică); EP pe masă | descrierea pinilor |
| ✔ | I2C cu 4,7 kΩ spre +3V3, ALRT cu 100 kΩ (open-drain) | — |
| ⚠ | Cu SW1 pe OPRIT, +3V3 cade și SDA/SCL stau pe 0. MAX17048 trece singur în somn la „bus low” doar dacă MODE.EnSleep = 1, care nu e implicit. **Firmware: nu se setează EnSleep.** | „Sleep Mode” |
| ⚠ | Pe OPRIT rămân alimentate din baterie MAX17048, divizorul R15/R16 (~2 µA) și scurgerea BQ24074: zeci de µA, adică ani pentru o celulă de 1000 mAh. „OPRIT” nu deconectează bateria | calcul |
| ⚠ | Polaritatea JST: pinul 1 e plusul; cablurile vin în ambele feluri (deja în README) | — |

### 3,3 V (U3 TPS63900, L1, SW1)

| | Ce | Sursa |
|---|---|---|
| ✔ | SEL pe masă → VO(1) ales de CFG3; R9 16,2 kΩ → 3,3 V | tabelul 7-3 |
| ✔ | CFG1 R8 36,5 kΩ cu CFG2 pe masă (0 Ω, cod valid) → VO(2) 3,3 V și limita curentului de intrare „nelimitat” | tabelul 7-2 |
| ✔ | Intrarea 1,8–5,5 V acoperă VSYS (3,0–4,4 V) | condiții recomandate |
| ✔ | C5 22 µF 0603 6,3 V e chiar piesa din lista TI (GRM187R60J226ME15). Cerința e minim 10 µF efectiv; la 3,3 V o singură piesă 0603 e la limită, dar C14 (22 µF) e pe aceeași șină | 6.3, 8.2 |
| ✔ | C4 10 µF pe intrare (minim 5 µF efectiv), plus C2 4,7 µF pe aceeași șină | 6.3 |
| ✎ | L1: era Murata DFE252012F-2R2M, care nu e în lista TI și al cărui ISAT nu l-am putut verifica. Acum e DFE252010F-2R2M, din tabelul 8-2: ISAT 3,1 A; „nelimitat” cere minim 2 A. Aceeași amprentă (2,5 × 2,0 mm) | 6.3, tabelul 8-2 |
| ⚠ | Dă 0,4 A (VI ≥ 2 V). WROOM-1 cere vârfuri de 355 mA la 802.11b cu 20,5 dBm, iar Espressif recomandă o sursă de minim 500 mA. Merge fără rezervă, și numai dacă WiFi-ul nu se suprapune cu reîmprospătarea panoului sau cu scrierea pe card | WROOM-1 6-4; ghid, „Power supply” |
| ✔ | Nota din README „R8/R9 de reverificat la PCB-4” se închide | — |

### Comutatoarele de alimentare (U4, U5 TPS22917)

| | Ce | Sursa |
|---|---|---|
| ✔ | CT de 1 nF spre VIN, cum cere foaia; creșterea durează ~3,6 ms | 9.3.2 |
| ✔ | QOD legat la VOUT: descărcarea internă a ieșirii | descrierea QOD |
| ✔ | Cu ieșirea peste intrare, curentul invers e blocat (maxim −1 µA): +3V3 nu e alimentat înapoi | RCB |
| ✔ | R10/R11 de 1 MΩ pe ON țin comutatorul oprit cât ESP-ul pornește. Pinul ON are oricum rezistență internă spre masă, deci sunt redundante, dar nu strică | descrierea ON |
| ✎ | **Alimentarea inversă prin SPI**. În prima trecere cardul și panoul împărțeau SCK și MOSI: cât una din șine era oprită și cealaltă vorbea, liniile împingeau 3,3 V în piesa oprită prin diodele ei de protecție. Acum fiecare are bus-ul lui (punctul 1), deci firmware-ul pune pe 0 liniile piesei pe care o oprește, fără s-o încurce pe cealaltă. `verifica.py` dă eroare dacă J3 și J4 ajung iar să împartă o linie | foaia TPS22917, „reverse current” |

### ESP32-S3 (U7)

| | Ce | Sursa |
|---|---|---|
| ✔ | Pinii modulului corespund GPIO-urilor din tabelul 3-1 (tabelul complet: `verifica.py --pini`) | WROOM-1, 3-1 |
| ✔ | GPIO35–37 neconectați: pe N16R8 îi ocupă PSRAM-ul octal | 3-1, nota b |
| ✔ | GPIO0: 10 kΩ spre 3,3 V (R20) și butonul BOOT (SW5) spre masă | ghid, „Strapping pins” |
| ✔ | GPIO46 liber: rezistența internă îl ține pe 0. Cu GPIO0 = 1 pornește din flash; cu BOOT apăsat intră în descărcare | tabelul „Boot Mode Control” |
| ✔ | GPIO45 liber → 0 → VDD_SPI 3,3 V, cum îi trebuie flash-ului de 3,3 V din N16R8 | ghid, „Digital Power Supply” |
| ✔ | GPIO3 liber: sursa JTAG implicită | ghid |
| ✔ | EN: 10 kΩ spre 3,3 V și 1 µF spre masă (R19/C16), exact recomandarea; plus butonul RESET | ghid, CHIP_PU |
| ✔ | VBAT_SENSE pe GPIO1 și VBUS_SENSE pe GPIO2, ambele pe ADC1 (ADC2 nu merge cât e pornit WiFi-ul) | 3-1 |
| ✔ | Butoanele pe GPIO4–6 și alerta indicatorului pe GPIO7, toți pini RTC: trezesc din somnul adânc (ext1). Rezistențele interne spre 3,3 V le pornește firmware-ul în somn (`buttons.c`) | 3-1 |
| ✔ | I2C pe GPIO16/17 (pinii cristalului de 32 kHz, nefolosit) | 3-1 |
| ✔ | Pinii comuni cu prototipul sunt aceiași ca pe DevKit (cardul pe SPI 11/12/13 cu CS pe 14, afișajul pe 8/9/10/18, butoanele 4/5/6, bateria 1). În plus, panoul are SCK pe GPIO47 și MOSI pe GPIO48, pe a doua gazdă SPI; antetul `board_pcb_v1.h` (PCB-8) le are pe toate | `verifica.py` |
| ✔ | GPIO47/48 merg la 3,3 V: nota c din foaia modulului (1,8 V) e doar pentru ESP32-S3R16V; N16R8 are VDD_SPI de 3,3 V | WROOM-1, tabelul 3-1, nota c |
| ✔ | Decuplarea: C14 22 µF și C15 100 nF la 3 mm de pinul 2 | `verifica.py` |
| ⚠ | J5 (UART): pinul 2 e TX al ESP-ului și merge la RX-ul adaptorului. Pe față nu e serigrafie, deci se ține minte: pinul 1 (pătrat) e masa | — |

### Cardul (J4) și afișajul (J3)

| | Ce | Sursa |
|---|---|---|
| ✔ | microSD: 1 DAT2, 2 CS (DAT3), 3 MOSI (CMD), 4 VDD, 5 SCK (CLK), 6 VSS, 7 MISO (DAT0), 8 DAT1; carcasa pe masă | ordinea microSD |
| ✔ | 47 kΩ spre 3V3_SD pe CS, MISO, DAT1, DAT2: liniile nefolosite nu plutesc, iar CS ține cardul neselectat până îl alege firmware-ul | specificația SD, modul SPI |
| ⚠ | C10 (100 nF) e la 11 mm de pinul 4. Mai aproape nu se poate: pinul e sub carcasa metalică a soclului. La SPI ≤ 20 MHz e acceptabil, iar C9 (10 µF) e pe aceeași șină | `verifica.py` |
| ✔ | J3: BUSY, RST, DC, CS, SCK, MOSI, GND, VCC, ordinea de pe modulul WeAct | desenul WeAct |
| ⚠ | Poziția lui J3 e luată din desen; de măsurat pe modul (deja în README) | — |

### Amprentele

| | Piesa | Amprenta | Observații |
|---|---|---|---|
| ✔ | U1, U4, U5 | SOT-23-6 | capsulele DBV și SC6 sunt SOT-23-6 |
| ? | U2 | VQFN-16 3×3, EP 1,6 mm | TI RGT0016C. De comparat la citire cu desenul „land pattern” din foaie (pastila centrală în desenul TI e ~1,68 mm) |
| ? | U3 | WSON-10 2,5×2,5, EP 1,2×2 | TI DSK0010A; numele se potrivește, de comparat cu desenul din foaie |
| ? | U6 | TDFN-8 2×2, EP 0,8×1,2 | Maxim T822+3, desenul 90-0065 e pe site-ul Maxim, nu în foaie: de descărcat și comparat |
| ✔ | U7 | RF_Module:ESP32-S3-WROOM-1 | amprenta KiCad a modulului, cu zona fără cupru a antenei |
| ✔ | L1 | 1008 (2520 metric) | DFE252010F are 2,5 × 2,0 mm |
| ✔ | J1, J4, SW1, SW2–SW6 | amprentele producătorilor din KiCad | — |
| ⚠ | toate | rotațiile din `cpl.csv` | de verificat în previzualizarea JLCPCB (deja în README) |

### Antena

| | Ce | Sursa |
|---|---|---|
| ✔ | Niciun cupru (plan, traseu sau via) sub antenă, pe niciunul din cele 4 straturi | `verifica.py` |
| ✔ | Antena e la marginea plăcii, cu punctul de alimentare lângă margine | ghid, „Module-on-board” |
| ⚠ | Placa continuă sub antenă (FR4 fără cupru, ~6 mm). Espressif preferă antena în afara plăcii sau o decupare sub ea. Găurile H2/H4 din colțuri ar permite o decupare între ele | ghid, figura „Keepout Zone” |
| ⚠ | Placa WeAct și panoul sunt la 11 mm deasupra antenei, iar ghidul cere 15 mm liberi în toate direcțiile, în carcasă. Cel mai aproape metal din carcasă sunt șuruburile H2/H4, la ~15 mm | ghid; `enclosure/README.md` |

### Punctele de test

| | Ce |
|---|---|
| ✔ | TP1–TP6: VBUS, VSYS, BAT_J, VBAT, +3V3, GND |
| ✔ | JP1 (I_BAT): puntea se taie, iar ampermetrul se pune între BAT_J și VBAT, pentru măsurătorile de consum |
| ✔ | J5 (UART0) pentru jurnal, lângă BOOT și RESET |
| ⚠ | 3V3_SD și 3V3_EPD nu au punct de test. Se măsoară pe C9 și C11 |

## De hotărât la citire

1. ✎ **Bus-uri SPI separate pentru card și panou. Făcut.** Cardul a rămas pe pinii de pe DevKit (SCK 12, MOSI 11, MISO 13, CS 14), iar panoul a primit SCK pe GPIO47 și MOSI pe GPIO48, pinii de lângă CS-ul și DC-ul lui pe latura modulului (în loc de GPIO38–42, cum scria prima trecere: așa traseele sunt mai scurte, iar firmware-ul cardului nu se schimbă). Placa s-a retrasat: 52 din 52 de rețele, DRC 0.
2. ✎ **Rezistențe serie pe D+/D−. Făcut**, pe jumătate: R25/R26 de 22 Ω (0603, ca restul plăcii) stau lângă modul. Traseele USB **n-au fost scurtate** (67 și 69 mm); asta cere mutarea mufei USB-C și a încărcătorului, deci și a carcasei. La full speed nu contează; rămâne pentru v2.
3. **Decupare sub antenă**, ~6 × 30 mm, între H2 și H4. Schimbă și conturul carcasei (`placa.py` îl preia singur). *Recomandare: de hotărât după o măsurătoare de semnal pe DevKit în carcasa printată (ENC-3), dacă WiFi-ul contează.*
4. **WiFi-ul și cei 400 mA.** Varianta ieftină e în firmware: puterea de emisie limitată la ~13 dBm (`esp_wifi_set_max_tx_power`), iar WiFi-ul doar cu panoul și cardul inactive. Varianta scumpă e alt regulator, dar ar pierde cei 75 nA în gol, pentru care a fost ales TPS63900. *Recomandare: firmware-ul, și măsurat.*
5. **Divizorul VBUS mai mare** (de exemplu 470k/470k), ca să scadă curentul din IO2 cât ESP-ul e oprit. Atunci îi trebuie un condensator de 100 nF, ca la VBAT_SENSE. *Mărunt; opțional.*
6. **Puncte de test pe 3V3_SD și 3V3_EPD.** *Opțional; C9/C11 ajung.*

## Pentru firmware (PCB-8, `board_pcb_v1.h`)

✎ **Scris** (`firmware/boards/include/board_pcb_v1.h`, `firmware/main/board_io.c`, `firmware/components/drivers/fuelgauge.c`). Compilează; nu a rulat pe placă. `verifica.py` compară acum antetul cu placa și dă eroare la orice diferență.

- Pini noi față de DevKit:
  - panoul pe a doua gazdă SPI (SPI3): SCK GPIO47, MOSI GPIO48; cardul rămâne pe SPI2 cu 12/11/13 și CS 14;
  - SD_PWR_EN GPIO15;
  - EPD_PWR_EN GPIO21;
  - I2C SDA GPIO16 / SCL GPIO17;
  - FG_ALRT GPIO7;
  - VBUS_SENSE GPIO2, cu divizor 2.
- Pe MAX17048 nu se setează MODE.EnSleep. ✎ `fuelgauge.c` doar citește.
- Înainte de a opri o șină comutată: liniile piesei de pe ea (SCK, MOSI, CS și restul) pe 0. Cu bus-urile separate, asta nu-l încurcă pe celălalt. ✎ `board_power_off()`, ținute pe 0 și în somn.
- WiFi-ul: puterea de emisie limitată, vezi punctul 4. ✎ Doar limita, în antet (`BOARD_WIFI_MAX_TX_QDBM`): firmware-ul nu are încă WiFi.

## Ce nu acoperă revizia

- Codurile LCSC și stocurile (PCB-5).
- Desenele de amprentă pentru U2, U3, U6, marcate cu „?” mai sus.
- Poziția lui J3 și polaritatea JST: se măsoară pe piese.
- Orice comportament real: nimic nu e lipit, iar consumul și semnalul WiFi se măsoară pe placă.
