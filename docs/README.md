# Materiale pentru documentația diplomei

Materialul brut pentru redactarea lucrării de diplomă despre cititorul e-ink.

- `MASTER-PLAN.md`: planul general. Direcțiile de lucru, sarcinile, calendarul și etapele, planul de măsurători, lista de cumpărături, riscurile, deciziile și întrebările deschise. Singura sursă de adevăr.
- `project-brief.md`: ce e proiectul, lista pieselor, cumpărăturile deschise, note despre ecran și alimentare, ce cuprinde firmware-ul și planul formatului de carte.
- `../hardware/`: schema KiCad (`eink-reader.kicad_sch`), exporturile ei PDF/SVG și `gen.py`, care le generează.
- `../firmware/`: firmware-ul ESP-IDF. README-ul lui descrie compilarea, scrierea în flash și conexiunile.
- `../sim/`: simulatorul pe PC. Compilează nucleul portabil al firmware-ului (fonturi, cititorul IBV, paginare) și desenează ecrane și pagini de carte în PNG, cu statistici pe cărți întregi și teste cu imagini de referință. Vezi README-ul lui.
- `../tools/fonttool/`: TTF/OTF → fonturi bitmap IBF, codecul de referință al formatului de font (`ibf.py`) și testele. Vezi README-ul lui.
- `../tools/converter/`: convertorul de cărți (EPUB / FB2 / TXT → `.ibv`), cititorul, scriitorul și validatorul de referință al formatului (`ibv.py`) și testele lor. Vezi README-ul lui.
- `../corpus/`: cărțile de test (le descarcă `fetch.sh`; neurmărite în git) și rezultatele conversiei lor.
- `../spec/`: specificația formatului de carte (`book-format.md`), specificația formatului de font (`font-format.md`) și un fișier exemplu comentat (`examples/`).

## Jurnal

### 2026-10-25: schema v1.0
- Făcută în KiCad 10, pentru că sPlan și Proteus folosesc formate închise care nu pot fi generate.
- Module: ESP32-S3-DevKitC-1 16 MB (PSRAM neconfirmat; IO35–37 lăsați liberi în caz că e N16R8), EPD WeAct 3,7" (UC8253, SPI), modul microSD, TP4056 (protecție DW01), buck-boost TPS63020 de 3,3 V, LiPo 1S.
- Traseul alimentării: LiPo → TP4056 B+/B− → OUT+ → SW1 → VSYS → TPS63020 → +3V3 (pinul 3V3 al DevKit-ului, EPD, SD). GND-ul sistemului = TP4056 OUT−.
- EPD-ul și cardul SD împart SPI2: SCK IO12, MOSI IO11, MISO IO13. EPD CS/DC/RST/BUSY = IO10/9/8/18, SD CS = IO14.
- Butoanele PREV/NEXT/MENU pe IO4/5/6 (cu RTC, ca să poată trezi din somn adânc), legate la GND cu pull-up intern. Măsurarea bateriei pe IO1 printr-un divizor 100k/100k plus 100 nF.
- Pini evitați: IO35–37 (folosiți de PSRAM-ul octal la N16R8) și pinii de strapping 0/3/45/46.
- ERC: 0 încălcări.

### 2026-10-25: descrierea proiectului adunată
- Notele de planificare de mai devreme (piese, ecran, alimentare, firmware, formatul cărții) adunate în `project-brief.md`.
- Schema verificată față de ele și se potrivește: alimentare pe pinul 3V3 (nu 5V), SPI comun cu pini CS separați, butoane de trezire pe GPIO-uri RTC, indicator 100k/100k + 100 nF.
- De făcut pe hardware: setarea padului SPI pe 4 fire al EPD-ului și verificarea punților de alimentare SB3/SB4/SB5 înainte de prima alimentare. Verificarea polarității JST la LiPo.

### 2026-10-25: planul general, ciorna v0.9
- Decizii din brainstorming: măsurătorile și motorul de paginare a textului sunt obligatorii. Sistem de operare propriu pe ESP32 (FreeRTOS + straturile noastre), nu Linux, pentru că S3 nu are MMU, iar o distribuție ar fi mai mult configurare decât muncă originală. S-a adăugat un PCB propriu, pentru că se potrivește jumătății de electronică a specializării și rezolvă curentul de somn de ordinul mA al DevKit-ului, ceea ce dă și o măsurătoare înainte/după. Termen ~9 luni (sfârșitul lui iulie 2027).
- Scris `MASTER-PLAN.md`: 10 direcții de lucru, etapele M0–M5, experimentele E1–E9, întrebările deschise Q1–Q7.
- Planul aprobat ca v1.0. Răspunsurile despre lucrare notate (în română, ~40 de pagini, tema aprobată). Lista de cumpărături rafinată: multimetru cu domeniu de µA obligatoriu, fire jumper și flux adăugate, linkuri Sigmanortec.

### 2026-10-25: firmware Faza 0, scheletul și aplicația de punere în funcțiune
- Proiect ESP-IDF v6.1 în `firmware/`. Se compilează pentru ESP32-S3 fără avertismente, iar aplicația ocupă 343 KB din partiția de 4 MB. Hărțile pinilor plăcilor sunt o componentă separată (`boards/`), ca PCB-ul propriu să se poată adăuga mai târziu ca a doua placă.
- Driver propriu pentru ecranul UC8253, scris după setul de comenzi: reîmprospătare completă (normală și o formă de undă „rapidă” de ~1 s), reîmprospătare parțială a unei ferestre, somn adânc, BUSY pe întrerupere în loc de interogare. Fiecare reîmprospătare înregistrează timpul petrecut în fiecare fază (scriere SPI, pornire, reîmprospătare, oprire), adică datele brute pentru E1.
- Drivere pentru cardul SD (FAT32 pe magistrala SPI comună, nu formatează niciodată), butoane (debounce, apăsare scurtă și lungă, coadă de evenimente, trezire din somn adânc) și ADC-ul bateriei (calibrare din eFuse).
- Aplicația de punere în funcțiune: arată cipul, memoria, SD-ul, bateria și motivul trezirii; PREV/NEXT testează reîmprospătarea parțială, MENU pe cea completă, MENU ținut apăsat somnul adânc și trezirea din buton. Tipărește rânduri `PERF,...` la fiecare reîmprospătare și adaugă câte un rând la fiecare pornire în `bringup.txt` pe card.
- Nucleul portabil: framebuffer pe 1 bit, text UTF-8, un font bitmap Noto Sans de 16 px (românesc cu ș ț cu virgulă dedesubt, chirilic). Simulatorul desenează aceleași ecrane pe PC. Acolo s-au verificat cele mai lungi șiruri pe care le poate produce firmware-ul, ca să încapă în lățimea de 240 px.
- Schimbare față de plan: testul de fum folosește această aplicație în loc de o schiță Arduino + GxEPD2, așa că driverul nostru e testat de la prima alimentare. GxEPD2 rămâne rezerva dacă panoul rămâne gol.
- Încă nerulat pe hardware: piesele nu sunt încă legate.

### 2026-10-26: macheta carcasei (3D, mărime reală)
- Machetă 3D interactivă a trei variante de carcasă pe un covoraș de tăiere A3 lângă o banană de 19 cm: A butoane jos (58 × 132 × 12 mm), B butoane lateral (82 × 110 × 12 mm, recomandată), C butoane pe margine (58 × 110 × 13 mm).
- Geometria ecranului e din desenele GDEY037T03 / WeAct. Mărimile carcaselor sunt estimări până se proiectează PCB-ul.
- Ecranele sunt desenate cu codul gfx și de fonturi al firmware-ului (`docs/mockup/mock.c`). Paginile de lectură folosesc cratime moi din pyphen și o rupere în rânduri pe tot paragraful, o previzualizare timpurie a direcției LAYOUT. Ecranele Sleep și Bring-up sunt exact ce desenează firmware-ul de azi, cu valori hardware de exemplu.
- Refacerea: comenzile din antetul `docs/mockup/mock.c`, apoi `python3 docs/mockup/build.py docs/mockup/screens docs/mockup/template.html docs/mockup/eink-mockup.html`.
- Urmează: alegerea unei carcase, apoi ENC-1 (CAD) și conturul PCB-ului.

### 2026-10-26: specificația formatului de carte v0.1 (SPEC-1)
- `spec/book-format.md`: structura la nivel de octet a antetului, directorului de secțiuni, META, TOC, indexului de blocuri, înregistrărilor și copertei; algoritmul de citire pe dispozitiv și bugetul de RAM; fișierele însoțitoare; regulile validatorului; versionarea; un tabel al alegerilor de proiectare cu alternativele respinse; mărimi preliminare. Numit IBV (Q5), extensia `.ibv`.
- Codecul de referință `tools/converter/ibv.py` (scriere, citire, validare, afișare și `from-lines` pentru textele machetei) cu 15 teste. Un test inversează fiecare bit al unui fișier exemplu; a găsit că o etichetă de director stricată trecea neobservată, așa că antetul a primit un CRC al directorului.
- Fișierul exemplu `spec/examples/example.ibv` (240 B, necomprimat), comentat octet cu octet în specificație. Se reface cu `python3 spec/examples/make_example.py`.
- Două schimbări față de plan: compresie pe bloc de ≤ 32 KB în loc de pe capitol (cel mai lung capitol din Moara are 37 KB, iar o carte TXT e un singur capitol) și fișiere însoțitoare identificate după un hash al conținutului în `/.ibv/` în loc de `<book>.pos`.
- Mărimi preliminare pe cele patru cărți ale machetei: 35–50 % din textul simplu la blocuri de 16 KB, cam 1,3× gzip pe tot fișierul. Cratimele moi adaugă 18,5 % la textul brut din Moara. Încă nu e E5 (îi trebuie corpusul de 20 de cărți și timpi pe dispozitiv).
- Q5 decisă: formatul e **IBV**, *Ink Book Volume* (și inițialele autorului), extensia `.ibv`. Codecul redenumit `tools/converter/ibv.py`.
- Urmează: revizuirea SPEC-2.

### 2026-10-26: convertorul de cărți (CONV-1…7, 9, 10)
- `tools/converter/convert.py`: `convert` (EPUB 2/3, FB2, `.fb2.zip`, TXT; o carte sau un lot), `validate`, `dump`, `preview-cover`. Fiecare rezultat e validat înainte de a fi scris.
- Cititoarele `txt.py`, `epub.py`, `fb2.py` umplu un `doc.Builder` comun. `doc.finish()` normalizează (`textnorm.py`), înlocuiește glifele lipsă (`glyphs.py`), transformă rândurile scurte legate cu `<br>` în versuri, ordonează separatoarele, desparte în silabe proza (`hyphenate.py`, Liang propriu + modele TeX), împarte paragrafele lungi și rezolvă cuprinsul. Coperte: `cover.py`, Floyd–Steinberg în serpentină la 1 bpp.
- TXT: codificările se punctează pe cuvânt (ambele fișiere Dubrovski, KOI8-R și CP1251, dau cărți identice la nivel de octet); antetele Gutenberg și lib.ru devin metadate; cuprinsurile tipărite nu devin intrări de cuprins.
- Validatorul: textul trebuie să fie și NFC (adăugat la regula 5 din specificație).
- Teste: `test_convert.py`, 51 de teste pe intrări sintetice EPUB/FB2/TXT plus `TestCorpus`; 66 cu `test_ibv.py`, toate trec.
- Corpusul (MEAS-1, parțial): `corpus/fetch.sh` descarcă 15 fișiere / 13 cărți (EN 6, RO 5, RU 4, cu tot cu copiile TXT). Toate se convertesc și se validează în 0,4–3,2 s fiecare; tabelul e în `corpus/README.md`. Moby-Dick: 7 litere grecești/ebraice lipsă din font.
- Încă deschis: o carte FB2 reală și un TXT românesc CP1250 pentru corpus; încă ~5 cărți până la 20; CONV-8 (fonttool) separat; deschiderea cărților pe dispozitiv are nevoie de LAY/FW.

### 2026-10-27: documentația în română (pasul 1 din 3)
- Proiectul e pentru facultate, deci tot depozitul trece în română: documentele și specificațiile, comentariile din cod, textele de pe dispozitiv și din unelte, mesajele de commit de acum înainte. Identificatorii (variabile, funcții, nume de fișiere, nume de câmpuri din formate) rămân în engleză. Decizia e trecută în planul general, secțiunea 11.
- Pasul 1, făcut: planul general, descrierea proiectului, acest jurnal, cele două specificații (IBV și IBF) și fiecare README (firmware, simulator, convertor, modele de despărțire, fonttool, corpus). Octeții, câmpurile, etichetele și codurile magice din exemplele hex au rămas neschimbate; doar comentariile lor sunt traduse.
- Rămân: pasul 2, comentariile din C și Python; pasul 3, textele afișate de dispozitiv, simulator și unelte (după care se verifică imaginile de referință).

### 2026-10-27: despărțirea în rânduri prin încadrare totală, Knuth–Plass (LAY-5)
- Paragrafele și citatele aliniate stânga-dreapta se rup acum în rânduri tot odată (Knuth și Plass, 1981, în forma din TeX), nu rând cu rând (greedy). Dintre toate felurile de a rupe paragraful se alege cel cu penalizarea totală cea mai mică: un rând costă cu atât mai mult cu cât spațiile lui se lățesc sau se îngustează (100·r³), iar cratimele, două cratime la rând și o cratimă pe penultimul rând costă în plus.
- Zona de lucru `layout_fit_t` (~66 KB, `cfg.fit`) e dată de apelant, câte una pe fiecare task care paginează, fără variabile globale. Ea păstrează rupturile ultimului paragraf, ca paginile unui paragraf lung să nu le recalculeze. Cu `cfg.fit = NULL` totul rămâne greedy. Greedy e și rezerva pentru un paragraf cu peste 2048 de puncte de rupere (în corpus maximul e ~2000) sau cu un cuvânt mai lat decât rândul.
- Determinismul s-a păstrat: rupturile depind doar de textul înregistrării și de cele două lățimi de rând, deci o pagină care începe în mijlocul paragrafului le primește la fel. `sim stats` (pagini înlănțuite, pagina dinainte, găsirea poziției, indexul) trece pe toate cele 15 cărți.
- Măsurat cu `sim stats` (coloane noi pentru E6: % rânduri cu cratimă, cratime consecutive, r = cât de lat e un spațiu justificat față de unul simplu: media, percentila 95, % rânduri cu r > 0,5). Pe 7 cărți în fonturile dispozitivului, față de greedy: mai puține cratime pe 6 din 7 (pg2701 la egalitate), de 2–10 ori mai puține cratime consecutive (P&P 1083 → 588), spații cu 25–35 % mai puțin lățite (media r 1,57 → 1,14, p95 4,26 → 2,82) și 1–2 % mai puține pagini. Pe PC durează ~1,5× cât greedy pe pagină (~25 µs); timpul pe dispozitiv se măsoară în E6.
- Constantele s-au ales printr-o baleiere (`-DSTRETCH_NUM`, `-DHYPHEN` etc.): cu spațiul care se lățește doar cu ½ din el, ca în TeX, la 216 px doar ~10 % din paragrafe treceau de prima trecere, iar penalizările pentru cratime nu mai contau. Cu lățire 2× trec 75–80 %, iar cratima 100 / două cratime 100000 dau compromisul de mai sus. Badness-ul nu mai e plafonat la 10000, ca a doua trecere să deosebească un rând larg de unul și mai larg.
- `sim --greedy` pentru comparații, `SIM_FIT=1` afișează câte paragrafe au trecut de fiecare trecere. `LAYOUT_VERSION` 3, iar hash-ul de layout include modul de rupere. Imaginile de referință ale configurației justificate s-au regenerat după o verificare vizuală (cea ragged nu s-a schimbat).

### 2026-10-27: fonturile cărților decise, alese după limbă (LAY-7)
- Decizia (luată de autor după foile de comparație): Libron pentru cărțile în alfabet latin, PT Serif pentru cele în chirilică. Cititorul nu alege niciodată o familie: dispozitivul o ia din `language` din META cărții.
- `fontsel.c` (nucleul portabil): `fontsel_cyrillic(lang)` citește eticheta BCP 47 (limbi chirilice ru, uk, be, bg, sr, mk, kk și altele; o subetichetă de scriere `Cyrl` sau `Latn` are prioritate, deci `sr-Latn` e Libron), `fontsel_pick(lang, step)` dă familia și mărimea, `fontsel_path` numele fișierului din `/fonts/`.
- O singură setare de mărime cu cinci trepte, fiecare o pereche de mărimi potrivite după înălțimea x și după numărul de pagini (Pride and Prejudice: Libron 15/16/17/18/20 = 1.541/1.845/2.081/2.370/2.844 de pagini, PT Serif 16/17/18/19/21 = 1.601/1.803/2.042/2.302/2.753). Implicit e treapta 3, Libron 17 / PT Serif 18, așa că cine trece de la o carte românească la una rusească vede aceeași mărime a textului.
- Convertorul verifică acum fiecare carte după caracterele fontului ei (`glyphs.charset_for`): o carte în alfabet latin nu primește chirilice și nici Ĳ ĳ ŉ ſ (ſ → s). Înainte, un cuvânt chirilic într-o carte englezească ar fi trecut de convertor și ar fi apărut ca pătrățele pe dispozitiv. Un test verifică că lista de limbi a convertorului se potrivește cu `fontsel.c`. Trec 69 de teste ale convertorului.
- `sim --fonts DIR [--size N]` alege fontul pe carte ca dispozitivul; `stats` are o coloană nouă `miss` (caractere fără glifă în fontul cărții). `run.sh` paginează corpusul a doua oară în fonturile dispozitivului și eșuează dacă vreo carte are o glifă lipsă. Toate cele 15 trec cu 0 (cu Libron forțat pe *Dubrovski* ies 108.792, deci verificarea funcționează). Firmware-ul se compilează.

### 2026-10-27: Libron ca al patrulea font candidat (LAY-7)
- Libron (Nico Verbruggen, OFL, din Readerly / Newsreader) e făcut pentru cititoare electronice. Acoperă complet româna, cu tot cu ș ț și formele cu sedilă, dar **nu are chirilice**, deci o carte rusească culeasă cu el ar arăta pătrățele U+FFFD. Folosirea lui înseamnă un al doilea font pentru cărțile chirilice (ales după limba cărții) sau Libron doar pentru cărțile în alfabet latin.
- Versiunea simplă are spațiere între rânduri foarte strânsă (rânduri de 19 px la 18 px), așa că accentele de pe ș și ț aproape ating rândul de deasupra. Versiunea „R” are spațiere relaxată și pe ea o folosim: la 16 px are rând de 23 px și înălțime x de 9 px, cam cât Literata 16.
- E îngust. Pagini pentru Pride and Prejudice / Slavici / Frankenstein: Literata 16 2.105 / 767 / 1.214; PT Serif 18 2.042 / 747 / 1.170; Libron 16 1.845 / 671 / 1.056; Libron 17 2.081 / 756 / 1.198; Libron 18 2.370 / 857 / 1.365. Libron 17 e cam la fel de dens ca cei doi favoriți de până acum, iar literele lui mai înguste lasă mai puține goluri late în rândurile justificate.
- `fetch-fonts.sh` descarcă Libron_R v0.32, iar `fonttool.py family libron` îl compilează (fără hinting, deci desenare gray cu pragul 128), 31–36 KB pe față. Foile sunt în `tools/fonttool/out/sheets/libron-vs-{en,ro}.png`.
- Ca și PT Serif, Libron are un Reserved Font Name. Bitmapurile noastre IBF sunt o versiune convertită, deci înainte de lansare numele afișat pe dispozitiv trebuie verificat față de OFL (sau cerută permisiunea autorului).

### 2026-10-27: rânduri văduve/orfane, pagina înapoi, indexul paginilor (LAY-3, cea mai mare parte din LAY-4)
- Controlul rândurilor văduve și orfane: când o pagină se umple în mijlocul unui paragraf, rândurile se mută ca niciun paragraf să nu lase un singur rând jos sau sus pe pagină (un paragraf de 2 rânduri se mută întreg). Un titlu rămas jos pe pagină se mută cu ce urmează. Pe corpus scade rândurile văduve și orfane de la până la 667 / 687 (pg1322, poezie) la 0 în fiecare carte, cu 0,5–4,7 % mai multe pagini. Se oprește cu `--no-keep-lines`; face parte din `layout_hash`.
- Pagina înapoi: rupturile de pagină depind de paginile dinainte, așa că paginarea pornește înainte de la cel mai apropiat *punct de sincronizare* până ajunge la pagina curentă. La început singurele puncte de sincronizare erau titlurile de nivel 1, dar într-un EPUB nivelul 1 e de obicei doar titlul cărții, deci o pagină înapoi costa toată cartea.
- Rezolvarea: un indicator nou în format, `PAGE` (bitul 6, jurnalul de schimbări al specificației 0.1): „înregistrarea asta începe o pagină când capitolele încep pagini”. Convertorul îl pune după o euristică: un nivel de titlu e nivel de capitol dacă mediana lungimii secțiunilor lui e ≥ 3.000 B, iar un titlu de pe el primește `PAGE` dacă propria secțiune are ≥ 750 B (ca poeziile scurte care împart nivelul cu capitolele să nu-l primească) și nu urmează direct după alt titlu. Fișierele din spine au fost respinse ca semne de capitol: Gutenberg le împarte după mărime, în mijlocul textului. Testul `test_chapters_start_pages`; trec 67 de teste ale convertorului.
- O pagină înapoi paginează acum în medie 13–56 de pagini (maximum 191) în cărțile cu capitole. Cele fără (pg1322, pg14741, pg16527, pg35323) încă costă cam jumătate de carte, deci se bazează pe index.
- Indexul paginilor (`pageidx.c`): începutul fiecărei pagini, 8 B pe pagină, construit treptat, căutat binar, salvat ca fișierul însoțitor `.pg`. FATFS nu poate redenumi peste un fișier, deci o salvare îl șterge întâi pe cel vechi (secțiunea 6 a specificației corectată). Tot corpusul la fontul implicit de interfață: 7–68 ms pentru indexarea unei cărți pe PC, 16–70 µs pe pagină.
- `sim stats` verifică pe fiecare carte: paginile se înlănțuie, fiecare `layout_prev` ajunge la începutul precedent, `layout_locate` și `pageidx_find` sunt de acord, pagina dinaintea primei e RANGE, fișierul însoțitor se salvează și se recitește corect și respinge o altă paginare sau un octet inversat. Trec toate cele 15 cărți. Imaginile de referință actualizate după verificare (rânduri orfane mutate, un titlu de la final mutat, subsoluri „n / 10”).
- Firmware-ul încă se compilează. Încă deschis: taskul de paginare în fundal pe dispozitiv (cu FW-5), Knuth–Plass (LAY-5), alegerea fontului LAY-7.

### 2026-10-26: fonturi, motorul de paginare și simulatorul de cărți (SPEC-3, CONV-8, LAY-1, 2, 6, o parte din 3)
- Formatul de font IBF v0.1, `spec/font-format.md`: antet de 96 de octeți, intervale Unicode, tabelul glifelor, bitmapuri, kerning în 1/64 px. Literele cu diacritice împrumută kerningul literei de bază (`kern_as`) și se păstrează doar perechile unde asta e greșit, ceea ce a dus Literata de la 7.596 de perechi (45 KB) la 3.844. Glifele lipsă primesc înlocuitori (U+2011 → '-', o literă cu diacritic → litera de bază).
- `tools/fonttool`: `build`, `family`, `c` (tabele C pentru fonturile de interfață incluse), `validate`, `dump`, `sheet`; codecul de referință `ibf.py`; 23 de teste. Literata, PT Serif și Noto Serif se compilează în 4 stiluri la 16/18/20 px, 19–41 KB pe față.
- O eroare arătată de primele pagini desenate: Literata schimbă glifele după mărimea optică (OpenType feature variations). Avansurile se măsurau prin HarfBuzz, dar bitmapurile se desenau doar cu FreeType, deci veneau de la glife diferite și fiecare „w” cursiv avea un gol după el. Acum și bitmapurile trec prin HarfBuzz; un test acoperă cazul.
- Nucleul firmware-ului (C portabil, compilat și de simulator): `font.c` (încarcă un IBF pe loc și verifică fiecare regulă din specificație), `gfx_glyph` (blit cu deplasare pe octet), `crc32.c`, `ibv.c` (cititorul IBV: antet, secțiuni, cache de două blocuri, CRC pe bloc, intervale de stil), `inflate_rom.c` (miniz din ROM-ul ESP32-S3; simulatorul folosește zlib), `layout.c` (rupere greedy în rânduri cu cratime moi și justificare în 1/64 px, paginare). Firmware-ul încă se compilează: aplicație de 346 KB.
- Regulile de paginare: PARA justificat cu alineat; QUOTE cu ambele margini; VERSE la stânga cu alineat agățat; HEADING centrat, ținut cu două rânduri după el, nivelul 1 pe pagină nouă; BREAK ca rând gol sau „* * *”, scos sus pe pagină; CONT continuă fără alineat sau spațiu. Rupturi la spații, cratime moi, U+200B și linii din interiorul cuvintelor; U+00A0 se lățește, dar nu rupe.
- `sim`: `pages` (carte → PNG-uri, cu copertă și subsol: capitol + procent), `stats` (paginează cărți întregi, verifică că paginile se înlănțuie și avansează și cât iese cerneala din zona de text, cronometrează, face hash pe fiecare pagină), `compare` (o pagină alăturat în mai multe fonturi). Testul cu imagini de referință `sim/test/run.sh` pe `sim/test/layout.ibv`, o carte de test cu fiecare tip de înregistrare și caracter special.
- Toate cele 15 cărți din corpus se paginează în fiecare font: 20–50 µs pe pagină pe PC, cerneala iese cel mult 1–4 px din zona de text (cursivele). Pride and Prejudice: 2.057 de pagini în Literata 16, 1.994 în PT Serif 18.
- Gata pentru LAY-7: foi de comparație ale celor trei fonturi (EN și RU) și ale Literatei la 16/18/20.
- Încă deschis: LAY-3 rânduri văduve/orfane și paginarea înapoi (LAY-4), Knuth–Plass (LAY-5, greedy lasă goluri late pe rânduri de 216 px), rularea paginării pe dispozitiv (are nevoie de prototip).
