# E5: tabele (generate de analiza.py)

Nu se editează de mână. KB = 1024 B. * = în afara prozei (versuri, culegerea de probleme). Textul simplu = octeții UTF-8 ai paragrafelor, fără cratimele moi.

## 1. Mărimile, varianta implicită (DEFLATE, blocuri de 16 KB, despărțire, copertă)

| carte | sursa | text simplu | TEXT brut | TEXT comprimat | comprimat / brut | comprimat / simplu | copertă | fișierul | deschiderea (B) | conversia (s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| libru-dubrovsky | 135 | 242 | 288 | 88,7 | 31 % | 37 % | 0 | 89,8 | 1178 | 0,56 |
| libru-geroi | 114 | 469 | 554 | 182 | 33 % | 39 % | 0 | 183 | 1450 | 0,85 |
| pg11 | 185 | 146 | 160 | 64,4 | 40 % | 44 % | 8,4 | 74 | 1156 | 0,33 |
| pg1342 | 24282 | 705 | 797 | 330 | 41 % | 47 % | 10,4 | 344 | 3926 | 1,63 |
| pg2701 | 795 | 1198 | 1324 | 583 | 44 % | 49 % | 8,9 | 600 | 7903 | 2,57 |
| pg4300 | 1459 | 1476 | 1657 | 769 | 46 % | 52 % | 6,4 | 780 | 5432 | 3,49 |
| pg62916 | 415 | 290 | 361 | 132 | 36 % | 45 % | 10 | 143 | 1034 | 0,81 |
| pg64597 | 176 | 205 | 260 | 106 | 41 % | 52 % | 5,4 | 113 | 864 | 0,63 |
| pg65565 | 369 | 293 | 379 | 149 | 39 % | 51 % | 8,6 | 158 | 1082 | 0,81 |
| pg84 | 430 | 410 | 461 | 193 | 42 % | 47 % | 7,9 | 202 | 1581 | 0,94 |
| ws-amintiri | 67,4 | 149 | 186 | 75,1 | 40 % | 50 % | 0 | 75,6 | 595 | 0,38 |
| ws-ion | 379 | 944 | 1207 | 483 | 40 % | 51 % | 0 | 486 | 2513 | 2,56 |
| ws-kapitanskaya | 4359 | 411 | 487 | 158 | 32 % | 38 % | 0 | 159 | 1763 | 0,73 |
| ws-moara-cu-noroc | 87,3 | 221 | 273 | 103 | 38 % | 47 % | 0 | 104 | 856 | 0,61 |
| pg1322 * | 840 | 677 | 735 | 341 | 46 % | 50 % | 7,6 | 361 | 12400 | 1,06 |
| pg14741 * | 138 | 186 | 204 | 75,9 | 37 % | 41 % | 3,9 | 82,2 | 2536 | 0,34 |
| pg1524 * | 347 | 171 | 198 | 90,6 | 46 % | 53 % | 7,6 | 99,7 | 1623 | 0,45 |
| pg16527-8 * | 167 | 240 | 280 | 76,7 | 27 % | 32 % | 0 | 77,3 | 675 | 0,63 |
| pg35323 * | 468 | 873 | 1018 | 445 | 44 % | 51 % | 3,2 | 458 | 9210 | 1,78 |
| ws-luceafarul * | 16,4 | 11,9 | 14,6 | 6,4 | 44 % | 54 % | 0 | 6,7 | 271 | 0,04 |

## 2. Ce costă fiecare alegere: mărimea fișierului față de varianta implicită

| carte | stocat (fără compresie) | fără copertă | fără despărțire | cratimele moi, din fișier |
|---|---:|---:|---:|---:|
| libru-dubrovsky | +222,1 % | +0 % | −8,5 % | 8,5 % |
| libru-geroi | +203,6 % | +0 % | −8 % | 8 % |
| pg11 | +129,4 % | −11,4 % | −3,4 % | 3,4 % |
| pg1342 | +135,6 % | −3 % | −6 % | 6 % |
| pg2701 | +123,6 % | −1,5 % | −5,3 % | 5,3 % |
| pg4300 | +113,9 % | −0,8 % | −5,4 % | 5,4 % |
| pg62916 | +160,7 % | −7 % | −7,8 % | 7,8 % |
| pg64597 | +136,1 % | −4,8 % | −8,7 % | 8,7 % |
| pg65565 | +146 % | −5,4 % | −10,2 % | 10,2 % |
| pg84 | +132,9 % | −3,9 % | −6 % | 6 % |
| ws-amintiri | +146,3 % | +0 % | −9 % | 9 % |
| ws-ion | +148,8 % | +0 % | −10,3 % | 10,3 % |
| ws-kapitanskaya | +206,9 % | +0 % | −8,7 % | 8,7 % |
| ws-moara-cu-noroc | +162,8 % | +0 % | −9 % | 9 % |
| pg1322 * | +109,1 % | −2,1 % | −0,1 % | 0,1 % |
| pg14741 * | +156,2 % | −4,8 % | −0,6 % | 0,6 % |
| pg1524 * | +107,7 % | −7,6 % | −1,9 % | 1,9 % |
| pg16527-8 * | +263,4 % | +0 % | −7 % | 7 % |
| pg35323 * | +125,2 % | −0,7 % | −2,3 % | 2,3 % |
| ws-luceafarul * | +121,9 % | +0 % | −2,5 % | 2,5 % |

## 3. Mărimea blocului

Proza, totalul celor 14 cărți. „Citit la un salt” = octeții stocați ai unui bloc (cât se citește de pe card ca să ajungi la o poziție); „RAM” = blocul decomprimat, cel mai mare din toate cărțile.

| bloc țintă | fișierele (KB) | față de 16 KB | blocuri | citit la un salt, mediana (B) | citit, maximul (B) | RAM, maximul (B) |
|---:|---:|---:|---:|---:|---:|---:|
| 4 KB | 4055 | +15,5 % | 2344 | 1752 | 3745 | 8246 |
| 8 KB | 3754 | +6,9 % | 1128 | 3373 | 4492 | 8246 |
| 16 KB | 3511 | +0 % | 550 | 6428 | 8271 | 16384 |
| 32 KB | 3310 | −5,7 % | 274 | 12069 | 15820 | 32768 |

## 4. Predicțiile

| | predicția | măsurat | |
|---|---|---|---|
| 1 | TEXT comprimat ≤ 50 % din brut, la fiecare carte în proză | cel mai mare raport 46 % | **trece** |
| 2 | cratimele moi adaugă ≤ 10 % la fișier | între 0,1 % și 10,3 % | **nu trece** |
| 3 | deschiderea încape în 4 KB la ≥ 3/4 din cărți | 16 din 20; nu încap: pg2701, pg4300, pg1322, pg35323 | **trece** |
| 4 | 32 KB câștigă < 5 %, 4 KB costă > 15 % (proză) | 32 KB: −5,7 %, 4 KB: +15,5 % | **nu trece** |
| 5 | conversia < 5 s pe carte | cea mai lentă pg4300, 3,49 s | **trece** |
