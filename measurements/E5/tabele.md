# E5: tabele (generate de analiza.py)

Nu se editează de mână. KB = 1024 B. * = în afara prozei (versuri, culegerea de probleme). Textul simplu = octeții UTF-8 ai paragrafelor, fără cratimele moi.

## 1. Mărimile, varianta implicită (DEFLATE, blocuri de 16 KB, despărțire, copertă)

| carte | sursa | text simplu | TEXT brut | TEXT comprimat | comprimat / brut | comprimat / simplu | copertă | fișierul | deschiderea (B) | conversia (s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| libru-dubrovsky | 135 | 242 | 288 | 88,7 | 31 % | 37 % | 0 | 89,8 | 1178 | 0,47 |
| pg11 | 185 | 146 | 160 | 64,4 | 40 % | 44 % | 8,4 | 74 | 1156 | 0,23 |
| pg1342 | 24282 | 705 | 797 | 330 | 41 % | 47 % | 10,4 | 344 | 3926 | 1,09 |
| pg2701 | 795 | 1198 | 1324 | 583 | 44 % | 49 % | 8,9 | 600 | 7903 | 1,67 |
| pg62916 | 415 | 290 | 361 | 132 | 36 % | 45 % | 10 | 143 | 1034 | 0,66 |
| pg64597 | 176 | 205 | 260 | 106 | 41 % | 52 % | 5,4 | 113 | 864 | 0,43 |
| pg65565 | 369 | 293 | 379 | 149 | 39 % | 51 % | 8,6 | 158 | 1082 | 0,52 |
| pg84 | 430 | 410 | 461 | 193 | 42 % | 47 % | 7,9 | 202 | 1581 | 0,6 |
| pg1322 * | 840 | 677 | 735 | 341 | 46 % | 50 % | 7,6 | 361 | 12400 | 0,78 |
| pg14741 * | 138 | 186 | 204 | 75,9 | 37 % | 41 % | 3,9 | 82,2 | 2536 | 0,24 |
| pg16527-8 * | 167 | 240 | 280 | 76,7 | 27 % | 32 % | 0 | 77,3 | 675 | 0,51 |
| pg35323 * | 468 | 873 | 1018 | 445 | 44 % | 51 % | 3,2 | 458 | 9210 | 1,2 |

## 2. Ce costă fiecare alegere: mărimea fișierului față de varianta implicită

| carte | stocat (fără compresie) | fără copertă | fără despărțire | cratimele moi, din fișier |
|---|---:|---:|---:|---:|
| libru-dubrovsky | +222,1 % | +0 % | −8,5 % | 8,5 % |
| pg11 | +129,4 % | −11,4 % | −3,4 % | 3,4 % |
| pg1342 | +135,6 % | −3 % | −6 % | 6 % |
| pg2701 | +123,6 % | −1,5 % | −5,3 % | 5,3 % |
| pg62916 | +160,7 % | −7 % | −7,8 % | 7,8 % |
| pg64597 | +136,1 % | −4,8 % | −8,7 % | 8,7 % |
| pg65565 | +146 % | −5,4 % | −10,2 % | 10,2 % |
| pg84 | +132,9 % | −3,9 % | −6 % | 6 % |
| pg1322 * | +109,1 % | −2,1 % | −0,1 % | 0,1 % |
| pg14741 * | +156,2 % | −4,8 % | −0,6 % | 0,6 % |
| pg16527-8 * | +263,4 % | +0 % | −7 % | 7 % |
| pg35323 * | +125,2 % | −0,7 % | −2,3 % | 2,3 % |

## 3. Mărimea blocului

Proza, totalul celor 8 cărți. „Citit la un salt” = octeții stocați ai unui bloc (cât se citește de pe card ca să ajungi la o poziție); „RAM” = blocul decomprimat, cel mai mare din toate cărțile.

| bloc țintă | fișierele (KB) | față de 16 KB | blocuri | citit la un salt, mediana (B) | citit, maximul (B) | RAM, maximul (B) |
|---:|---:|---:|---:|---:|---:|---:|
| 4 KB | 1991 | +15,5 % | 1125 | 1799 | 3536 | 8246 |
| 8 KB | 1841 | +6,8 % | 535 | 3425 | 4186 | 8246 |
| 16 KB | 1723 | +0 % | 263 | 6513 | 7910 | 16384 |
| 32 KB | 1625 | −5,7 % | 133 | 12236 | 14653 | 32768 |

## 4. Predicțiile

| | predicția | măsurat | |
|---|---|---|---|
| 1 | TEXT comprimat ≤ 50 % din brut, la fiecare carte în proză | cel mai mare raport 44 % | **trece** |
| 2 | cratimele moi adaugă ≤ 10 % la fișier | între 0,1 % și 10,2 % | **nu trece** |
| 3 | deschiderea încape în 4 KB la ≥ 9 din 12 cărți | 9 din 12; nu încap: pg2701, pg1322, pg35323 | **trece** |
| 4 | 32 KB câștigă < 5 %, 4 KB costă > 15 % (proză) | 32 KB: −5,7 %, 4 KB: +15,5 % | **nu trece** |
| 5 | conversia < 5 s pe carte | cea mai lentă pg2701, 1,67 s | **trece** |
