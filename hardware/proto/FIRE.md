# Placa de prototipare: piese și fire

Generat de `layout.py` (nu se editează de mână). Desenul 1:1: `out/sablon.pdf`, pagina 1 fața pieselor, pagina 2 fața de lipire.

Placa: 41 × 21 găuri (104.14 × 53.34 mm), tăiată dintr-o placă de 9 × 15 cm sau mai mare. Găurile se numesc rând + coloană: rândurile A–X (fără I, O, Q) de sus în jos, coloanele 1–41 de la stânga la dreapta, văzut dinspre piese.

## Piesele

| Piesă | Cum se prinde | Mărimea |
|---|---|---|
| U1 ESP32-S3-DevKitC-1 | pe 2 socluri mamă 1×22; USB spre stânga | măsurat (desen oficial) |
| U4 soclu afișaj 1×8 | pe fața dinspre ecran, lipit pe fața pieselor | din desen, de verificat pe piesă |
| U5 modul microSD | 42 × 24 mm, tipic; bareta de 6 pini jos, fanta sus | estimat, de măsurat |
| U2 TP4056 | 28 × 17,5 mm, tipic; bandă dublu adezivă, USB-C la margine | estimat, de măsurat |
| U3 TPS63020 | 20 × 14 mm, tipic; bandă dublu adezivă | estimat, de măsurat |
| SW1 SS12D00 | 3 pini; se folosesc mijlocul și cel de sus | estimat, de măsurat |
| JBTN baretă butoane 1×4 | cablu cu 4 fire spre plăcuța cu cele 3 butoane | piesă standard, pas 2,54 |
| R1, R2 100k, C1 100nF | culcat, sub DevKit | piesă standard, pas 2,54 |

Bateria nu are găuri în placă: cablul JST se lipește direct pe B+ (roșu) și B− (negru) ale TP4056, **după** verificarea polarității (HW-5).

## Firele de pe fața de lipire

În ordinea de lipire: întâi alimentarea, apoi semnalele.

| Nr. | Rețea | De la | La | Lungime |
|---|---|---|---|---|
| 1 | 3V3 | H22 (U3 VOUT) | M28 (U1 3V3) | ~35 mm |
| 2 | 3V3 | B21 (U4 VCC) | H22 (U3 VOUT) | ~30 mm |
| 3 | 3V3 | U35 (U5 VCC) | M28 (U1 3V3) | ~45 mm |
| 4 | GND | T22 (R2 GND) | V24 (C1 GND) | ~20 mm |
| 5 | GND | U34 (U5 GND) | V24 (C1 GND) | ~50 mm |
| 6 | GND | M7 (U1 G) | T22 (R2 GND) | ~60 mm |
| 7 | GND | J12 (U2 OUT−) | M7 (U1 G) | ~30 mm |
| 8 | GND | K22 (JBTN GND) | J12 (U2 OUT−) | ~40 mm |
| 9 | GND | B20 (U4 GND) | K22 (JBTN GND) | ~35 mm |
| 10 | GND | F22 (U3 GND) | B20 (U4 GND) | ~25 mm |
| 11 | OUT+ | D12 (U2 OUT+) | R3 (SW1 comun) | ~60 mm |
| 12 | VSYS | P3 (SW1 VSYS) | R22 (R1 VSYS) | ~60 mm |
| 13 | VSYS | D22 (U3 VIN) | R22 (R1 VSYS) | ~75 mm |
| 14 | BTN_MENU | M23 (U1 IO6) | K23 (JBTN MENIU) | ~15 mm |
| 15 | BTN_NEXT | M24 (U1 IO5) | K24 (JBTN ÎNAINTE) | ~15 mm |
| 16 | BTN_PREV | M25 (U1 IO4) | K25 (JBTN ÎNAPOI) | ~15 mm |
| 17 | EPD_BUSY | B14 (U4 BUSY) | M18 (U1 IO18) | ~45 mm |
| 18 | EPD_CS | B17 (U4 CS) | M13 (U1 IO10) | ~50 mm |
| 19 | EPD_DC | B16 (U4 D/C) | M14 (U1 IO9) | ~40 mm |
| 20 | EPD_RST | B15 (U4 RES) | M17 (U1 IO8) | ~40 mm |
| 21 | SD_CS | U39 (U5 CS) | M9 (U1 IO14) | ~100 mm |
| 22 | SPI_MISO | U36 (U5 MISO) | M10 (U1 IO13) | ~90 mm |
| 23 | SPI_MOSI | B19 (U4 SDA) | M12 (U1 IO11) | ~55 mm |
| 24 | SPI_MOSI | U37 (U5 MOSI) | B19 (U4 SDA) | ~100 mm |
| 25 | SPI_SCK | B18 (U4 SCL) | M11 (U1 IO12) | ~55 mm |
| 26 | SPI_SCK | U38 (U5 SCK) | B18 (U4 SCL) | ~100 mm |
| 27 | VBAT_SENSE | T26 (R2 SENS) | V26 (C1 SENS) | ~15 mm |
| 28 | VBAT_SENSE | R26 (R1 SENS) | T26 (R2 SENS) | ~15 mm |
| 29 | VBAT_SENSE | X25 (U1 IO1) | V26 (C1 SENS) | ~20 mm |

## Firele scurte de pe fața pieselor

| Modul | Pad | Gaura |
|---|---|---|
| U2 TP4056 | OUT+ | D12 |
| U2 TP4056 | OUT− | J12 |
| U3 TPS63020 | VIN | D22 |
| U3 TPS63020 | GND | F22 |
| U3 TPS63020 | VOUT | H22 |

## Plăcuța butoanelor

Trei butoane tactile pe o bucată mică de placă, legate la J_BTN cu un cablu cu 4 fire: fiecare buton între linia lui și GND (pull-up-urile sunt interne). Unde stă plăcuța se hotărăște cu carcasa; pentru masă poate atârna.

| J_BTN | Gaura | Butonul | GPIO |
|---|---|---|---|
| 1 | K22 | GND, comun la toate trei | |
| 2 | K23 | MENIU | IO6 |
| 3 | K24 | ÎNAINTE | IO5 |
| 4 | K25 | ÎNAPOI | IO4 |

## De măsurat pe piesele tale, înainte de lipire

Valorile marcate „estimat” sunt mărimi tipice. Se măsoară cu rigla sau șublerul și se trec în `layout.py`; scriptul refăcut spune dacă tot mai încape.

1. **Bareta afișajului**: unde e pinul 1 (BUSY) față de colțul plăcii WeAct, ce fel de baretă are (tată, mamă sau doar găuri) și cât de lungi sunt pinii. De aici ies poziția soclului și înălțimea distanțierelor (cu soclu mamă de 8,5 mm, ~11 mm).
2. **Modulul microSD**: lungimea, lățimea, unde e bareta de 6 pini și ordinea pinilor ei. Și dacă are regulatorul AMS1117 (nota 3 din schemă).
3. **TP4056**: mărimea și unde sunt padurile OUT+, B+, B−, OUT−.
4. **TPS63020**: mărimea și unde sunt VIN, GND, VOUT.
5. **SS12D00**: pasul pinilor (2,54 sau 2,0 mm).
6. **Găurile de prindere ale afișajului**: distanța dintre ele (aici 91,8 × 48,0 mm, din desen).

## Înainte de prima alimentare

1. Fără DevKit, fără card, fără baterie: cu multimetrul pe continuitate, fiecare rețea din lista de mai sus sună de la un capăt la altul, iar 3V3, VSYS și OUT+ **nu** sună cu GND.
2. Polaritatea cablului bateriei față de B+ / B− (HW-5).
3. Bateria pusă, SW1 pornit, încă fără DevKit: 3,3 V între K28 (soclul J1, pinul 3V3) și K7 (G). Apoi SW1 oprit, DevKit-ul în socluri, afișajul și cardul puse.
4. Restul e lista de punere în funcțiune (HW-8) și aplicația din `firmware/`.

## Verificări

- Rețelele vin din schemă (`kicad-cli`); 16 se leagă pe placă, toate complete, fără scurtcircuite.
- Fiecare pin de soclu cade pe o gaură; nicio gaură cu doi pini; nicio piesă peste șuruburi sau sub antenă.
- Atenție: 7 puncte de fir sub antenă.
