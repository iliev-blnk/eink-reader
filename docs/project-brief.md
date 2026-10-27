# Descrierea proiectului

Proiect de diplomă de licență: un cititor de cărți electronice cu ecran e-ink, cu firmware propriu (un „sistem de operare propriu”) și format de carte propriu.

Adunat pe 2026-10-25 din discuțiile de planificare de mai devreme (19 oct. planificarea pieselor, 25 oct. lista pieselor cumpărate).

## Hardware: cumpărat (la 2026-10-25)

| Piesă | Note |
|---|---|
| ESP32-S3 DevKitC-1, flash de 16 MB | A înlocuit ESP32-WROOM-32D (48 lei). PSRAM (N16R8 sau nu) neconfirmat. |
| Modul e-paper WeAct Studio 3,7", 416×240 | ~93,92 lei, Elektroweb |
| Modul buck-boost TPS63020, ieșire 3,3 V | 49,09 lei |
| Modul de încărcare LiPo TP4056 | |
| Acumulator LiPo | |
| Modul pentru card microSD | |
| Card microSDHC MediaRange | SDHC (FAT32) în locul cardului de 128 GB, ca să evităm problemele cu exFAT |
| Cititor de carduri microSD USB-C | pentru încărcarea cărților de pe PC |
| Trusă de lipit 15-în-1, 60 W | |
| Fire, rezistoare și piese diverse existente | |

## Hardware: încă deschis

- Multimetru (UT33A, 92,25 lei), dacă trusa de lipit nu are unul
- 3–5 butoane tactile și un comutator glisant SS12D00, dacă trusa nu le are
- Placă de prototipare, barete mamă, cablu JST PH2.0 pentru LiPo (**verificați polaritatea**)
- Carcasă: printare 3D sau o cutie de proiect
- De verificat dacă ESP32-S3 e N16R8 (cu PSRAM). Acum contează mai puțin; vezi secțiunea despre formatul cărții.

## Ecranul: WeAct 3,7"

- Panoul e probabil GDEY037T03 (UC8253). Clasa GxEPD2: `GxEPD2_370_GDEY037T03`
- Timpi de reîmprospătare: cam 3 s complet, 1 s rapid, 0,2 s parțial
- 8 pini: VCC, GND, SDA (MOSI), SCL (SCK), CS, D/C, RES, BUSY. Logică de 3,3 V, deci nu trebuie convertor de nivel.
- Alimentarea se alege prin punți de lipire: SB4+SB5 pentru 3,3–5 V, SB3 doar pentru 3,3 V. **Verificați punțile implicite la primire.**
- **Setați padul SPI pe 4 fire înainte de prima alimentare.**
- Placa are 105,79 × 53,80 mm. Găurile de prindere de 3,2 mm sunt la vreo 93 × 48 mm una de alta (necesar pentru carcasă).
- În orientare portret pagina are 240×416, adică e îngustă.

## Alimentarea

- Traseul: LiPo → TP4056 OUT → comutator glisant → TPS63020 IN → TPS63020 OUT → pinul 3V3 al ESP32.
- Nu alimentați pinul 5V al DevKit-ului: AMS1117 de pe placă cade pe la 3,9 V.
- Indicatorul bateriei: divizor 2× 100k plus 100 nF într-un pin ADC.
- DevKit-ul consumă câțiva mA în somn adânc (cipul USB-UART, LED-ul), deci autonomia va fi de zile, nu de săptămâni. Pentru un prototip e în regulă. Dacă autonomia contează la susținere, o placă LOLIN D32 sau de tip FireBeetle rezolvă problema.
- Ecranul și cardul SD împart SPI, fiecare cu pinul CS propriu.
- Butoanele merg pe GPIO-uri cu RTC, ca să poată trezi cipul din somn adânc.

## Firmware („sistem de operare propriu”)

Firmware propriu pe ESP-IDF/FreeRTOS, nu un nucleu scris de la zero. Cuprinde:

- Ecranul de pornire și lansatorul (bibliotecă, setări, somn)
- Butoanele și trezirea din somn adânc
- Stratul de stocare pe sistemul de fișiere FAT al cardului SD
- Gestionarea energiei: somn adânc, indicatorul bateriei, oprire la tensiune mică
- Stratul de afișare: reîmprospătare parțială la întoarcerea paginii, completă la fiecare N pagini ca să șteargă urmele (ghosting)

## Formatul de carte propriu

Un convertor pe PC transformă EPUB/TXT în formatul dispozitivului, așa că dispozitivul nu parsează niciodată EPUB. Asta elimină nevoia de PSRAM. S3 rămâne util pentru USB nativ și pentru cei 16 MB de flash, unde stau fonturile.

Puncte de proiectare:

- Antet și index: metadate, tabelul capitolelor, deplasamente în octeți pentru salt
- Pre-paginat (simplu și rapid, dar legat de un singur ecran și font) **sau** reflowable (are nevoie de paginare pe dispozitiv). Încă nedecis.
- Fonturi bitmap cu diacritice românești și chirilice
- Compresie opțională
- Semnele de carte într-un fișier mic separat

Pentru lucrare, convertorul, specificația formatului și firmware-ul dispozitivului luate împreună sunt argumentul cel mai puternic.

## Pașii următori (deschiși)

- [x] Harta pinilor ESP32-S3: făcută ca schemă KiCad, 2026-10-25 (vezi `../hardware/` și Jurnalul din `README.md`)
- [ ] Specificația formatului de carte v0.1
