# Formatul de font IBF, v0.1

Stare: **ciornă pentru revizuire.** Scrisă pe 2026-10-26.
Implementarea de referință: `tools/fonttool/ibf.py` (scriitor, cititor, validator). Constructorul de fonturi: `tools/fonttool/fonttool.py`. Teste: `tools/fonttool/test_fonttool.py`. Cititorul de pe dispozitiv: `firmware/components/core/font.c` (`font_open`).

**Numele.** IBF, *Ink Bitmap Font*, perechea lui IBV. Extensia `.ibf`.

Când documentul ăsta și `ibf.py` nu sunt de acord, unul dintre ele are o eroare. Se repară, apoi se adaugă un rând în jurnalul de schimbări de la final.

---

## 1. La ce folosește formatul

Dispozitivul desenează textul din **bitmapuri desenate dinainte**. Nu are rasterizator TrueType: FreeType ar costa cam 300 KB de flash și câteva milisecunde pe glifă, iar pe un ecran de 1 bit rezultatul tot trebuie trecut printr-un prag. PC-ul face desenarea o singură dată, cu hinting complet sau cu un prag ales atent, și scrie câte un fișier pentru fiecare față (regular, bold, italic, bold italic) și mărime în pixeli.

Ce îi trebuie dispozitivului de la un font:

1. **Să găsească repede o glifă.** Caută fiecare caracter de pe pagină, deci căutarea trebuie să fie câteva comparații.
2. **Să măsoare exact.** Ruperea în rânduri și justificarea adună lățimi de avans. Avansurile întregi în pixeli împrăștie erori de rotunjire de-a lungul rândului, deci avansurile și kerningul se păstrează în **1/64 px**, același virgulă fixă 26.6 pe care îl folosește FreeType.
3. **Să folosească fișierul pe loc.** Tabelele sunt așezate astfel încât un pointer la o structură C să poată arăta direct în fișierul încărcat (sau într-o partiție de flash mapată în memorie). Deschiderea unui font e o verificare a antetului, niciodată o parsare.
4. **Să rămână mic.** Setul implicit de caractere (cam 435 de glife) la 18 px ocupă 20–38 KB pe față, deci toate patru fețele fontului de text încap împreună în RAM.

## 2. Convenții

- Little-endian, ca IBV. `u8/u16/u32` fără semn, `i8/i16` în complement față de doi.
- Toate deplasamentele sunt absolute, de la începutul fișierului.
- Tabelele încep la deplasamente multiple de 4. Fiecare înregistrare are alinierea naturală (un `u32` la un multiplu de 4, un `u16` la un multiplu de 2), deci pe ESP32 și pe PC structurile C din `font.h` au exact mărimile și așezarea de aici, fără `packed`. `font.h` verifică mărimile cu `_Static_assert`.
- `CRC32` e CRC-ul zlib/PNG, ca în IBV.
- Unități: **px** sunt pixeli de ecran. **u** e 1/64 px (deci 64 u = 1 px).
- y crește în jos, ca pe ecran. Linia de bază e y = 0. Un y negativ e deasupra liniei de bază.

## 3. Structura fișierului

```
offset 0   ┌────────────────────────────────┐
           │ Antet                   96 B   │  cod magic, versiune, metrici, deplasamentele tabelelor, CRC-uri
       96  ├────────────────────────────────┤
           │ Intervale     8 B × intervale  │  intervale de puncte de cod → indici de glifă
           │ Glife            12 B × glife  │  deplasamentul bitmapului, caseta, avansul
           │ Perechi de kerning 6 B × per.  │  opțional (numărul poate fi 0)
           │ Bitmapuri                      │  1 bpp, rânduri completate la octet
           └────────────────────────────────┘
```

Scriitorul folosește ordinea asta. Cititorii găsesc tabelele doar prin deplasamentele din antet.

### 3.1 Antetul (96 de octeți)

| Deplasament | Mărime | Câmp | Valoare / semnificație |
|---:|---:|---|---|
| 0 | 8 | `magic` | `89 49 42 46 0D 0A 1A 0A` = `\x89IBF\r\n\x1a\n` (aceeași idee ca la IBV) |
| 8 | 1 | `version_major` | 0 |
| 9 | 1 | `version_minor` | 1 |
| 10 | 2 | `header_size` | 96 |
| 12 | 1 | `bpp` | 1. (2 e rezervat pentru 4 niveluri de gri; cititoarele v0.1 îl refuză.) |
| 13 | 1 | `style` | bitul 0 bold, bitul 1 italic: aceiași biți ca la intervalele de stil IBV |
| 14 | 1 | `render` | cum a făcut PC-ul bitmapurile, informativ: 0 = monocrom cu hinting, 1 = nuanțe de gri + prag. Cititoarele îl ignoră. |
| 15 | 1 | `threshold` | pentru `render` = 1: cerneală unde acoperirea ≥ threshold/255. Altfel 0. |
| 16 | 2 | `pixel_size` | mărimea em în px |
| 18 | 2 | `line_height` | distanța recomandată între liniile de bază, px (ascent + descent + spațiul dintre rânduri al fontului sursă) |
| 20 | 2 | `ascent` | px deasupra liniei de bază |
| 22 | 2 | `descent` | px sub linia de bază |
| 24 | 2 | `x_height` | px; folosită la centrarea ornamentelor și liniilor pe text |
| 26 | 2 | `range_count` | |
| 28 | 4 | `glyph_count` | ≤ 65535 |
| 32 | 4 | `kern_count` | poate fi 0 |
| 36 | 4 | `ranges_offset` | multiplu de 4 |
| 40 | 4 | `glyphs_offset` | multiplu de 4 |
| 44 | 4 | `kerns_offset` | multiplu de 4 (și când `kern_count` = 0) |
| 48 | 4 | `bitmaps_offset` | |
| 52 | 4 | `bitmaps_length` | octeți; bitmapurile încheie fișierul |
| 56 | 32 | `name` | numele familiei în UTF-8, completat cu NUL, de exemplu `PT Serif` |
| 88 | 4 | `data_crc` | CRC32 al tuturor octeților de la `header_size` până la sfârșitul fișierului |
| 92 | 4 | `header_crc` | CRC32 al octeților 0–91 |

### 3.2 Intervalele (câte 8 octeți)

| Deplasament | Mărime | Câmp |
|---:|---:|---|
| 0 | 4 | `first_cp`: primul punct de cod din interval |
| 4 | 2 | `count`: punctele de cod din interval, ≥ 1 |
| 6 | 2 | `first_glyph`: indicele glifei lui `first_cp` |

Intervalele sunt ordonate după `first_cp` și nu se suprapun. Punctul de cod *c* din intervalul *r* are indicele de glifă `first_glyph + (c − first_cp)`. Glifele sunt numerotate în ordinea punctelor de cod, deci `first_glyph` al fiecărui interval e suma numerelor de dinaintea lui.

Căutarea e o căutare binară în intervale, apoi o adunare. Setul implicit de caractere (secțiunea 5) înseamnă 12 intervale când fontul are toate caracterele (PT Serif, căruia îi lipsesc câteva litere rare, are 17), deci cel mult 4–5 comparații, față de 9 la o căutare binară în 435 de glife, iar tabelul nu are nevoie de câte un punct de cod pe glifă.

### 3.3 Glifele (câte 12 octeți)

| Deplasament | Mărime | Câmp | Semnificație |
|---:|---:|---|---|
| 0 | 4 | `bitmap` | deplasamentul bitmapului față de `bitmaps_offset` |
| 4 | 2 | `advance` | avansul creionului, **u** (1/64 px) |
| 6 | 1 | `width` | lățimea casetei bitmapului, px (0 pentru glifele goale, ca spațiul) |
| 7 | 1 | `height` | înălțimea casetei bitmapului, px |
| 8 | 1 | `x_off` (i8) | marginea stângă a casetei față de creion, px |
| 9 | 1 | `y_off` (i8) | marginea de sus a casetei față de linia de bază, px (negativ = deasupra) |
| 10 | 2 | `kern_as` | indicele glifei ale cărei perechi de kerning le folosește glifa; propriul indice dacă are perechile ei (secțiunea 3.4) |

Bitmapul unei glife are `height` rânduri a câte `ceil(width / 8)` octeți, MSB = pixelul cel mai din stânga, **bitul 1 = cerneală**. E invers față de framebuffer (bitul 1 = alb): un bit pus înseamnă „șterge bitul ăsta din framebuffer”, deci desenarea e un AND-NOT pe octet.

Un punct de cod din interiorul unui interval pe care fontul sursă nu-l are nu se păstrează ca glifă. Scriitorul împarte intervalul în jurul lui.

### 3.4 Perechile de kerning (câte 6 octeți)

| Deplasament | Mărime | Câmp |
|---:|---:|---|
| 0 | 2 | `left`: indicele glifei |
| 2 | 2 | `right`: indicele glifei |
| 4 | 2 | `value` (i16): se adaugă la avansul glifei din stânga când îi urmează `right`, **u**. Poate fi 0. |

Ordonate după (`left`, `right`), fără duplicate. Kerningul se aplică doar între glife vecine din **aceeași față**. O schimbare de stil în mijlocul unui cuvânt nu primește kerning.

**Kerningul împrumutat.** O literă cu diacritic face de obicei kerning ca litera ei de bază: *Àv* ca *Av*, *Ță* ca *Ta*. Păstrarea din nou a acestor perechi pentru fiecare literă cu diacritic triplă tabelul (PT Serif la 18 px: 7.596 de perechi în loc de 2.344). Așa că `kern_as` al unei glife numește glifa ale cărei perechi le folosește (À → A, ă → a, Й → И). Fiecare `kern_as` arată spre o glifă care face kerning ca ea însăși, deci nu e niciodată mai mult de un pas.

Împrumutul nu e mereu corect: fonturile fac mai puțin kerning la *Vă* decât la *Va*, ca semnul de deasupra lui ă să nu atingă V-ul. Unde valoarea reală diferă de cea împrumutată cu ¼ px sau mai mult, constructorul păstrează perechea cu indicii proprii ai glifelor (o **excepție**, valoarea 0 e permisă). Căutarea pentru glifele *a*, *b* e:

1. perechea (*a*, *b*), dacă există;
2. altfel, dacă `kern_as[a]` ≠ *a* sau `kern_as[b]` ≠ *b*, perechea (`kern_as[a]`, `kern_as[b]`), dacă există;
3. altfel 0.

Fiecare pas e o căutare binară, cam 12 comparații pentru 3.000 de perechi. Constructorul păstrează o pereche doar dacă |valoarea| ≥ 16 u (¼ px) la mărimea aceea; valorile mai mici nu mută o glifă de 1 bit destul de des ca să conteze. Cu excepțiile, fiecare pereche măsurată de constructor iese la cel mult ¼ px de kerningul fontului. PT Serif 18 px: 2.344 de perechi + 604 excepții.

Perechile se măsoară în cadrul unei singure scrieri plus punctuație: latin cu latin, chirilic cu chirilic. Perechile din scrieri amestecate nu apar în interiorul cuvintelor.

### 3.5 Bitmapurile

Bitmapurile glifelor puse unul după altul. Glifele cu bitmapuri identice (de exemplu U+00A0 și spațiul, sau A latin și А chirilic) pot împărți unul.

### 3.6 Înlocuitorii

Când fontului sursă îi lipsește un caracter din setul implicit, constructorul păstrează un **înlocuitor**: o copie a metricilor unei glife asemănătoare, care îi împarte bitmapul. U+2010 HYPHEN devine `-`, U+2012 FIGURE DASH devine `–`, U+201B devine `’`, iar o literă cu diacritic devine litera de bază (PT Serif nu are *Ċ*, deci primește *C*). Constructorul afișează înlocuitorii. Un caracter fără înlocuitor e omis, iar dispozitivul desenează U+FFFD în locul lui.

## 4. Cum folosește dispozitivul un font

**Deschiderea.** Se citește (sau se mapează) fișierul și se verifică codul magic, versiunea, `bpp` = 1 și ambele CRC-uri. Se verifică dacă fiecare tabel e în interiorul fișierului și fiecare deplasament e aliniat. Apoi se pun pointerii în fișier. Asta e tot `font_open()`. Nu se copiază nimic.

**Desenarea unei glife.** Se găsește (secțiunea 3.2). Dacă lipsește, se folosește U+FFFD, pe care trebuie să-l aibă orice font. Bitmapul se copiază la (`round(pen) + x_off`, `baseline + y_off`). Apoi pen += `advance` + kerningul cu glifa următoare.

**Aritmetica creionului.** Creionul se ține în u. Fiecare glifă se desenează la creionul rotunjit la cel mai apropiat pixel, deci erorile de rotunjire nu se adună de-a lungul rândului. Rândurile justificate își adaugă și ele spațiul suplimentar în u.

**Fețele.** Pe card, fonturile stau în `/fonts/` ca `<familie>-<mărime>-<stil>.ibf`. Stilul e `r`, `b`, `i` sau `bi`, de exemplu `ptserif-18-bi.ibf`. Dispozitivul alege familia după limba cărții și o mărime după o singură setare a cititorului (mai jos). O față bold sau italic lipsă revine la fața regular (bold se desenează atunci de două ori, la 1 px distanță).

**Ce font primește o carte** (`fontsel.c`). O carte al cărei `language` din META se scrie în chirilică (`ru`, `uk`, `be`, `bg`, `sr`, `mk`, `kk` și încă câteva, sau orice etichetă cu subeticheta de scriere `Cyrl`) se culege cu PT Serif; orice altă carte cu Libron, care nu are chirilice. O subetichetă `Latn` o face Libron (`sr-Latn`). Cititorul nu alege niciodată o familie. Setarea de mărime are cinci trepte, iar fiecare treaptă numește câte o mărime pentru fiecare familie. Mărimile sunt potrivite după înălțimea x și după numărul de pagini pe corpus, deci trecerea la o carte în cealaltă scriere nu schimbă cât de mare pare textul:

| Treapta | 1 | 2 | 3 (implicit) | 4 | 5 |
|---|---:|---:|---:|---:|---:|
| Libron | 15 | 16 | 17 | 18 | 20 |
| PT Serif | 16 | 17 | 18 | 19 | 21 |
| înălțimea x, px | 8 | 9 | 9 | 10 | 11 |

**RAM.** O față la 18 px cu setul implicit de caractere are 20–38 KB cu tot cu kerningul (secțiunea 7). Cele patru fețe de text plus o față pentru titluri ajung la cam 150–190 KB din cei 512 KB de SRAM ai S3. Dacă se dovedește prea mult lângă bufferele cărții, fișierele pot sta într-o partiție de flash mapată în memorie, fără schimbarea formatului.

## 5. Setul de caractere

`tools/fonttool/fonttool.py` `DEFAULT_RANGES`. Convertorul (`glyphs.py`) citește aceeași listă, mai puțin ce îi lipsește fontului cărții: pentru o carte în alfabet latin, asta înseamnă chirilicele și Ĳ ĳ ŉ ſ, pe care Libron nu le are (ſ devine s, celelalte formele lor din două litere). Așa o carte nu are niciodată nevoie de un caracter care lipsește din fontul ei, iar `sim/test/run.sh` verifică asta pe corpus.

- Basic Latin, Latin-1, Latin Extended-A
- ș ț românești cu virgulă dedesubt (U+0218–U+021B)
- Chirilicele U+0400–U+045F (rusă, ucraineană, belarusă, bulgară, sârbă)
- punctuația tipografică: linii, ghilimele, buline, puncte de suspensie, ghilimele unghiulare, semnul euro
- U+FFFD, caracterul de înlocuire: **obligatoriu**. Spațiul (U+0020) și cratima (U+002D) sunt și ele obligatorii.

Dispozitivul desenează U+00A0 (spațiul neseparabil) cu lățimea spațiului și desenează U+002D la o rupere pe cratimă moale. Nu caută niciodată U+00AD, U+200B sau U+2028, care sunt caractere de control al paginării.

## 6. Regulile validatorului

`fonttool.py validate` (în `ibf.py`) verifică:

1. Codul magic, versiunea majoră, `header_size`, `bpp` = 1, `header_crc`, `data_crc`.
2. Fiecare tabel e în interiorul fișierului, la un deplasament aliniat, iar tabelele nu se suprapun. Bitmapurile se termină exact la sfârșitul fișierului.
3. Intervalele sunt ordonate și nu se suprapun, cu numărul ≥ 1. Valorile `first_glyph` sunt consecutive și se adună la `glyph_count`.
4. Bitmapul fiecărei glife e în interiorul zonei de bitmapuri. Fiecare `kern_as` e < `glyph_count` și arată spre o glifă al cărei `kern_as` e ea însăși.
5. Perechile de kerning sunt ordonate, fără duplicate, și ambii indici sunt < `glyph_count`.
6. U+0020, U+002D și U+FFFD există.

## 7. Alegeri de proiectare și alternativele respinse

| Alegerea | Alternativa | De ce |
|---|---|---|
| Bitmapuri desenate dinainte | TrueType + FreeType pe dispozitiv | Fără rasterizator în flash, timp de desenare constant, iar desenarea pe 1 bit se reglează o singură dată pe PC (mono cu hinting sau prag, ales pe font). Costul: un fișier pe mărime, deci mărimile sunt fixe (cerința e 3+). |
| Un fișier pe față și mărime | Un fișier cu toate mărimile și stilurile | Dispozitivul încarcă doar ce cere setarea cititorului. Fișierele rămân destul de mici ca să se încarce întregi. |
| Avansuri și kerning în 1/64 px | Pixeli întregi | Un rând justificat de 30 de caractere cu avansuri în pixeli întregi greșește în total cu până la ±15 px, ceea ce se vede ca spațiere neuniformă între cuvinte. În virgulă fixă 26.6 eroarea rămâne sub 1 px pe glifă și nu se acumulează. |
| Tabel de intervale | Un punct de cod în fiecare înregistrare de glifă (cum avea `font_ui16` în v0) | Cu 4 octeți mai puțin pe glifă și mai puține comparații pe căutare. |
| Tabel de perechi de kerning cu `kern_as` | Fără kerning, un tabel complet de perechi sau kerning pe clase (ca în OpenType) | Perechile se caută cel mai simplu. Un tabel complet repetă perechile fiecărei litere de bază pentru formele ei cu diacritice (45 KB pentru PT Serif la 18 px). Clasele OpenType sunt mai mici, dar cer un tabel de clase pe fiecare parte. `kern_as` e o clasă pe o singură parte care nu costă octeți în plus (folosește câmpul liber al înregistrării de glifă), cu perechi de excepție unde e greșit. Cu pragul de ¼ px o față are 1.000–4.000 de perechi (6–23 KB). |
| Rânduri completate la octeți întregi | Un singur flux continuu de biți pe glifă | Desenarea lucrează pe octeți întregi. Completarea costă cam 28 % din octeții bitmapurilor (cam 2,5 KB pe față la 18 px), puțin față de tabelul de kerning. |
| Structuri pe loc, aliniate la 4 octeți | O codificare compactă parsată la încărcare | Deschiderea unui font nu costă nimic, iar același fișier merge din RAM, de pe SD sau dintr-o partiție de flash mapată în memorie. |

## 8. Versionarea

La fel ca la IBV: un cititor refuză o versiune majoră pe care n-o cunoaște. În 0.x se poate schimba orice. Formatul se îngheață ca 1.0 împreună cu IBV (SPEC-4).

## Jurnalul de schimbări

- **0.1** (2026-10-26) Prima ciornă, cu `ibf.py`, `fonttool.py build` și cititorul de pe dispozitiv. Kerningul împrumutat (`kern_as` în câmpul liber al înregistrării de glifă, plus perechi de excepție) și înlocuitorii adăugați în aceeași zi, după ce prima compilare PT Serif avea 7.596 de perechi.
- **0.1** (2026-10-27) Alegerea fontului după limba cărții (LAY-7): Libron pentru alfabetul latin, PT Serif pentru chirilică, cinci trepte de mărime potrivite. Setul de caractere al convertorului urmează fontul cărții.
