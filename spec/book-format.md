# Formatul de carte IBV, v0.1

Stare: **ciornă pentru revizuire (SPEC-2).** Scrisă pe 2026-10-26.
Implementarea de referință: `tools/converter/ibv.py` (cititor, scriitor, validator). Teste: `tools/converter/test_ibv.py`.
Exemplu lucrat: `spec/examples/example.ibv`, generat de `spec/examples/make_example.py`.

**Numele.** IBV, *Ink Book Volume*, care sunt și inițialele autorului. Extensia `.ibv`, dosarul fișierelor însoțitoare `/.ibv/` (Q5, decis pe 2026-10-26).

Când documentul ăsta și `ibv.py` nu sunt de acord, unul dintre ele are o eroare. Se repară, apoi se adaugă un rând în jurnalul de schimbări de la final.

---

## 1. La ce folosește formatul

Dispozitivul nu parsează niciodată EPUB, FB2 sau HTML. Un convertor pe PC face toate astea o singură dată și scrie formatul ăsta. Dispozitivului îi rămâne doar:

1. să listeze cărțile ieftin: citește antetul și metadatele, câteva sute de octeți;
2. să deschidă o carte ieftin: citește indexul blocurilor, câțiva KB;
3. să ia repede textul din jurul oricărei poziții, cu **RAM mărginit**: decodează cel mult un bloc de ≤ 32 KB;
4. să pagineze singur textul la orice mărime de font și margini a ales cititorul, pentru că formatul e **rearanjabil**.

Tot ce e scump sau ține de limbă se face pe PC:
- normalizarea Unicode
- ş/ţ românești → ș/ț
- despărțirea în silabe, păstrată ca cratime moi
- detectarea structurii
- compresia, făcută cu cele mai bune setări pentru că rulează o singură dată

Dispozitivul face doar ce depinde de ecran: ruperea în rânduri, justificarea și paginarea.

## 2. Convenții

- Toți întregii sunt **little-endian**, ordinea nativă a ESP32, deci dispozitivul îi citește cu încărcări simple.
- `u8/u16/u32` sunt fără semn. Deplasamentele sunt absolute, în octeți de la începutul fișierului, deci fișierele sunt limitate la 4 GiB.
- `varint` = LEB128 fără semn: 7 biți pe octet, grupul de jos primul, bitul de sus = „mai urmează octeți”. Valorile trebuie să fie < 2³², deci un varint are cel mult 5 octeți. Varinturile apar doar în blocurile de text, unde majoritatea valorilor sunt mici.
- Textul e **UTF-8, normalizat NFC**.
- `CRC32` e CRC-32 standard folosit de zlib, PNG și Ethernet (reflectat, polinomul 0xEDB88320, valoarea inițială și XOR-ul final 0xFFFFFFFF). Pe dispozitiv e `esp_rom_crc32_le()` din ROM.
- `DEFLATE` înseamnă DEFLATE brut (RFC 1951), fără antet zlib sau gzip. Pe dispozitiv e `tinfl_decompress()` din ROM-ul ESP32-S3 (`esp32s3.rom.ld:83`), deci nu ocupă spațiu în flash.

## 3. Structura fișierului

```
offset 0   ┌─────────────────────────────┐
           │ Antet               44 B    │  cod magic, versiune, book_id, numere, CRC-uri
       44  ├─────────────────────────────┤
           │ Directorul secțiunilor 16 B×n│ etichetă, deplasament, lungime, CRC32 pe secțiune
           ├─────────────────────────────┤
           │ META   metadate             │  titlu, autor, limbă, …
           │ TOC    cuprins              │  opțional
           │ BLKS   indexul blocurilor   │  câte o intrare de 24 B pe bloc de text
           │ COVR   coperta              │  opțional
           │ TEXT   blocurile de text    │  conținutul, bloc după bloc
           └─────────────────────────────┘
```

Dispozitivul găsește secțiunile doar prin director și nu presupune niciodată ordinea de mai sus. Scriitorul folosește ordinea asta ca toate secțiunile mici să vină primele: o singură citire de 4 KB ia de obicei antetul, directorul, META și BLKS împreună.

### 3.1 Antetul (44 de octeți)

| Deplasament | Mărime | Câmp | Valoare / semnificație |
|---:|---:|---|---|
| 0 | 8 | `magic` | `89 49 42 56 0D 0A 1A 0A` = `\x89IBV\r\n\x1a\n` |
| 8 | 1 | `version_major` | 0 |
| 9 | 1 | `version_minor` | 1 |
| 10 | 2 | `header_size` | 44. Directorul începe aici. Versiunile viitoare pot mări antetul; cititoarele sar ce nu cunosc. |
| 12 | 4 | `flags` | 0. Rezervat. |
| 16 | 8 | `book_id` | primii 8 octeți din SHA-256 peste conținutul META, apoi fiecare înregistrare codificată, în ordine (vezi 3.7) |
| 24 | 4 | `paragraph_count` | numărul de înregistrări din carte |
| 28 | 4 | `text_bytes` | suma lungimilor textului tuturor înregistrărilor, în octeți; folosită pentru „x %” până e gata paginarea |
| 32 | 2 | `section_count` | numărul intrărilor din director |
| 34 | 2 | rezervat | 0 |
| 36 | 4 | `directory_crc` | CRC32 al întregului director |
| 40 | 4 | `header_crc` | CRC32 al octeților 0–39 |

Codul magic e împrumutat de la PNG. Primul octet non-ASCII înseamnă că fișierul nu poate fi confundat cu text. Octeții `\r\n`, `\x1a` și `\n` prind un fișier stricat de o copiere în mod text (conversia capetelor de rând sau tăierea la Ctrl-Z).

### 3.2 Directorul secțiunilor (16 octeți pe intrare)

| Deplasament | Mărime | Câmp |
|---:|---:|---|
| 0 | 4 | `tag`: patru caractere ASCII, de exemplu `META`, `TOC ` (cu un spațiu) |
| 4 | 4 | `offset` |
| 8 | 4 | `length` |
| 12 | 4 | `crc32` al octeților secțiunii |

Reguli:
- Secțiuni obligatorii: `META`, `BLKS`, `TEXT`. Opționale: `TOC `, `COVR`.
- O etichetă apare cel mult o dată. **Cititoarele sar etichetele pe care nu le cunosc**, deci un convertor mai nou poate adăuga secțiuni fără să strice firmware-ul mai vechi.
- Dispozitivul verifică CRC-ul fiecărei secțiuni când o citește. CRC-ul lui `TEXT` acoperă tot conținutul, deci îl verifică doar validatorul. Dispozitivul verifică în schimb CRC-ul propriu al fiecărui bloc când îl decodează.

### 3.3 `META`: metadatele

```
u16 count
count × { u8 key, u16 length, length octeți de UTF-8 }
```

| Cheie | Nume | Obligatorie | Exemplu |
|---:|---|:---:|---|
| 1 | title | da | `Amintiri din copilărie` |
| 2 | author | | `Ion Creangă` |
| 3 | language | da | etichetă BCP 47: `ro`, `ru`, `en` |
| 4 | source | | numele fișierului original sau URL-ul |
| 5 | converter | | `ibv.py 0.1` |
| 6 | converted | | dată ISO 8601 |
| 7 | year | | anul publicării originale, ca text |
| 8 | description | | |

Fiecare cheie apare cel mult o dată, iar cititoarele sar cheile pe care nu le cunosc. Ecranul bibliotecii are nevoie doar de antet și META.

### 3.4 `TOC `: cuprinsul (opțional)

```
u16 count
count × { u32 para, u8 level (1–4), u8 reserved = 0, u16 length, length octeți de titlu UTF-8 }
```

`para` e indicele înregistrării la care sare intrarea, de obicei un HEADING. Intrările sunt ordonate după `para` strict crescător. Titlurile se păstrează fără cratime moi, deci lista capitolelor se poate desena fără a decoda vreun bloc. Un cuprins gol sau lipsă e valid, ceea ce e normal pentru intrările TXT.

### 3.5 `BLKS`: indexul blocurilor

```
u32 count
count × intrare de 24 de octeți:
```

| Deplasament | Mărime | Câmp | Semnificație |
|---:|---:|---|---|
| 0 | 4 | `offset` | deplasamentul absolut în fișier al blocului stocat |
| 4 | 4 | `stored_len` | octeți pe disc |
| 8 | 2 | `raw_len` | octeți după decodare, **≤ 32768** |
| 10 | 1 | `method` | 0 = stocat, 1 = DEFLATE. Alte valori: blocul nu poate fi citit. |
| 11 | 1 | rezervat | 0 |
| 12 | 4 | `first_para` | indicele primei înregistrări din bloc |
| 16 | 4 | `text_start` | totalul octeților de text ai tuturor înregistrărilor dinaintea blocului |
| 20 | 4 | `crc32` | CRC32 al octeților **decodați** (bruți) |

Invarianți, toți verificați de validator:
- Blocurile sunt lipite unul de altul. Blocul 0 începe la deplasamentul secțiunii TEXT, fiecare bloc următor începe unde se termină precedentul, iar ultimul se termină la sfârșitul lui TEXT.
- `first_para` și `text_start` pornesc de la 0 și cresc exact cu cât conține blocul precedent. Fiecare bloc conține `first_para[i+1] − first_para[i]` înregistrări; ultimul conține `paragraph_count − first_para[last]`.
- Fiecare bloc conține cel puțin o înregistrare întreagă și nicio înregistrare nu e împărțită între blocuri.

**De ce CRC-ul acoperă octeții decodați:** o singură verificare prinde o citire greșită de pe SD, o eroare în compresorul convertorului și o eroare în decompresorul dispozitivului. gzip face aceeași alegere.

### 3.6 `TEXT`: blocurile de text și înregistrările

Un bloc decodat e un șir de **înregistrări**. O înregistrare e un element de nivelul paragrafului:

```
u8     kind
u8     flags
varint text_len           octeți de text UTF-8
varint run_count
run_count × { varint start, varint length, u8 style }
text_len octeți de text UTF-8
```

**Tipuri**

| Valoare | Tip | Text | Paginarea implicită |
|---:|---|---|---|
| 0 | PARA | da | justificat, alineat pe primul rând |
| 1 | HEADING | da | centrat, font mai mare sau bold, spațiu deasupra, niciodată ultimul rând de pe pagină |
| 2 | VERSE | da | un vers: aliniat la stânga, nejustificat. Dacă e prea lat, se continuă pe rândul următor cu alineat agățat. |
| 3 | BREAK | **niciunul** (`text_len` = 0, fără intervale) | separator de strofă sau de scenă: un rând gol sau un ornament, dacă are indicatorul. Scos sus pe pagină. |
| 4 | QUOTE | da | citat bloc sau motto: retras pe ambele părți |

Un cititor care întâlnește un tip necunoscut cu text îl paginează ca PARA și sare un tip necunoscut fără text.

**Indicatori**

| Bit | Mască | Semnificație |
|---:|---:|---|
| 0–1 | `0x03` | alinierea: 0 = cea implicită a tipului, 1 = stânga, 2 = centru, 3 = dreapta |
| 2 | `0x04` | `NO_INDENT`: fără alineat pe primul rând (de exemplu primul paragraf după un titlu). La BREAK același bit înseamnă `ORNAMENT`: se desenează `* * *` în loc de un rând gol. |
| 3 | `0x08` | `CONT`: continuă înregistrarea precedentă. Convertorul a împărțit un paragraf prea lung, deci nu e nici alineat, nici spațiu între paragrafe; paginarea le tratează pe cele două ca un singur paragraf. |
| 4–5 | `0x30` | doar la HEADING: nivelul − 1, pentru nivelurile 1–4 |
| 6 | `0x40` | `PAGE`: înregistrarea începe o pagină nouă când setarea „capitolele pe pagină nouă” e pornită. Convertorul îl pune pe titlurile de capitol (CONV-7). Invalid pe BREAK și pe înregistrările CONT. O pagină care începe la o înregistrare `PAGE` nu depinde niciodată de paginile dinaintea ei, deci pagina înapoi trebuie paginată doar de la cea mai apropiată (secțiunea 5). |
| 7 | `0x80` | rezervat, trebuie să fie 0; validatorul îl respinge |

**Intervalele de stil** marchează intervale de octeți `[start, start + length)` din text, în octeții UTF-8 ai înregistrării. Intervalele sunt ordonate, nu se suprapun niciodată și nu despart niciodată o secvență UTF-8. Textul neacoperit de vreun interval e regular, iar un interval cu stilul 0 e invalid. Biții de stil: `0x01` bold, `0x02` italic. Ceilalți biți sunt rezervați și ignorați de cititoare.

**Regulile textului.** Ele fac paginarea pe dispozitiv simplă:
- Fără caractere de control C0 sau C1: fără tab-uri sau capete de rând.
- Un singur spațiu între cuvinte, niciunul la capete.
- Caractere speciale pe care le pune convertorul și pe care motorul de paginare trebuie să le respecte:
  - `U+00AD` cratima moale: un punct de rupere permis. Se desenează „-” doar dacă rândul se rupe acolo; altfel e invizibilă.
  - `U+00A0` spațiul neseparabil: un spațiu care nu trebuie rupt. Convertorul leagă o linie de dialog de cuvântul dinainte, ca niciun rând să nu înceapă cu „—”.
  - `U+200B` spațiul de lățime zero: un punct de rupere permis fără cratimă, de exemplu în URL-urile lungi.
  - `U+2028` separatorul de rând: o rupere forțată de rând în interiorul paragrafului (un `<br/>` din XHTML).

**Mărimea blocului.** `raw_len` are cel mult **32768** octeți. Asta e mărimea ferestrei DEFLATE, deci dispozitivul decodează un bloc întreg într-un singur buffer fix de 32 KB, iar bufferul servește și ca dicționar al decompresorului. În limita asta, convertorul umple blocurile greedy cu înregistrări întregi până la o mărime țintă: implicit 16384, schimbată cu `--block`. Alegerea valorii implicite face parte din experimentul E5. Un paragraf a cărui înregistrare ar depăși 32 KB e împărțit de convertor la un spațiu, iar părțile următoare sunt marcate `CONT`. Fiecare bloc se stochează brut (metoda 0) când DEFLATE nu l-ar micșora.

### 3.7 Pozițiile și `book_id`

O poziție de lectură e perechea **(indicele paragrafului, deplasamentul în octeți în textul acelui paragraf)**. Deplasamentul cade mereu la granița unui caracter UTF-8.

Pozițiile nu depind de font, de ecran sau de împărțirea în blocuri. Paginarea se schimbă cu setările, dar poziția rămâne aceeași, deci după o schimbare de setări cititorul repaginează în jurul aceleiași propoziții.

`book_id` face hash peste conținutul META și peste **înregistrările codificate, înainte de compresie**. Deci convertirea aceleiași cărți cu altă mărime de bloc sau altă compresie dă același `book_id`, iar pozițiile salvate rămân valide. Orice schimbare în text, inclusiv în despărțirea în silabe, dă un `book_id` nou. Firmware-ul știe atunci că deplasamentele vechi pot arăta în locul greșit. ID-ul are 64 de biți, deci o coliziune întâmplătoare între cărțile de pe un card nu e o problemă practică.

### 3.8 `COVR`: coperta (opțională)

```
u16 width, u16 height      cel mult 240 × 416 (ecranul în portret)
u8  method                 0 = stocat, 1 = DEFLATE
u8  reserved = 0
u32 raw_len                = ceil(width / 8) × height
bitmap (stocat sau DEFLATE)
```

Bitmapul e de 1 bpp, pe rânduri, cu rândurile completate la octeți întregi, MSB = pixelul cel mai din stânga, **bitul 1 = alb**. Asta e așezarea memoriei panoului UC8253 și a framebufferului `gfx_t` din firmware, deci o copertă pe tot ecranul se copiază direct pe panou. Convertorul scalează imaginea și aplică dithering (Floyd–Steinberg, CONV-6).

## 4. Exemplu lucrat

`spec/examples/example.ibv`, 240 de octeți, stocat fără compresie ca fiecare octet să se poată citi. Conține:

```
HEADING  "I"                          centrat, intrarea de cuprins "I"
PARA     "Stau câ­teo­da­tă."        NO_INDENT, trei cratime moi
PARA     "Ce mai «faci»?"             «faci» cu italic
BREAK                                 ornament (* * *)
VERSE    "A fost odată"
```

Afișarea hex, cu fiecare câmp comentat (deplasamente în hex):

```
0000  89 49 42 56 0d 0a 1a 0a      magic
0008  00 01                        versiunea 0.1
000a  2c 00                        header_size = 44
000c  00 00 00 00                  flags
0010  43 cd 94 74 31 9d 51 73      book_id
0018  05 00 00 00                  paragraph_count = 5
001c  35 00 00 00                  text_bytes = 53
0020  04 00                        section_count = 4
0022  00 00                        rezervat
0024  aa f1 19 ab                  directory_crc
0028  89 0c c5 4e                  header_crc (octeții 0x00–0x27)

002c  4d 45 54 41  6c 00 00 00  11 00 00 00  56 34 22 21    META  @108, 17 B
003c  54 4f 43 20  7d 00 00 00  0b 00 00 00  9f d4 ab 99    TOC   @125, 11 B
004c  42 4c 4b 53  88 00 00 00  1c 00 00 00  e0 39 43 e7    BLKS  @136, 28 B
005c  54 45 58 54  a4 00 00 00  4c 00 00 00  7c a2 11 ca    TEXT  @164, 76 B

006c  02 00                        META: 2 intrări
006e  01 07 00 45 78 65 6d 70 6c 75   cheia 1 title, 7 B "Exemplu"
0078  03 02 00 72 6f                  cheia 3 language, 2 B "ro"

007d  01 00                        TOC: 1 intrare
007f  00 00 00 00 01 00 01 00 49      para 0, nivelul 1, 1 B "I"

0088  01 00 00 00                  BLKS: 1 bloc
008c  a4 00 00 00                    offset 164
0090  4c 00 00 00                    stored_len 76
0094  4c 00 00                       raw_len 76, method 0 (stocat)
0097  00                             rezervat
0098  00 00 00 00                    first_para 0
009c  00 00 00 00                    text_start 0
00a0  7c a2 11 ca                    crc32 al celor 76 de octeți bruți

00a4  01 02 01 00 49               HEADING, flags 0x02 (centru, nivelul 1), 1 B, 0 intervale, "I"
00a9  00 04 17 00                  PARA, flags 0x04 (NO_INDENT), 23 B, 0 intervale
00ad  53 74 61 75 20 63 c3 a2 c2 ad 74 65 6f c2 ad 64 61 c2 ad 74 c4 83 2e
                                     "Stau câ" SHY "teo" SHY "da" SHY "tă."   (c2 ad = U+00AD)
00c4  00 00 10 01 07 08 02         PARA, flags 0, 16 B, 1 interval: start 7, lungime 8, stil 0x02 italic
00cb  43 65 20 6d 61 69 20 c2 ab 66 61 63 69 c2 bb 3f
                                     "Ce mai «faci»?"   (octeții 7–14 = «faci»)
00db  03 04 00 00                  BREAK, flags 0x04 (ORNAMENT), 0 B, 0 intervale
00df  02 00 0d 00                  VERSE, flags 0, 13 B, 0 intervale
00e3  41 20 66 6f 73 74 20 6f 64 61 74 c4 83     "A fost odată"
00f0                               sfârșitul fișierului (240 B)
```

Același fișier prin afișare:

```
$ python3 tools/converter/ibv.py dump spec/examples/example.ibv
version 0.1  book_id 43cd9474319d5173  paragraphs 5  text 53 B  file 240 B
...
      0 HEADING 00000010 runs=[] 'I'
      1 PARA    00000100 runs=[] 'Stau câ·teo·da·tă.'
      2 PARA    00000000 runs=[(7, 8, 2)] 'Ce mai «faci»?'
      3 BREAK   00000100 runs=[] ''
      4 VERSE   00000000 runs=[] 'A fost odată'
```

## 5. Cum citește dispozitivul o carte

**Biblioteca.** Pentru fiecare fișier `.ibv` de pe card:
1. Se citesc primii 44 de octeți și se verifică codul magic, versiunea și `header_crc`.
2. Se citește directorul și se verifică `directory_crc`.
3. Se citește META.

Coperta se citește doar pentru ecranul de somn.

**Deschiderea.**
1. Se citește BLKS (24 B pe bloc; *Moara cu noroc* are 17 blocuri la 16 KB, deci cam 400 B) și i se verifică CRC-ul.
2. Se încarcă poziția din fișierul însoțitor (secțiunea 6).
3. Se găsește blocul care conține paragraful *p* prin căutare binară după `first_para`.

**Citirea unui bloc.** Se citesc `stored_len` octeți și se decomprimă în bufferul de 32 KB (metoda 1) sau se citesc direct în el (metoda 0). Se verifică `crc32` față de octeții bruți. Apoi se parcurg înregistrările: lungimea fiecăreia e în antetul ei, deci parcurgerea nu are nevoie de index.

**Bugetul de RAM al cititorului de cărți:**

| | |
|---|---|
| bufferul blocului | 32 KB |
| starea decompresorului `tinfl` | ~11 KB |
| indexul blocurilor | 24 B × blocuri |
| bufferul de citire pentru blocul stocat | ≤ 32 KB, sau citit în bucăți de 4 KB |

În total sub 80 KB din cei 512 KB de SRAM ai S3 și **independent de lungimea cărții**. Nu e nevoie de PSRAM. Un al doilea buffer de bloc e opțional. Scutește o pagină care trece peste granița unui bloc, sau o „pagină anterioară” de la începutul unui bloc, de o a doua decodare. Firmware-ul hotărăște asta după E2.

**Progresul.** Până se termină paginarea:

> % = (`text_start` al blocului + octeții dinaintea poziției în bloc) / `text_bytes`

După paginare e „pagina x din y”.

**Pagina înapoi.** Unde începe o pagină depinde de paginile dinaintea ei, deci paginarea nu poate rula înapoi. Ea paginează înainte de la un *punct de sincronizare*, un loc care începe mereu o pagină: începutul cărții sau o înregistrare `PAGE` când capitolele încep pagini noi. Cu indexul paginilor (fișierul însoțitor `.pg`) e în schimb o căutare binară. Măsurat pe corpus: în medie 13–56 de pagini de paginat pentru o carte cu capitole, aproape toată cartea pentru una fără, care atunci așteaptă indexul.

**Coruperea.** Un CRC greșit la antet, director sau secțiune înseamnă că dispozitivul arată „fișier deteriorat” în bibliotecă și nu deschide cartea. Un CRC de bloc greșit înseamnă că arată o pagină de eroare în locul textului acelui bloc și îl lasă pe cititor să treacă peste el. Un singur sector stricat n-ar trebui să facă toată cartea de necitit.

## 6. Fișierele însoțitoare

Dispozitivul scrie starea fiecărei cărți lângă cărți, dar niciodată în fișierul cărții. Cartea rămâne exact cum a scris-o convertorul, iar validatorul trece și după luni de lectură.

Fișierele însoțitoare stau în `/.ibv/` pe card și sunt numite după `book_id` în hex, nu după numele fișierului cărții. Redenumirea sau mutarea unei cărți pe PC îi păstrează progresul, iar dosarul cărților rămâne curat când răsfoiești cardul. *(Asta schimbă planul, care avea `<book>.pos` lângă fiecare carte.)* Numele lungi de fișier din FATFS trebuie activate; ESP-IDF le suportă.

Fiecare fișier însoțitor începe cu un cod magic de 4 octeți și cu `book_id` de 8 octeți și se termină cu un CRC32 al tuturor octeților dinainte. Dacă CRC-ul sau `book_id` nu se potrivesc, dispozitivul ignoră fișierul: mai bine niciun progres decât o poziție greșită.

**Scrierile nu lasă niciodată un fișier greșit:** dispozitivul scrie `<nume>.tmp`, șterge fișierul vechi și îl redenumește pe cel nou în locul lui. (FATFS nu poate redenumi peste un fișier existent, deci sunt doi pași.) O cădere de curent în timpul scrierii pierde cel mult acel fișier însoțitor, nu-l corupe niciodată: un `.tmp` e ignorat, iar un `.pg` lipsă se reface.

| Fișier | Structură | Scris când |
|---|---|---|
| `<id>.pos` | `"IPOS"`, book_id, u32 para, u32 byte, crc32 → 24 B | la somn, la închiderea cărții și la fiecare N pagini întoarse |
| `<id>.bm` | `"IBMK"`, book_id, u16 count, count × {u32 para, u32 byte}, crc32 | la adăugarea sau ștergerea unui semn de carte |
| `<id>-<layout>.pg` | `"IPGC"`, book_id, u32 layout_hash, u32 page_count, u8 complete, 3 B rezervați, page_count × {u32 para, u32 byte}, crc32 | de paginatorul de fundal, pe măsură ce avansează |

`layout_hash` acoperă tot ce schimbă unde se rup paginile:
- fonturile (fiecare stil de text și fontul titlurilor) și mărimile lor
- zona de text (ecranul minus marginile)
- alineatul, spațierea rândurilor și a paragrafelor
- justificarea, despărțirea în silabe, capitolele pe pagină nouă, controlul rândurilor văduve și orfane, ruperea greedy sau prin încadrare totală
- numărul de versiune al motorului de paginare (`LAYOUT_VERSION`)

Orice schimbare de setare înseamnă alt fișier `.pg`. Dispozitivul le păstrează pe ultimele câteva și le șterge pe celelalte. `complete = 0` înseamnă că paginarea încă rulează: subsolul arată un procent până se termină. Un index incomplet se reia prin paginarea din nou a ultimei lui pagini. Încărcătorul respinge începuturile de pagină care nu cresc strict sau nu pornesc de la {0, 0}.

## 7. Regulile validatorului

`ibv.py validate` verifică următoarele și iese cu un cod diferit de zero la orice eșec. CONV-10 îl rulează pe tot corpusul.

1. Codul magic, versiunea majoră, `header_size`, `header_crc`, `directory_crc`.
2. Fără etichete duplicate. Fiecare secțiune e în interiorul fișierului. Fiecare CRC de secțiune se potrivește, inclusiv TEXT. META, BLKS și TEXT există.
3. META are un titlu și o limbă și nicio cheie de două ori.
4. Fiecare invariant BLKS din 3.5. Fiecare bloc se decodează în exact `raw_len` octeți, CRC-ul lui se potrivește, iar înregistrările lui se termină exact la sfârșitul blocului.
5. Fiecare înregistrare:
   - doar biți de indicator cunoscuți
   - BREAK nu are text
   - `PAGE` nu e pe BREAK sau CONT
   - celelalte tipuri au text nevid
   - UTF-8 valid, normalizat NFC
   - fără caractere de control
   - fără spații la început, la sfârșit sau dublate
   - intervalele ordonate, fără suprapuneri, în limite, la granițe de caracter, stilul ≠ 0
6. `paragraph_count`, `text_bytes` și `book_id` se potrivesc cu conținutul.
7. Intrările cuprinsului sunt în ordine strict crescătoare și în limite, cu nivelurile 1–4.
8. Coperta are cel mult 240 × 416.

Testele (`test_ibv.py`) inversează pe rând fiecare bit al unui fișier exemplu și verifică că validatorul îl prinde pe fiecare. Prima rulare a testului a găsit că o etichetă de director stricată trecea neobservată, pentru că secțiunile necunoscute se sar prin proiectare. De aceea există `directory_crc`.

## 8. Versionarea

- Versiunea **majoră**: un cititor refuză orice versiune majoră pe care n-o cunoaște.
- Versiunea **minoră**: doar adăugiri compatibile, ca secțiuni, chei META, tipuri sau biți de stil noi. Cititoarele le ignoră deja după regulile de mai sus.
- În **0.x** se poate schimba orice. Formatul se îngheață ca **1.0** odată ce convertorul, validatorul, simulatorul și firmware-ul îl implementează toate (SPEC-4). După aceea, orice schimbare crește o versiune.

## 9. Alegeri de proiectare și alternativele respinse

| Alegerea | Alternativa | De ce |
|---|---|---|
| Înregistrări rearanjabile | Pagini pre-paginate | Motorul de paginare e obligatoriu, iar mărimea fontului trebuie să se poată schimba. Un rezultat pre-paginat ar lega fiecare fișier de un singur font și o singură mărime. |
| Despărțirea în silabe în convertor (cratime moi) | Modele de despărțire pe dispozitiv | Modelele Liang pentru RO + RU + EN sunt mari, iar PC-ul are timp nelimitat. Dispozitivul trebuie doar să rupă rândurile. Costul: cratimele moi fac textul brut **cu 18,5 % mai mare** la *Moara cu noroc* (câte 2 octeți). |
| Blocuri de ≤ 32 KB, comprimate independent | Un flux DEFLATE pe capitol, cum spunea întâi planul general | Un capitol nu are limită de mărime: o carte TXT e un singur capitol, iar cel mai lung capitol din *Moara cu noroc* are 37 KB, deja peste fereastra DEFLATE de 32 KB. Saltul într-un flux DEFLATE înseamnă decodare de la începutul lui. Blocurile fac constante și memoria folosită, și timpul de salt. **Asta înlocuiește „compresia pe capitol” din planul general.** |
| DEFLATE | LZ4, heatshrink, fără compresie | Decodorul e deja în ROM-ul ESP32-S3, deci costă zero flash, iar DEFLATE comprimă textul mai bine decât LZ4. E5 măsoară timpul de decodare pe dispozitiv. |
| Varinturi în înregistrări | u16/u32 fixe | Majoritatea lungimilor sunt sub 128, deci 1 octet în loc de 4. Câmpurile antetului rămân de lățime fixă ca să fie ușor de citit și de sărit. |
| Director de secțiuni cu etichete | Structură fixă | Cititoarele sar secțiunile necunoscute, deci firmware-ul v0.x merge mai departe cu fișiere mai noi: aceeași idee ca la chunk-urile PNG și tabelele TrueType. |
| Poziția = (paragraf, octet) | Numărul paginii sau un deplasament global în octeți | Supraviețuiește schimbărilor de font și de setări. Deplasamentul global ar supraviețui și el, dar perechea duce direct la căutarea blocului. |
| `book_id` peste conținutul necomprimat | Hash al fișierului sau numele fișierului | Supraviețuiește reîmpărțirii în blocuri, recompresiei și redenumirii și se schimbă exact când se schimbă textul. |

## 10. Cifre preliminare

Vin din cele patru cărți ale machetei (`docs/mockup/texts/*.hy.txt`), trecute prin `ibv.py` pe PC pe 2026-10-26. Nu sunt încă E5: E5 folosește corpusul de 20 de cărți și adaugă timpi pe dispozitiv. Sunt aici ca să arate că proiectarea e într-un domeniu rezonabil.

| Carte | Text simplu | gzip -9 (tot fișierul) | IBV stocat | IBV, blocuri de 4 KB | 8 KB | **16 KB** | 32 KB | Blocuri la 16 KB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Amintiri din copilărie (RO) | 153.945 | 59.734 | 185.619 | 89.520 | 82.289 | **76.816** | 72.298 | 12 |
| Moara cu noroc (RO) | 227.663 | 77.922 | 272.075 | 125.290 | 114.245 | **105.269** | 97.948 | 17 |
| Luceafărul (RO, versuri) | 11.619 | 4.920 | 12.398 | 6.270 | 5.943 | **5.549** | 5.549 | 1 |
| Белые ночи (RU) | 183.416 | 48.628 | 211.813 | 77.201 | 70.962 | **64.862** | 60.175 | 14 |

Mărimile sunt în octeți. Textul simplu înseamnă sursa fără cratimele moi.

- La 16 KB, o carte are **35–50 % din textul simplu** și cam 1,3× cât obține gzip comprimând tot fișierul ca un singur flux. Acel 1,3× e prețul accesului aleatoriu plus al cratimelor moi.
- Blocurile de 32 KB mai economisesc 6–8 %, dar dublează decodarea în cazul cel mai rău la fiecare salt. Dacă merită depinde de timpul de decomprimare a unui bloc pe dispozitiv. Asta e întrebarea lui E5, împreună cu E2.
- Întrebări pentru E5: surplusul cratimelor moi s-ar putea elimina păstrând punctele de rupere ca o listă de biți pe înregistrare. E mai mic, dar complică parcurgerea textului în motorul de paginare. De măsurat înainte de a decide.

## 11. Nu sunt în v0.1 (candidați pentru mai târziu)

- Notele de subsol și de final. EPUB le are; le trebuie o țintă de legătură plus o vedere „note”.
- Imaginile din cărți. Nu sunt un obiectiv pentru v1; e suportată doar coperta.
- Majusculele mici, exponentul, indicele: biții de stil sunt liberi.
- Indicații de font pe carte, de exemplu „cartea asta are nevoie de greacă”.
- Legăturile interne, dincolo de ce dă deja cuprinsul.

## Jurnalul de schimbări

- **0.1** (2026-10-26) Prima ciornă, împreună cu codecul de referință și 15 teste. Schimbări față de planul general: blocuri în loc de compresie pe capitol și fișiere însoțitoare identificate după `book_id` în `/.ibv/`. `directory_crc` adăugat după ce testul cu inversarea biților a găsit o lacună.
- **0.1** (2026-10-26, aceeași zi) Numit IBV (Q5): codul magic `\x89IBV`, extensia `.ibv`, fișiere însoțitoare în `/.ibv/` cu codurile magice `IPOS`, `IBMK`, `IPGC`. `book_id` nu s-a schimbat, pentru că nu acoperă antetul.
- **0.1** (2026-10-27) Bitul de indicator 6 e acum `PAGE` (capitolul începe o pagină), pus de convertor după o euristică pe nivelul capitolelor; face ieftină pagina înapoi. Secțiunea 6: scrierile fișierelor însoțitoare sunt ștergere + redenumire pe FATFS. Lista `layout_hash` adusă la zi cu codul.
- **0.1** (2026-10-27) `layout_hash` include și modul de rupere în rânduri (greedy sau încadrare totală, LAY-5).
