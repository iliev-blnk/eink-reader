# Planul general: diploma cu cititorul e-ink

v1.0, 2026-10-25. Aprobat. Întrebările deschise rămase din [secțiunea 12](#12-întrebări-deschise) nu blochează Faza 0.

Fișierul ăsta e singura sursă de adevăr pentru ce se construiește, în ce ordine, de cine și cum dovedim că merge. Când se schimbă ceva, se actualizează aici și se adaugă un rând în Jurnalul din `README.md`.

**Stări:** `[ ]` de făcut · `[~]` în lucru · `[x]` făcut · `[-]` renunțat (cu motivul)

---

## 1. Ce construim și de ce

Un cititor de cărți electronice e-ink de mână, proiectat cap-coadă:

1. **Hardware:** întâi un prototip pe placă de prototipare, apoi un PCB propriu.
2. **Firmware:** propriul nostru „sistem de operare”, o platformă de firmware pe straturi peste FreeRTOS. Are drivere, servicii și aplicații proprii.
3. **Formatul de carte:** un format de fișier propriu, plus o unealtă pe PC care convertește EPUB/FB2/TXT în el.
4. **Motorul de paginare a textului:** tipografie adevărată pe un ecran de 240×416 pe 1 bit: rearanjare, justificare și despărțire în silabe pentru română și rusă.
5. **Evaluare măsurată:** fiecare afirmație din lucrare e susținută de o măsurătoare.

**Teza lucrării, într-o propoziție:** *Mutarea parsării documentelor într-o unealtă pe PC, împreună cu un format, un motor de paginare și un hardware atent la consum făcute anume, permite unui microcontroler ieftin să ofere o experiență de lectură utilizabilă (tipografie bună, întoarcerea rapidă a paginii, săptămâni de autonomie) fără stiva procesor de aplicații plus Linux pe care o folosesc cititoarele comerciale.*

**De ce se potrivește unei specializări de inginerie electrică + calculatoare:** jumătatea de electronică e proiectarea circuitului, electronica de putere, PCB-ul și măsurătorile de curent. Jumătatea de informatică e arhitectura firmware-ului, proiectarea formatului de fișier, algoritmii de paginare a textului și lanțul de unelte. Nicio jumătate nu e umplutură.

## 2. Obiective, ce nu facem, ce ar fi un eșec

**Obiective (obligatorii)**
- Un dispozitiv care deschide o carte convertită și întoarce paginile, cu reîmprospătare parțială, pe baterie.
- Text rearanjabil: 3+ mărimi de font, justificat, despărțit în silabe, cu acoperire completă pentru română (ă â î ș ț) și chirilică.
- Bibliotecă, progresul lecturii, semne de carte, setări, ecran de somn.
- Un PCB propriu care înlocuiește placa de dezvoltare și coboară curentul de somn la microamperi.
- Un convertor pentru EPUB și TXT (FB2 dacă e timp) cu o specificație scrisă și versionată a formatului.
- Un capitol de măsurători: timpi, consum, autonomie, eficiența formatului, calitatea paginării.

**Ce nu facem (lăsate deliberat afară)**
- Parsarea EPUB, PDF sau HTML pe dispozitiv
- DRM, magazine online, sincronizare în cloud, conturi
- Imagini în cărți (v1). Doar coperta, cu dithering, pe ecranul de somn.
- Ecran tactil, iluminare frontală, audio
- Linux sau un nucleu scris de la zero. FreeRTOS e nucleul; tot ce e peste el e al nostru.
- Transformarea într-un produs: fără certificare, fără producție de serie

**Ce ar fi un eșec (chiar dacă „merge”)**
- Multe funcții pe jumătate în loc de câteva terminate
- Un dispozitiv care merge doar cu cartea demonstrativă
- Afirmații în lucrare fără o măsurătoare în spate
- Orice nu poți explica singur la susținere
- Manipularea nesigură a LiPo: scurtcircuite, fără protecție, o baterie neprinsă

## 3. Constrângeri

| | |
|---|---|
| Termen | ~9 luni: predarea pe la **sfârșitul lui iulie 2027** (data exactă: întrebarea deschisă Q2) |
| Specializarea | Inginerie electrică și calculatoare |
| Obligatorii (decizia ta) | rezultate măsurate, motorul de paginare a textului |
| Platforma | ESP32-S3 (ai un DevKitC-1 de 16 MB). Prin proiectare nu e nevoie de PSRAM. |
| Bugetul de memorie | 512 KB SRAM. Framebufferul e 240×416 la 1 bpp = 12.480 B. |
| Buget | Piesele cumpărate, plus cam 600–900 RON în plus (vezi [secțiunea 9](#9-lista-de-cumpărături)) |
| Comenzi | PCB-uri de la JLCPCB/PCBWay, livrate în România (TVA încasat la plată) |

## 4. Arhitectura sistemului

```
 Pe PC                                     Pe dispozitiv
 ┌─────────────────────────┐               ┌───────────────────────────────────────────┐
 │ convertor (Python CLI)  │   microSD     │ Aplicații: lansator · bibliotecă · lector·│
 │  EPUB/FB2/TXT → parsare │  sau USB-MSC  │            setări · somn · diagnostic     │
 │  → normalizare (NFC,ș/ț)│ ────────────▶ │ Servicii:  paginare · desenarea fonturilor│
 │  → structură → silabe   │  fișier .ibv  │            · cititorul cărții · energie · │
 │  → compresie → scriere  │  + fonturi    │            stocare · setări · intrare     │
 │ fonttool: TTF → bitmap  │               │ Drivere:   EPD UC8253 · SD/FATFS · butoane│
 │ validator / afișare     │               │            · ADC baterie / indicator      │
 └─────────────────────────┘               │ HAL:       ESP-IDF (SPI, GPIO, ADC, NVS)  │
                                           │ Nucleu:    FreeRTOS                       │
 Simulator pe PC (același nucleu C) → PNG  └───────────────────────────────────────────┘
```

**Ideea cheie, decizia mea:** **nucleul** (cititorul formatului, desenarea fonturilor, motorul de paginare) e C portabil fără dependențe de ESP-IDF. Se compilează pentru ESP32 și pentru PC. Versiunea de PC e un **simulator** care desenează pagini în PNG. Asta ne dă:
- teste unitare care rulează pe PC
- dezvoltarea software fără să aștepte hardware-ul
- figuri pentru lucrare
- experimente de calitate a paginării pe sute de pagini în câteva secunde

## 5. Structura depozitului (decizia mea)

```
eink-reader/
  hardware/            schema prototipului (există, v1.0)
  hardware/proto/      desenul plăcii de prototipare (HW-6)
  hardware/pcb/        placa proprie: schema, cablajul, fișierele de fabricație, BOM, lista de verificare
  firmware/            proiectul ESP-IDF
    components/core/   C portabil: format, fonturi, paginare (comun cu sim/)
    components/drivers/  epd_uc8253, sdcard, buttons, battery
    components/services/ energie, stocare, setări, intrare, interfață
    main/              managerul de aplicații + aplicațiile
    boards/            board_devkit.h, board_pcb_v1.h (hărțile pinilor)
  sim/                 nucleul compilat pe PC → pagini PNG, teste
  emu/                 firmware-ul întreg pe un ESP32-S3 emulat (QEMU), comparat cu sim/
  tools/converter/     convertorul și validatorul în Python
  tools/fonttool/      TTF → font bitmap
  tools/texref/        ruperea în rânduri comparată cu TeX
  tools/fuzz/          fuzzere pentru fișierele de pe card
  tools/perf/          analiza jurnalului de timpi (E1, E2, E4, E6)
  tools/ina226/        înregistratorul de curent (E3, E4)
  corpus/              cărțile de test (descărcate de fetch.sh)
  spec/                book-format.md, font-format.md (versionate)
  measurements/<exp>/  CSV brut, scripturi, figuri generate (câte un dosar pe experiment; common/ are graficele)
  enclosure/           CAD + STL
  docs/                jurnalul din README, descrierea, planul ăsta, photos/, deciziile
  thesis/              sursa lucrării (formatul depinde de Q1)
```

Controlul versiunilor: git din prima zi; depozitul e public pe GitHub (Q6).

---

## 6. Direcții de lucru

Fiecare direcție are livrabilele, sarcinile, o verificare „gata când” și capitolul din lucrare pe care îl alimentează.

### HW: Hardware-ul prototipului (placă de prototipare, DevKit)

**Livrabil:** un prototip de mână funcțional, conform `hardware/eink-reader.kicad_sch`.
**Lucrare:** proiectarea hardware, plus referința pentru fiecare măsurătoare înainte/după.

- [ ] HW-1 Verifică marcajul modulului DevKit: N16R8 (are PSRAM) sau N16. Fotografiază-l.
- [ ] HW-2 Modulul EPD: fotografiază punțile SB3/SB4/SB5 și padul de mod SPI înainte de orice alimentare. Setează-l pe SPI cu 4 fire. Ce se leagă reiese din poze.
- [ ] HW-3 Testul de fum pe breadboard sau cu fire jumper, cu aplicația de punere în funcțiune din `firmware/` (vezi `firmware/README.md`): text pe ecran, reîmprospătare parțială, citire și scriere pe SD, butoane, somn adânc și trezire. Aplicația e gata din 2026-10-25. Se leagă și se scrie flash-ul.
- [ ] HW-4 Cumpără piesele lipsă (secțiunea 9, „Acum”).
- [ ] HW-5 Verifică polaritatea JST a LiPo-ului față de B+/B− ale TP4056 cu multimetrul *înainte* de a conecta ceva.
- [x] HW-6 Desenul plăcii de prototipare: unde stă fiecare modul și traseul firelor, ca să încapă cam în spatele ecranului de 106×54 mm. *(2026-11-17: ciornă în `hardware/proto`, șablonul 1:1 + `FIRE.md`; mărimile modulelor marcate „estimat” se măsoară pe piese înainte de HW-7.)*
- [ ] HW-7 Lipește prototipul și fotografiază fiecare etapă.
- [ ] HW-8 Lista de punere în funcțiune: tensiunile pe linii în gol și în timpul reîmprospătării, citirea ADC a bateriei față de multimetru, fiecare buton, SD la viteza SPI maximă.
- [ ] HW-9 Actualizează schema dacă s-a schimbat ceva la asamblare. Trece la v1.1 cu jurnal de schimbări.

**Gata când:** merge pe baterie, se încarcă prin USB-C prin TP4056 și trece fiecare punct din HW-8.

### FW: Platforma de firmware („sistem de operare propriu”)

**Livrabil:** un firmware ESP-IDF cu arhitectura pe straturi din secțiunea 4.
**Lucrare:** arhitectura firmware-ului.

Limbajul și stiva (decizia mea): **C pe ESP-IDF** (ultima versiune stabilă), cu **propriul nostru driver UC8253**. GxEPD2 e doar o referință pentru secvențele de inițializare și o verificare a hardware-ului. Nu e o dependență.

- [x] FW-1 Scheletul proiectului: componentele, hărțile pinilor `boards/*.h`, instrucțiunile de compilare și scriere în flash. Instalează ESP-IDF sau rulează scriptul meu de instalare. *Făcut 2026-10-25: `firmware/` se compilează pe ESP-IDF v6.1, care era deja instalat.*
- [~] FW-2 Driverul EPD: inițializare, reîmprospătare completă, rapidă și parțială, comanda de somn adânc, BUSY pe întrerupere. Opțional 4 niveluri de gri, dacă panoul le suportă. *v0 2026-10-25: tot în afară de niveluri de gri, plus cronometrarea pe faze. Se compilează, dar încă n-a rulat pe panou.*
- [~] FW-3 Grafica: framebuffer de 1 bpp, rotație (portret 240×416), urmărirea dreptunghiurilor modificate pentru reîmprospătarea parțială. *v0: framebuffer, text UTF-8 cu un font compilat în program, dreptunghiuri modificate. Încă fără rotație.*
- [~] FW-4 Stocarea: SD pe SPI + montarea FATFS și recuperarea, API de fișiere folosit de servicii. *v0: montare la 20 MHz cu revenire la 5 MHz; nu formatează niciodată. Încă fără recuperare sau API de fișiere.*
- [~] FW-5 Intrarea: butoane cu debounce, apăsare scurtă și lungă, o coadă de evenimente. *v0: toate (interogare la 10 ms, debounce de 30 ms, apăsare lungă de 800 ms). Încă nerulat pe hardware.*
- [~] FW-6 Managerul de aplicații: ciclul de viață al unei aplicații (intrare, eveniment, desenare, ieșire) și împărțirea pe taskuri FreeRTOS (task de interfață, task de intrare, task de fundal pentru paginare). *v0 2026-10-27 (`components/ui`): stiva de aplicații (`enter`/`resume`/`leave`/`key`/`draw`/`idle`), reîmprospătarea parțială calculată din diferența de framebuffer, cea completă la N. Fără task de paginare separat: lucrul de fundal e cooperativ, în taskul interfeței (de ce: Jurnalul). Verificat în `sim ui`; încă nerulat pe hardware.*
- [~] FW-7 Managerul de energie:
  - somn ușor în timpul lecturii, somn adânc după o perioadă de inactivitate, trezire din orice buton (ext1 ANY_LOW, pull-up RTC activate). *Trezirea din buton merge deja în aplicația de punere în funcțiune.*
  - ecran de baterie descărcată și oprire sub pragul de tăiere
  - fiecare schimbare de stare marcată în timp, pentru măsurători

  *v0 2026-10-27: somn adânc după inactivitate (setarea „Somn după”) și la MENIU lung, coperta pe ecran, trezirea redeschide cartea; ecranul de baterie descărcată sub 3,40 V; linii PERF la apăsare, paginare, reîmprospătare și somn. Încă fără somn ușor.*
- [~] FW-8 Bateria: calibrarea ADC (curba din eFuse), corecția divizorului, o curbă tensiune→% pentru LiPo, netezire. *v0: calibrarea și corecția divizorului. 2026-10-27: procentul dintr-o curbă LiPo pe 11 puncte (`ui_battery_pct`). 2026-11-18: netezirea, o medie exponențială cu ponderea 1/4 pentru procentul afișat, luată de la capăt după un salt de peste 200 mV; oprirea la baterie descărcată rămâne pe citirea brută (testul `baterie` din simulator). Încă neîncercat pe hardware: curba și pragul de salt sunt de verificat cu E4.*
- [~] FW-9 Setările în NVS: mărimea fontului, spațierea rândurilor, marginile, intervalul N de reîmprospătare completă, timpul până la somn, despărțirea în silabe pornită/oprită. *v0 2026-10-27: toate, plus alinierea; blob în NVS, aplicația Setări, paginarea refăcută la aceeași poziție.*
- [~] FW-10 Aplicațiile:
  - ecranul de pornire
  - lansatorul
  - biblioteca: lista cărților din metadate, ordonate după cele recente
  - lectorul
  - setările
  - ecranul de somn: coperta sau titlul, afișat pe un panou nealimentat
  - diagnosticul: tensiuni, timpi, versiuni

  *v0 2026-10-27: toate, plus cuprinsul și lista semnelor de carte. Punerea în funcțiune rămâne, cu MENIU ținut la o pornire la rece.*
- [~] FW-11 Progresul și semnele de carte: fișiere însoțitoare lângă fiecare carte (vezi SPEC). *v0 2026-10-27: `.pos`, `.bm` (până la 64), `.pg` și `/.ibv/recent`, scrise prin `.tmp` + ștergere + redenumire. 2026-11-18: `/.ibv/layouts` cu ultimele 6 paginări; indexurile `.pg` ale celorlalte se șterg (testul `pg` din simulator).*
- [~] FW-12 Stocare de masă USB pe USB nativ (TinyUSB MSC): conectat la un PC, cardul SD apare ca disc și se copiază cărțile. Pe dispozitiv FATFS e demontat cât USB-ul e activ. *v0 2026-11-17: „Conectare la PC” în meniul principal; `usbdisk.c` peste esp_tinyusb 3.0.1 (cardul deschis direct prin sdspi, fără formatare), scoaterea în siguranță de pe calculator închide ecranul singură, confirmare la ieșire cât discul e montat; în simulator cu un disc fals (`u:STARE`, scenariul `usb` din `sim/test`). Se compilează; încă neîncercat pe hardware.*
- [~] FW-13 Puncte de măsurare: un jurnal `perf` de marcaje de timp (frontul butonului, începutul/sfârșitul paginării, transferul SPI, BUSY jos/sus) scris pe SD ca CSV, plus un mod de test cu „întoarcere automată a paginii”. *v0 2026-10-28: `perflog.c` (pornit de dosarul `/perf`, scris doar când bucla așteaptă), frontul butonului în evenimentul din coadă, reîmprospătarea cu SPI și BUSY separate, întoarcerea automată la 5 s / 1 min din meniul cărții; analiza în `tools/perf/perf.py` (E1, E2, E4, E6), încercată pe jurnalul simulatorului. Încă nefolosit pe hardware.*
- [~] FW-14 Scrierea în flash și testarea pe prototip după fiecare funcție. Raportează și fotografiază orice e ciudat. *2026-11-16: până e legat prototipul, firmware-ul întreg rulează în emulator (`emu/`), cu cadrele comparate cu ale simulatorului; vezi Jurnalul.*

**Gata când:** poți citi un roman întreg pe prototip folosind doar dispozitivul (bibliotecă → carte → lectură → somn → trezire → aceeași pagină), fără căderi și fără SD corupt după 100 de cicluri somn/trezire.

### SPEC: Formatul de carte

**Livrabil:** `spec/book-format.md` (v0.1 → v1.0) și `spec/font-format.md`.
**Lucrare:** proiectarea formatului de carte, cu o comparație față de EPUB/TXT/FB2.

Deciziile mele de proiectare (de justificat în specificație):
- **Rearanjabil, nu pre-paginat.** Paginarea textului e obligatorie, deci paginarea se face pe dispozitiv.
- **Fișier binar little-endian:** antet cu cod magic + versiune, metadate (titlu, autor, limbă), un tabel al capitolelor cu deplasamente în octeți și un flux de conținut din înregistrări de paragraf. Fiecare înregistrare are text UTF-8 plus intervale de stil (bold, italic, nivel de titlu) și cratime moi (U+00AD) puse de convertor.
- **Compresie pe bloc** de ≤ 32 KB (paragrafe întregi, țintă 16 KB), ca memoria folosită și timpul de salt să rămână constante oricât de lung ar fi un capitol. DEFLATE, decodat de `tinfl` din miniz, care e deja în ROM-ul ESP32-S3. Experimentul E5 măsoară mărimile blocurilor și timpul de decodare. *(Era „pe capitol”; schimbat în SPEC-1 pentru că o carte TXT e un singur capitol, iar capitolele reale depășesc fereastra de 32 KB.)*
- **Coperta:** un bloc opțional cu o imagine pe 1 bit, cu dithering.
- **Integritatea:** CRC32 pe antet, pe directorul de secțiuni, pe fiecare secțiune și pe fiecare bloc decodat. Validatorul le verifică pe toate, iar un test inversează fiecare bit al unui fișier exemplu.
- **Fișiere însoțitoare** în `/.ibv/`, numite după un hash al conținutului (`book_id`), ca redenumirea unei cărți să-i păstreze progresul: `<id>.pos` (progresul), `<id>.bm` (semnele de carte), `<id>-<layout>.pg` (cache-ul paginării, identificat după font, mărime, margini și spațiere). Scrise atomic (tmp + redenumire).
- **Formatul de font:** un tabel de glife cu intervale Unicode, bitmapuri de 1 bpp (opțional 2 bpp), lățimile de avans și perechi de kerning opționale. Fonturile stau într-o partiție de flash (16 MB ajung din plin) sau pe SD.

- [x] SPEC-1 Scrierea v0.1 cu o diagramă la nivel de octet și un exemplu lucrat. Făcut 2026-10-26: `spec/book-format.md`, codecul + validatorul de referință `tools/converter/ibv.py` (15 teste), exemplul `spec/examples/example.ibv`.
- [~] SPEC-2 Revizuirea. Alegerea numelui și a extensiei (Q5). Numele e ales: IBV.
- [x] SPEC-3 Specificația formatului de font v0.1. Făcut 2026-10-26: `spec/font-format.md` (IBF), codecul + validatorul de referință `tools/fonttool/ibf.py`, încărcătorul C `firmware/components/core/font.c` care verifică aceleași reguli.
- [ ] SPEC-4 Înghețarea v1.0 după ce convertorul și firmware-ul o implementează amândouă. Schimbările de după cresc versiunea.

**Gata când:** convertorul îl scrie, validatorul îl verifică, firmware-ul și simulatorul îl citesc, iar specificația se potrivește cu toate trei.

### CONV: Convertorul și uneltele de pe PC

**Livrabil:** `tools/converter`, o aplicație Python 3 în linia de comandă: `convert`, `validate`, `dump`. Plus `tools/fonttool`.
**Lucrare:** lanțul de unelte.

- [x] CONV-1 Intrarea TXT: UTF-8, plus detectarea CP1250/CP1251 pentru fișierele vechi românești/rusești și detectarea paragrafelor. Făcut 2026-10-26 (`txt.py`): BOM / UTF-8 / CP1250 / ISO-8859-2/-16 / CP1251 / KOI8-R / CP1252 punctate pe cuvânt; modurile cu rând gol, cu alineat și cu un rând pe paragraf; antete Gutenberg și lib.ru; titluri, separatoare de scenă, versuri, cuprinsuri tipărite.
- [x] CONV-2 Intrarea EPUB 2/3: spine-ul OPF, cuprinsul NCX/nav, XHTML → înregistrări de paragraf cu stiluri de bază. Etichetele necunoscute devin text simplu. Făcut 2026-10-26 (`epub.py`): nav → NCX → titluri pentru cuprins, bold/italic din CSS, notele de subsol și textul standard PG scoase, poezii → VERSE.
- [x] CONV-3 Normalizarea:
  - Unicode NFC
  - ş/ţ românești cu sedilă → ș/ț cu virgulă
  - ghilimele, linii, puncte de suspensie
  - spațiile

  Făcut 2026-10-26 (`textnorm.py`), plus ghilimele după limbă, fără spațiu înainte de punctuație și româna tastată cu ã/º/þ readusă la forma corectă.
- [x] CONV-4 Despărțirea în silabe: algoritmul lui Liang cu modelele de despărțire TeX pentru RO, RU și EN (de verificat licențele), alese după limba cărții. Pune cratime moi. Făcut 2026-10-26 (`hyphenate.py`): implementare Liang proprie (nu pyphen, ca să rămână portabilă) care citește modelele hyph-utf8. Licențele în `tools/converter/patterns/README.md`: en-gb MIT, en-us permisivă, ru LPPL, ro nedeclarată (rezerva: LibreOffice `hyph_ro_RO`).
- [x] CONV-5 Verificarea acoperirii glifelor față de fontul țintă. Avertizează despre caracterele lipsă și aplică înlocuirile obișnuite. Făcut 2026-10-26 (`glyphs.py`): setul de caractere citit din intervalele fonttool, înlocuiri, restul → U+FFFD și raportate.
- [x] CONV-6 Extragerea copertei → redimensionare → dithering Floyd–Steinberg → 1 bpp. Făcut 2026-10-26 (`cover.py`): încadrare în 240×416, Floyd–Steinberg în serpentină, ~50 ms.
- [x] CONV-7 Scriitorul, validatorul și o afișare care tipărește structura într-o formă ușor de citit. Făcut 2026-10-26 (`ibv.py`, `convert.py`): `convert`, `validate`, `dump`, `preview-cover`; fiecare rezultat e validat înainte de a fi scris.
- [x] CONV-8 fonttool: TTF/OTF → formatul nostru de font la mărimile alese, cu intervalele RO + chirilice + punctuație. Alegerea fontului: licență liberă, de exemplu Literata, Noto Serif sau PT Serif (de comparat în simulator). Făcut 2026-10-26 (`tools/fonttool`, 23 de teste): toate trei familiile × 4 stiluri × 16/18/20 px se compilează, 19–41 KB pe față; kerning împrumutat (`kern_as`) pentru literele cu diacritice; înlocuitori pentru glifele lipsă. Alegerea e LAY-7.
- [x] CONV-9 Intrarea FB2 (obișnuită pentru cărțile rusești). De dorit. Făcut 2026-10-26 (`fb2.py`): FB2 și `.fb2.zip`; testat doar pe o carte sintetică.
- [x] CONV-10 Teste pe corpusul de test (vezi MEAS). Fiecare carte se convertește și se validează. Făcut 2026-10-26: 66 de teste; toate cele 15 fișiere din corpus se convertesc și se validează (`corpus/README.md`). Deschiderea lor pe dispozitiv așteaptă LAY/FW.

**Gata când:** toate cărțile din corpus se convertesc și se validează și se deschid pe dispozitiv.

### LAYOUT: Motorul de paginare a textului (obligatoriu)

**Livrabil:** partea de paginare din `firmware/components/core`, comună cu `sim/`.
**Lucrare:** paginarea textului.

- [x] LAY-1 Desenarea fonturilor: căutarea glifelor, copierea în framebufferul de 1 bpp, bold și italic (fișiere de font separate). Făcut 2026-10-26 (`font.c`, `gfx_glyph`): blit cu deplasare pe octet, creion în 1/64 px cu kerning.
- [x] LAY-2 Ruperea în rânduri v1: greedy, cu cratimele moi ca puncte de rupere și justificare prin distribuirea spațiilor. Făcut 2026-10-26 (`layout.c`): și U+200B, linii în interiorul cuvintelor, legături U+00A0, U+2028, tăieri de urgență; ~20–30 µs pe pagină pe PC.
- [x] LAY-3 Paginatorul:
  - rânduri → pagini
  - margini, spațierea rândurilor, alineatul paragrafului, titluri
  - controlul rândurilor văduve/orfane (de dorit)

  Parțial făcut 2026-10-26 (`layout_page`): paginare înainte, margini, spații între rânduri și paragrafe, alineat, titluri centrate și ținute cu ce urmează, capitole pe pagină nouă, separatoare și ornamente, alineat agățat la versuri, marginile citatelor, subsol cu capitolul și procentul. Încă deschis: rândurile văduve/orfane, pagina înapoi (are nevoie de indexul paginilor din LAY-4).

  Făcut 2026-10-27: controlul rândurilor văduve și orfane (`keep_lines`), un titlu rămas jos pe pagină trece pe următoarea, BREAK-urile de la capătul paginii se sar ca începuturile de pagină să fie canonice. Pagina înapoi (`layout_prev`) și găsirea paginii care conține o poziție (`layout_locate`) paginează înainte de la cel mai apropiat punct de sincronizare: începutul cărții sau o înregistrare cu indicatorul `PAGE` (nou în format, pus de convertor pe titlurile de capitol).
- [x] LAY-4 Cache-ul paginării plus un task de paginare în fundal, pentru numerele paginilor și „x din y”. Saltul la un capitol trece prin deplasamentele capitolelor.

  Parțial făcut 2026-10-27 (`pageidx.c`): indexul paginilor (8 B pe pagină, construit câte câteva pagini, căutare binară) și fișierul lui însoțitor `.pg` (salvare, încărcare, reluare, verificări de CRC și de hash al paginării); subsolul din simulator arată „n / total” după ce e complet. Încă deschis: taskul de fundal care îl conduce pe dispozitiv (cu FW-5).

  Făcut 2026-10-27: lectorul construiește indexul în fundal, câte 8 pagini între apăsări (`ui_idle`, cooperativ, nu un task separat), îl salvează și îl reia; saltul la capitol trece prin cuprins și index. Rămâne timpul pe dispozitiv (E6).
- [x] LAY-5 Ruperea în rânduri v2: Knuth–Plass (încadrare totală) pe paragraf, cu o limită de memorie și revenire la greedy pentru paragrafele uriașe. Făcut 2026-10-27: `layout.c` (zonă de lucru `layout_fit_t`, max 2048 puncte de rupere, altfel greedy); față de greedy mai puține cratime, de 2–10× mai puține cratime consecutive, spații cu ~30 % mai puțin lățite (vezi Jurnalul). *2026-11-17: verificat paragraf cu paragraf față de LuaTeX (`tools/texref`); trei abateri nescrise de la TeX îndreptate (LAYOUT_VERSION 4, zona de lucru ~92 KB), 98,63 % din paragrafele corpusului rupte identic cu TeX, restul explicat de două alegeri scrise. E6 în simulator (2026-11-17): pe proză, r mediu −27 %, r p95 −34 %, cratime consecutive −54 %; pe PC de 2,32 ori mai lent decât greedy. v2 pe 20 de cărți (2026-11-18): r mediu −26 %, r p95 −34 %; timpul ×1,36, reprodus prin rulări alternate (×2,32 de la v1 nu se reproduce cu același cod).* Rămâne timpul pe dispozitiv (E6).
- [x] LAY-6 Simulatorul: desenarea oricărei cărți sau pagini în PNG la orice setare, un mod în lot pentru experimente și teste cu imagini de referință. Făcut 2026-10-26 (`sim/`): `pages`, `stats` (cărți întregi: înlănțuire, depășire, timpi, hash-uri de pagini), `compare` (foi de fonturi), testul de referință `test/run.sh` pe o carte de test cu fiecare tip de înregistrare.
- [x] LAY-7 Verificarea vizuală: foi de comparație, judecate pe ecranul real. Candidați: Literata, PT Serif, Noto Serif și Libron (adăugat 2026-10-27: doar latin, deci îi trebuie o rezervă pentru chirilică). Decis 2026-10-27 (tu, după foi): **Libron pentru cărțile în alfabet latin, PT Serif pentru chirilică, ales automat după limba cărții** (`fontsel.c`), cinci trepte de mărime potrivite între cele două (spec/font-format.md, secțiunea 4). Rămâne de confirmat pe ecranul real când prototipul merge.

**Gata când:** cărțile în română, rusă și engleză se paginează fără depășiri, glife lipsă sau diacritice stricate la fiecare mărime de font. Knuth–Plass e mai bun decât greedy la metricile E6 și încape în bugetul de timp al întoarcerii paginii.

### PCB: Placa proprie

**Livrabil:** `hardware/pcb/` cu schema v2, cablajul, fișierele de fabricație (Gerber, BOM, CPL), plăcile asamblate și un raport de punere în funcțiune.
**Lucrare:** proiectarea hardware (placa proprie) și proiectarea alimentării.

Direcția de proiectare (decizia mea; alegerea pieselor se verifică final după stocul JLCPCB la proiectare):
- **MCU:** modulul ESP32-S3-WROOM-1-N16R8, cu zona liberă a antenei respectată.
- **USB-C:** USB nativ pentru scrierea flash-ului, jurnale și stocare de masă, plus intrarea de încărcare. Protecție ESD.
- **Încărcătorul:** un încărcător pentru o celulă cu power path, ca să meargă în timpul încărcării. Candidați: BQ24074 / MCP73831 + împărțirea sarcinii. Plus protecția bateriei (DW01 + MOSFET dublu sau o celulă protejată).
- **Regulatorul de 3,3 V:** un buck-boost cu curent de repaus mic. Candidați: TPS63802 sau TPS63020 (ca modulul tău).
- **Indicatorul de baterie:** MAX17048 (I²C), comparat cu divizorul ADC în măsurători.
- **Ecranul:** v1 păstrează modulul WeAct pe o baretă, ceea ce e puțin riscant. Ambiție: comanda panoului gol printr-un FPC de 24 de pini cu propriul nostru circuit de ridicare a tensiunii (Q4).
- Soclu microSD, 3 butoane, comutator de alimentare, puncte de test pe fiecare linie, o baretă UART, găuri de prindere potrivite cu tiparul de 91,8×48 mm al ecranului.
- 4 straturi (plan de masă continuu, bun pentru antenă și zgomot), dacă prețul nu impune 2.

- [x] PCB-1 Cerințele + tabelul alegerii pieselor, cu motivul fiecărei piese. *2026-11-17: tabelul „Piesele” din `hardware/pcb/README.md`, pe blocuri, cu motivul fiecăruia; codurile LCSC se verifică la PCB-5.*
- [x] PCB-2 Schema v2 + ERC. Calculul bugetului de consum (alimentează predicțiile E3/E4). *2026-11-17: schema generată din `hardware/pcb/circuit.py`, ERC 0; bugetul de somn ~17,5 µA (din foile de catalog) în README-ul plăcii, folosit de E3.*
- [x] PCB-3 Conturul plăcii din dimensiunile ecranului + carcasă, plasarea, rutarea, DRC cu regulile JLCPCB. *2026-11-17: ciornă v1, 4 straturi, rutată de trasorul propriu; DRC 0, nicio legătură lipsă. Ciorna nu e încă revizuită: asta e PCB-4.*
- [x] PCB-4 Revizuirea proiectului după o listă de verificare: amprente, polaritate, pinii de boot/strapping, pinii USB, distanța față de antenă, puncte de test. O citești cu mine. O arăți coordonatorului, dacă vrea. *2026-11-18: cele șase puncte de hotărât din `hardware/pcb/REVIEW.md` au fiecare o decizie: 1, 2, 5, 6 făcute pe placă, 4 în firmware (13 dBm, WiFi doar cu panoul și cardul inactive), 3 (decuparea sub antenă) amânat până la ENC-3. Tot 2026-11-18: amprentele U1 (USBLC6), U2, U3, U6 comparate cu desenele producătorilor, toate potrivite; BQ24074 e în capsula RGT0016B (EP 1,6), cu care amprenta se potrivește exact. Placa nu se schimbă.*
  *Prima trecere e în `hardware/pcb/REVIEW.md`: fiecare bloc față de foaia de catalog, plus regulile din `verifica.py`, care rulează în `make.sh`. Nicio greșeală de legătură; L1 schimbată pe bobina din lista TI. Punctele 1 și 2 sunt făcute: bus-uri SPI separate pentru card și panou (panoul pe GPIO47/48) și rezistențe de 22 Ω pe D+/D−; traseele USB au rămas lungi. Rămâne citirea împreună și punctele 3–6.*
- [ ] PCB-5 Comanda: 5 plăci, cu asamblarea JLCPCB a pieselor cu pas fin. Fișierele comenzii ies din `make.sh`.
- [ ] PCB-6 Lipirea manuală a pieselor mari (conectori, butoane, mufa bateriei, bareta ecranului).
- [ ] PCB-7 Punerea în funcțiune, pas cu pas:
  1. prima alimentare cu curent limitat
  2. liniile de alimentare
  3. încărcătorul
  4. scrierea flash-ului
  5. perifericele
  6. curentul de somn

  Fiecare eroare se notează într-un raport de punere în funcțiune.
- [x] PCB-8 Adăugarea `board_pcb_v1.h`. În rest firmware-ul rulează neschimbat. *2026-11-17: scris și compilat, plus comutarea șinelor înainte de somn (`main/board_io.c`) și indicatorul MAX17048 (`drivers/fuelgauge.c`); `verifica.py` compară antetul cu placa. Neîncercat pe placă.*
- [ ] PCB-9 Dacă v1 are o eroare care blochează: o reparație cu fir sau o refacere v2. E rezervă de timp pentru o refacere în aprilie.

**Gata când:** dispozitivul merge în întregime pe PCB-ul propriu, iar curentul de somn adânc se măsoară în microamperi.

### ENC: Carcasa

**Livrabil:** o carcasă printată care ține PCB-ul, ecranul, bateria și butoanele; CAD în `enclosure/`.
**Lucrare:** partea mecanică (o secțiune scurtă + fotografii).

- [x] ENC-1 CAD parametric (OpenSCAD sau similar). Intrări: ecranul de 105,79×53,80 mm cu găuri la 91,8×48 mm, conturul PCB-ului, mărimea bateriei. *(2026-11-17: ciornă în `enclosure/`, cu placa citită din fișierul KiCad; cotele panoului, ale baretei dintre plăci și ale bateriei sunt estimate și se măsoară înainte de ENC-2, lista e în README-ul ei.)*
- [ ] ENC-2 Printarea: la facultate, într-un maker space sau la un serviciu online (Q3).
- [ ] ENC-3 Verificarea potrivirii → revizuire → printul final.

**Gata când:** se ține în mână, e închisă, butoanele merg, USB-C e accesibil, bateria nu se poate mișca.

### MEAS: Măsurători și evaluare (obligatoriu)

**Livrabil:** `measurements/<exp>/` conține metoda, datele CSV brute, un script și figurile generate. Fiecare număr din lucrare vine dintr-unul din aceste dosare.
**Lucrare:** evaluarea.

Metoda fiecărui experiment se scrie **înainte** de a-l rula, ca rezultatele să n-o poată influența.

| ID | Întrebare | Montaj | Metrică | Când |
|---|---|---|---|---|
| E1 | Cât durează fiecare mod de reîmprospătare? | jurnalul perf din FW + analizor logic pe BUSY | ms, completă/rapidă/parțială, n≥50 | dec. |
| E2 | Cât durează întoarcerea unei pagini cap-coadă și unde se duce timpul? | frontul butonului → eliberarea BUSY. Defalcare: paginare, desenare, SPI, panou. Somn ușor față de somn adânc între pagini. | ms, medie/p95 | feb. + mai |
| E3 | Cât curent consumă fiecare stare? | INA226 + placă de înregistrare în serie cu bateria pentru stările de ordinul mA. Somnul adânc pe PCB (µA) e sub rezoluția de ~25 µA a INA226 cu un șunt de 0,1 Ω, deci se măsoară cu domeniul de µA al multimetrului, cu un jumper care ocolește aparatul la trezire. | mA/µA pe stare, energie pe pagină întoarsă | feb. (DevKit) + mai (PCB) |
| E4 | Cât ține de fapt bateria? | modul de întoarcere automată (1 pagină/min) până la pragul de tăiere, pe ambele plăci | ore/pagini măsurate față de cele prezise din E3 | mar. (DevKit) + mai (PCB) |
| E5 | Merită formatul nostru? | corpus de 20 de cărți | mărime (EPUB/TXT/al nostru ± compresie), timpul de conversie, de deschidere, de salt la capitol, RAM maxim | feb. |
| E6 | Ajută cu adevărat o rupere mai bună în rânduri? | simulator pe corpus: greedy față de Knuth–Plass × despărțire în silabe pornită/oprită | statistici ale raportului de ajustare, % rânduri cu cratimă, cratime consecutive, ms de paginare/pagină pe dispozitiv | feb.–mar. |
| E7 | Cât de des e nevoie de o reîmprospătare completă? | fotografii la lumină fixă după N = 1/5/10/20 întoarceri parțiale | urmele (vizual + contrast din fotografii) | ian. |
| E8 | Comportamentul la încărcare | tester USB-C | timpul de încărcare, curba curentului | mai (opțional) |
| E9 | Le place cititorilor reali? | 3–5 oameni citesc 20 min, chestionar scurt | note + comentarii | mai (opțional) |

- [x] MEAS-1 Corpusul de test: ~20 de cărți din domeniul public în EN, RO și RU (Project Gutenberg, Wikisource), scurte și lungi, simple și cu formatare bogată. Listate cu sursele și licențele. *Făcut 2026-11-18: 26 de fișiere / 20 de cărți în `corpus/` (`fetch.sh`, README cu surse, licențe și rezultate), cu două FB2 reale (lib.ru), Caragiale în CP1250 și ISO-8859-16 și cinci exporturi Wikisource. E5 și E6 rulate din nou pe toate 20 (v2).*
- [~] MEAS-2 Firmware-ul de înregistrare pentru placa INA226, plus scripturile CSV → figură. *2026-11-17: scrise (`tools/ina226`): firmware pentru ESP32 și ESP32-C3, compilat; `ina226.py` cu captura, rezumatul, figura și energia pe pagină aliniată cu jurnalul `perf`, testat pe date sintetice. Rămâne de încercat pe hardware, după cumpărarea modulului și a plăcii.*
- [x] MEAS-3 O fișă de metodă de o pagină pentru fiecare experiment. Făcut 2026-11-17: `measurements/E1…E9/METODA.md`, scrise înainte de date, cu predicții și praguri.
- [~] MEAS-4 Rularea lor: fotografii ale montajului, datele brute rămân brute. *2026-11-17: rulate partea din simulator a E6 și partea de pe PC a E5 (`measurements/E5`, `E6`); restul cer prototipul.*
- [~] MEAS-5 Analiza + figurile + o notă scurtă cu concluziile fiecărui experiment. Fiecare notă intră direct în capitolul lucrării. *2026-11-17: E5 (PC) și E6 (simulator) au `analiza.py`, `tabele.md`, figuri și `REZULTATE.md`.*

**Gata când:** E1–E7 au date pentru ambele plăci, unde e cazul, cu figuri.

### THESIS: Redactarea și susținerea

**Livrabil:** lucrarea, prezentarea, o demonstrație și un video de rezervă.

În **română**, cam **40 de pagini** (răspunsul tău la Q1). Tema e aprobată. Ciorna cuprinsului cu un buget de pagini (anexele în plus). Se ajustează după șablonul facultății:

1. Introducere: motivația, scopul, contribuțiile (2–3 p)
2. Stadiul actual:
   - cititoarele comerciale (Kindle, Kobo, PocketBook, care rulează Linux pe procesoare de aplicații)
   - proiectele open-source (KOReader, Inkplate, proiecte de cititoare pe ESP32)
   - tehnologia e-paper
   - formatele de fișier
   - algoritmii de rupere în rânduri

   (5–6 p)
3. Cerințele și arhitectura sistemului (3–4 p)
4. Hardware: prototipul, PCB-ul propriu, proiectarea alimentării (7–8 p)
5. Platforma de firmware (5–6 p)
6. Formatul de carte și convertorul (4–5 p)
7. Motorul de paginare a textului (4–5 p)
8. Evaluarea (MEAS) (6–7 p)
9. Concluzii și direcții viitoare (1–2 p)

40 de pagini e strâmt pentru atâta muncă, deci detaliile merg în anexe (specificația, schemele, BOM-ul, tabelele de măsurători). Capitolele păstrează doar deciziile și rezultatele.

Anexe: scheme, specificația formatului, BOM, corpusul de test.

- [~] TH-1 Cerințele facultății. Se știe: română, ~40 de pagini, temă aprobată. Încă trebuie: fișierul șablon, stilul de citare, dacă anexele intră în cele 40 de pagini, regulile verificării antiplagiat.
- [ ] TH-2 Studiul stadiului actual cu referințe reale, verificate. Găsesc și verific sursele; fără citări inventate.
- [ ] TH-3 Fiecare capitol se scrie când se închide faza lui, nu toate în iunie. Ciorna pornește din Jurnal, din specificații și din măsurători.
- [ ] TH-4 Figurile: scheme bloc, scheme electrice, grafice ale măsurătorilor, comparații de paginare.
- [ ] TH-5 Prezentarea plus scenariul demonstrației. Repetiții și o susținere de probă cu întrebări ca la comisie.
- [ ] TH-6 Înregistrarea unui video demonstrativ de rezervă, în caz că dispozitivul face figuri în ziua aceea.

### DOCS: Documentația, pe tot parcursul

- După fiecare sesiune de lucru care contează, o intrare datată în **Jurnalul** din `docs/README.md`: ce s-a făcut și de ce.
- Deciziile merg în [secțiunea 11](#11-decizii) de aici, cu motivul.
- Fotografiile: `docs/photos/YYYY-MM-DD-<ce>.jpg`. Fă multe; sunt material pentru lucrare. Se redenumesc și primesc legende.
- „Gata când” al fiecărei direcții se bifează aici, cu data.

---

## 7. Calendarul și etapele

Azi e 2026-10-25. Predarea presupusă: sfârșitul lui iulie 2027. Sesiunea de iarnă (ian.–feb.) înseamnă luni mai ușoare pentru munca practică.

| Când | Accent | Etapa (gata când) |
|---|---|---|
| **26 oct. – 8 nov.** | Faza 0: pregătirea. HW-1…5, FW-1, depozitul git, TH-1 (regulile facultății, coordonatorul), comanda pieselor „Acum” | **M0:** text pe ecran, un fișier citit de pe SD, regulile facultății cunoscute |
| **nov. – dec.** | Prototipul lipit (HW-6…9). Driverul, stocarea, intrarea, managerul de aplicații (FW-2…6). SPEC v0.1. Convertorul TXT + EPUB (CONV-1…3, 7). Paginare greedy + simulator (LAY-1, 2, 6). E1. | **M1 (20 dec.):** o carte convertită se citește pagină cu pagină pe prototip la o singură mărime de font |
| **ian. – feb.** | Managerul de energie, setările, aplicațiile, USB-MSC (FW-7…13). Despărțirea în silabe, fonturile, paginatorul, Knuth–Plass (CONV-4…6, 8; LAY-3…5). E2, E3 (DevKit), E5, E6, E7. PCB-1, PCB-2 încep în feb. Ciorna capitolului 2. | **M2 (28 feb.):** firmware-ul complet ca funcții pe prototip; referința DevKit măsurată |
| **mar. – apr.** | PCB-3…8: comanda pe la **~15 mar.**, plăcile pe la ~1 apr., punerea în funcțiune. E4 (DevKit). ENC. Rezervă pentru refacere în aprilie. Ciornele capitolelor 3–7. | **M3 (30 apr.):** dispozitivul merge pe PCB-ul propriu, în carcasă |
| **mai** | E2/E3/E4 pe PCB, E8/E9 opțional. Doar reparații de erori, fără funcții noi. Ciorna capitolului 8. | **M4 (31 mai):** toate datele măsurătorilor adunate; funcțiile înghețate |
| **iun. – iul.** | Lucrarea completă, runde de revizuire cu coordonatorul, prezentarea, videoul, susținerea de probă. Săptămâni de rezervă. | **M5:** predată |

**Regulă:** dacă o etapă întârzie mai mult de 2 săptămâni, tăiem din cele de dorit (FB2, fonturi de 2 bpp, E8/E9, controlul rândurilor văduve) înainte să ne atingem de cele obligatorii.

## 8. Priorități

| Obligatoriu | De dorit | Ambiție |
|---|---|---|
| Prototipul, driverul EPD propriu, platforma de firmware, aplicațiile, managerul de energie | Intrarea FB2 | Circuitul de comandă al panoului gol pe PCB v2 |
| Specificația formatului + convertorul (TXT, EPUB) | Stocare de masă USB | 4 niveluri de gri / fonturi cu antialiasing |
| Paginarea: rearanjare, justificare, despărțire în silabe, 3+ mărimi | Knuth–Plass (greedy e rezerva) | Pagină Wi-Fi pentru încărcarea cărților |
| PCB-ul propriu v1 + carcasa | Comparația indicator de baterie IC față de ADC | Studiu de utilizare (E9) |
| Măsurătorile E1–E7 | Controlul rândurilor văduve/orfane | |

## 9. Lista de cumpărături

Prețurile sunt aproximative (RON). Le verific pe cele actuale înainte de fiecare cumpărătură.

**Acum (Faza 0–1)**
- [ ] Multimetru UT33A, dacă trusa de lipit n-are unul: ~92. **Trebuie să aibă domeniu de µA DC** pentru măsurătorile de somn adânc. UT33A are (200/2000 µA sau 400/4000 µA, după versiune).
- [ ] 3–5 butoane tactile, comutator glisant SS12D00, placă de prototipare, barete mamă, cablu JST PH2.0: ~40–60
- [ ] Fire jumper Dupont mamă–mamă, ~40 buc., pentru testul de fum dinainte de lipire: ~10–15 (sari dacă le ai)
- [ ] Flux (pix sau pastă), dacă trusa n-are: ~15–25
- [ ] De verificat că ai: 2 rezistoare de 100 kΩ și un condensator de 100 nF (măsurarea bateriei)
- [ ] Analizor logic USB, 24 MHz, 8 canale (clasa clonelor Saleae): ~50–90. Necesar pentru E1/E2 și depanarea driverului.
- [ ] Modul senzor de curent INA226: ~25–45 (INA219 merge ca rezervă). [Sigmanortec](https://sigmanortec.ro/en/current-and-power-monitor-sensor-ina226-i2c-iic-bidirectional-16bit-27-55v)
- [ ] Placă de înregistrare pentru INA226. Vechiul tău ESP32-WROOM-32D, dacă îl mai ai; altfel un ESP32-C3 SuperMini, ~27 la [Sigmanortec](https://sigmanortec.ro/en/esp32-c3-supermini-development-board-33v-wifi-bluetooth) (2026-10-25).

**Faza 3 (martie)**
- [ ] Comanda PCB, 5 plăci + asamblare + livrare: ~30–60 $ (~140–280)
- [ ] Un LiPo de rezervă, de același tip: ~30–50
- [ ] Printarea carcasei: depinde de Q3
- [ ] Tester de alimentare USB-C, pentru E8: ~40–60 (opțional)

**Doar pentru ambiție:** un panou GDEY037T03 gol, ~10–15 $.

## 10. Riscuri

| Risc | Probabilitate | Impact | Ce facem |
|---|---|---|---|
| Ciudățenii ale driverului EPD (urme, LUT-uri pentru reîmprospătarea parțială) | Medie | Mediu | GxEPD2 ca referință, analizor logic, E7 pentru alegerea lui N |
| Knuth–Plass prea lent sau prea mâncăcios de memorie pe paragrafe lungi | Medie | Mic | Limită de memorie + revenire la greedy, măsurat în E6 |
| PCB v1 are o eroare | Medie | Mare | Lista de verificare, puncte de test, punere în funcțiune cu curent limitat, fire de reparație, rezervă pentru refacere în aprilie. Prototipul rămâne o demonstrație de rezervă funcțională. |
| Lipirea pieselor cu pas fin | Mare dacă e făcută manual | Mediu | Piesele astea le asamblează JLCPCB |
| Timpul tău disponibil (serviciu, examene) | Mare | Mare | Simulatorul lasă software-ul să avanseze fără hardware. Sarcinile practice se grupează și primești o listă de verificare înainte de fiecare. |
| Deteriorarea sau aprinderea LiPo | Mică | Mare | Circuit protejat, verificarea polarității (HW-5), la început nu încărca niciodată nesupravegheat, bateria prinsă în carcasă |
| Regulile facultății (formatul, aprobarea temei) se bat cap în cap cu planul | Necunoscută | Mare | TH-1 în Faza 0 |
| Muncă pierdută | Mică | Mare | git din prima zi + depozit la distanță (Q6) |
| Glife lipsă sau diacritice stricate | Medie | Mediu | Verificarea acoperirii CONV-5, cărți RO/RU în corpus din prima zi |

## 11. Decizii

**Ale tale**
- 2026-10-25: Măsurătorile și motorul de paginare a textului sunt obligatorii.
- 2026-10-25: Sistem de operare propriu pe ESP32 (FreeRTOS + straturile noastre), nu o distribuție Linux.
- 2026-10-25: Lucrarea în română, ~40 de pagini. Tema e aprobată.
- 2026-10-25: PCB propriu, comandat cu livrare în România. *(Așa am înțeles „cum pot face rost de PCB-ul propriu în România”. Corectează-mă dacă e greșit.)*
- 2026-10-26: Formatul de carte se numește IBV, *Ink Book Volume* (și inițialele tale), extensia `.ibv`.
- 2026-10-27: Fonturile cărților: Libron pentru alfabetul latin, PT Serif pentru chirilică, alese automat după limba cărții (LAY-7).
- 2026-10-27: Tot depozitul e în română: documentația, specificațiile, comentariile din cod, textele de pe dispozitiv și din unelte, mesajele de commit. Identificatorii rămân în engleză.

**Deciziile mele** (poți schimba oricare; fiecare primește un motiv în lucrare)
- C pe ESP-IDF; propriul nostru driver UC8253
- Nucleu C portabil comun cu un simulator pe PC
- Format binar rearanjabil, despărțirea în silabe făcută în convertor ca cratime moi, ruperea în rânduri pe dispozitiv
- Întâi rupere greedy în rânduri, apoi Knuth–Plass
- Compresie pe bloc de ≤ 32 KB (miniz/DEFLATE din ROM), măsurată în E5
- Convertor în Python (doar linie de comandă, fără interfață grafică)
- PCB v1 păstrează modulul de ecran WeAct; IC indicator de baterie; placă cu 4 straturi
- JLCPCB cu asamblare
- INA226 + analizor logic pentru măsurători
- Metodele scrise înainte de experimente
- Structura depozitului din secțiunea 5
- Testul de fum din Faza 0: propria noastră aplicație ESP-IDF de punere în funcțiune în loc de o schiță Arduino + GxEPD2 (2026-10-25). Testează driverul nostru din prima zi. Dacă panoul rămâne gol, exemplul GxEPD2 e rezerva, ca să deosebim o greșeală de cablaj de o eroare de driver.

## 12. Întrebări deschise

Fiecare dintre ele schimbă planul:

- ~~Q1~~ Răspuns: română, ~40 de pagini, temă aprobată. Încă de obținut: fișierul șablon și stilul de citare (TH-1).
- **Q2** Data exactă a predării și a susținerii.
- **Q3** Ai acces la o imprimantă 3D (facultate, maker space, un prieten) sau plănuim un serviciu de printare online?
- **Q4** Ambiție: PCB v2 să comande panoul gol cu propriul nostru circuit de ridicare a tensiunii? E mai multă electronică, dar costă ~3 săptămâni în plus, plus riscul.
- ~~Q5~~ Răspuns: formatul de carte e **IBV** (*Ink Book Volume*), extensia `.ibv`.
- ~~Q6~~ Răspuns: depozitul e pe GitHub, public: [iliev-blnk/eink-reader](https://github.com/iliev-blnk/eink-reader).
- **Q7** Cam câte ore pe săptămână poți da sarcinilor practice?
