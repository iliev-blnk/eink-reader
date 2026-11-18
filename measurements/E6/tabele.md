# E6: tabele (generate de analiza.py)

Nu se editează de mână. Proza = rândurile celor 14 cărți în proză, puse împreună. r = cât de lat e spațiul justificat față de unul simplu (0 = la fel, 1 = dublu).

Hash-urile paginării la cele 5 rulări repetate: identice.

## 1. Proza, treapta implicită (2)

| condiția | rânduri justificate | r medie | r mediană | r p95 | r > 0,5 | r > 1 | strânse (r < 0) | cratimă | 2 cratime / 1000 rânduri | pagini |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| greedy, fără despărțire | 264.250 | 3,11 | 1,88 | 10,21 | 84 % | 69,9 % | 0 % | 0 % | 0 | 19.936 |
| Knuth–Plass, fără despărțire | 260.019 | 2,35 | 1,75 | 6,7 | 80,9 % | 68,4 % | 7,1 % | 0 % | 0 | 19.560 |
| greedy, cu despărțire | 254.865 | 1,53 | 1,11 | 4,32 | 75,2 % | 54,1 % | 0 % | 22,3 % | 57,5 | 19.228 |
| Knuth–Plass, cu despărțire | 250.330 | 1,13 | 0,98 | 2,86 | 69,2 % | 49 % | 10,9 % | 20,2 % | 29,2 | 18.890 |

## 2. Pe carte, cu despărțire, treapta 2

| carte | font | greedy r medie | K–P r medie | schimbare | greedy r p95 | K–P r p95 | schimbare | greedy 2cr/1000 | K–P 2cr/1000 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| libru-dubrovsky | ptserif-18 | 1,55 | 1,18 | −24 % | 4,38 | 2,77 | −37 % | 165,7 | 113,6 |
| libru-geroi | ptserif-18 | 1,53 | 1,13 | −26 % | 4,36 | 2,74 | −37 % | 121,5 | 82,9 |
| pg11 | libron-17 | 1,59 | 1,16 | −27 % | 4,25 | 2,94 | −31 % | 11,1 | 5,6 |
| pg1342 | libron-17 | 1,57 | 1,13 | −28 % | 4,26 | 2,81 | −34 % | 35,8 | 19,9 |
| pg2701 | libron-17 | 1,74 | 1,25 | −28 % | 4,73 | 3,03 | −36 % | 27,1 | 20,5 |
| pg4300 | libron-17 | 1,83 | 1,33 | −27 % | 5,16 | 3,43 | −34 % | 28,3 | 23,2 |
| pg62916 | libron-17 | 0,99 | 0,79 | −20 % | 2,65 | 2 | −24 % | 61,3 | 5,3 |
| pg64597 | libron-17 | 1,11 | 0,87 | −22 % | 2,97 | 2,19 | −26 % | 81,2 | 15,2 |
| pg65565 | libron-17 | 1,12 | 0,89 | −20 % | 3,01 | 2,27 | −25 % | 96,5 | 17,8 |
| pg84 | libron-17 | 1,58 | 1,14 | −28 % | 4,18 | 2,74 | −34 % | 42,7 | 19,1 |
| ws-amintiri | libron-17 | 1,03 | 0,84 | −18 % | 2,81 | 2,04 | −27 % | 75,8 | 6,6 |
| ws-ion | libron-17 | 1,09 | 0,88 | −19 % | 2,92 | 2,17 | −26 % | 94,9 | 17 |
| ws-kapitanskaya | ptserif-18 | 1,96 | 1,32 | −32 % | 6,19 | 3,2 | −48 % | 156,6 | 168,4 |
| ws-moara-cu-noroc | libron-17 | 0,98 | 0,77 | −21 % | 2,62 | 1,9 | −28 % | 54,3 | 3,7 |
| pg1322 (în afara prozei) | libron-17 | 2,75 | 2,02 | −27 % | 6,62 | 6,09 | −8 % | 0 | 0 |
| pg14741 (în afara prozei) | ptserif-18 | 1,45 | 1,03 | −29 % | 3,63 | 2,67 | −26 % | 3,3 | 2,8 |
| pg1524 (în afara prozei) | libron-17 | 1,52 | 1,08 | −29 % | 4,02 | 2,77 | −31 % | 2,4 | 1,2 |
| pg16527-8 (în afara prozei) | ptserif-18 | 1,33 | 0,97 | −27 % | 3,7 | 2,54 | −32 % | 64,6 | 28,6 |
| pg35323 (în afara prozei) | libron-17 | 1,46 | 1,15 | −21 % | 4,19 | 2,56 | −39 % | 17,9 | 7,2 |
| ws-luceafarul (în afara prozei) | libron-17 | 1,5 | 0,74 | −51 % | 7,16 | 1,82 | −75 % | 1,7 | 3,4 |
| **proza** |  | 1,53 | 1,13 | −26 % | 4,32 | 2,86 | −34 % | 57,5 | 29,2 |

## 3. Proza pe trepte de mărime: r medie (r p95)

| condiția | treapta 0 | treapta 1 | treapta 2 | treapta 3 | treapta 4 |
|---|---:|---:|---:|---:|---:|
| greedy, fără despărțire | 2,53 (7,99) | 2,82 (9,08) | 3,11 (10,21) | 3,39 (11,72) | 3,9 (14,24) |
| Knuth–Plass, fără despărțire | 1,87 (5,24) | 2,1 (5,92) | 2,35 (6,7) | 2,62 (7,53) | 3,12 (9,29) |
| greedy, cu despărțire | 1,27 (3,47) | 1,4 (3,86) | 1,53 (4,32) | 1,68 (4,81) | 1,99 (5,91) |
| Knuth–Plass, cu despărțire | 0,95 (2,42) | 1,03 (2,63) | 1,13 (2,86) | 1,23 (3,13) | 1,45 (3,77) |

## 4. Timpul de paginare pe PC, treapta 2

Mediana celor 5 rulări; o rulare paginează toată cartea și construiește indexul paginilor.

| carte | greedy, fără despărțire (ms) | Knuth–Plass, fără despărțire (ms) | greedy, cu despărțire (ms) | Knuth–Plass, cu despărțire (ms) | K–P / greedy, cu despărțire |
|---|---:|---:|---:|---:|---:|
| libru-dubrovsky | 11 | 11 | 11,3 | 15,9 | 1,41 |
| libru-geroi | 20,7 | 21,6 | 21,3 | 28,5 | 1,34 |
| pg11 | 8,5 | 9,8 | 8,6 | 10,4 | 1,21 |
| pg1342 | 43,6 | 51,3 | 44,1 | 58,6 | 1,33 |
| pg2701 | 74,5 | 88,2 | 76,9 | 102,7 | 1,34 |
| pg4300 | 96,1 | 111,9 | 97,4 | 125,9 | 1,29 |
| pg62916 | 17,3 | 20,4 | 18,4 | 25,8 | 1,4 |
| pg64597 | 13,4 | 15,2 | 13,8 | 19,2 | 1,39 |
| pg65565 | 19,2 | 21,7 | 19,7 | 29,1 | 1,48 |
| pg84 | 25 | 29,6 | 25,5 | 34,1 | 1,34 |
| ws-amintiri | 9,6 | 11,8 | 9,8 | 14,6 | 1,49 |
| ws-ion | 61,9 | 71,6 | 64,3 | 93,4 | 1,45 |
| ws-kapitanskaya | 18,7 | 17,6 | 18,4 | 26,3 | 1,43 |
| ws-moara-cu-noroc | 13,5 | 16,3 | 14,1 | 20,1 | 1,43 |
| pg1322 | 42,9 | 42,1 | 43 | 42,7 | 0,99 |
| pg14741 | 7,8 | 7,8 | 7,8 | 8,4 | 1,08 |
| pg1524 | 11,1 | 11,3 | 10,8 | 11,4 | 1,06 |
| pg16527-8 | 9,7 | 10,9 | 10,5 | 13,2 | 1,26 |
| pg35323 | 53 | 55 | 53,3 | 64,2 | 1,2 |
| ws-luceafarul | 0,7 | 0,8 | 0,7 | 0,8 | 1,14 |
| **proza, total** | 433 | 498 | 443,6 | 604,6 | **1,36** |

## 5. Predicțiile

| | predicția | măsurat | |
|---|---|---|---|
| 1 | K–P scade r medie ≥ 15 % și r p95 ≥ 20 % (proză, despărțire, treapta 2) | r medie −26 %, r p95 −34 % | **trece** |
| 2 | fără despărțire r medie ≥ 1,5× mai mare, la amândoi algoritmii; K–P nu compensează | greedy 2,03×, K–P 2,08×; K–P fără despărțire 2,35 față de greedy cu despărțire 1,53 | **trece** |
| 3 | K–P are mai puține cratime consecutive | greedy 57,5, K–P 29,2 la 1000 de rânduri | **trece** |
| 4 | spațierea e cea mai proastă la treapta 4, la toate variantele | greedy, fără despărțire: maxim la treapta 4, Knuth–Plass, fără despărțire: maxim la treapta 4, greedy, cu despărțire: maxim la treapta 4, Knuth–Plass, cu despărțire: maxim la treapta 4 | **trece** |
| 5 | K–P ≤ 2× timpul greedy pe PC | 1,36× | **trece** |
