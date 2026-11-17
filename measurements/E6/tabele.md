# E6: tabele (generate de analiza.py)

Nu se editează de mână. Proza = rândurile celor 8 cărți în proză, puse împreună. r = cât de lat e spațiul justificat față de unul simplu (0 = la fel, 1 = dublu).

Hash-urile paginării la cele 5 rulări repetate: identice.

## 1. Proza, treapta implicită (2)

| condiția | rânduri justificate | r medie | r mediană | r p95 | r > 0,5 | r > 1 | strânse (r < 0) | cratimă | 2 cratime / 1000 rânduri | pagini |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| greedy, fără despărțire | 134.780 | 2,89 | 1,81 | 9,21 | 83,6 % | 69,2 % | 0 % | 0 % | 0 | 10.086 |
| Knuth–Plass, fără despărțire | 132.310 | 2,13 | 1,69 | 5,68 | 80,4 % | 67,6 % | 7,3 % | 0 % | 0 | 9.884 |
| greedy, cu despărțire | 130.307 | 1,53 | 1,15 | 4,23 | 76 % | 55,3 % | 0 % | 20,5 % | 47,6 | 9.755 |
| Knuth–Plass, cu despărțire | 127.876 | 1,12 | 0,99 | 2,8 | 69,5 % | 49,7 % | 10,8 % | 18,6 % | 22,1 | 9.579 |

## 2. Pe carte, cu despărțire, treapta 2

| carte | font | greedy r medie | K–P r medie | schimbare | greedy r p95 | K–P r p95 | schimbare | greedy 2cr/1000 | K–P 2cr/1000 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| libru-dubrovsky | ptserif-18 | 1,55 | 1,18 | −24 % | 4,38 | 2,77 | −37 % | 165,7 | 113,6 |
| pg11 | libron-17 | 1,59 | 1,16 | −27 % | 4,25 | 2,94 | −31 % | 11,1 | 5,6 |
| pg1342 | libron-17 | 1,57 | 1,13 | −28 % | 4,26 | 2,81 | −34 % | 35,8 | 19,9 |
| pg2701 | libron-17 | 1,74 | 1,25 | −28 % | 4,73 | 3,03 | −36 % | 27,1 | 20,5 |
| pg62916 | libron-17 | 0,99 | 0,79 | −20 % | 2,65 | 2 | −24 % | 61,3 | 5,3 |
| pg64597 | libron-17 | 1,11 | 0,87 | −22 % | 2,97 | 2,19 | −26 % | 81,2 | 15,2 |
| pg65565 | libron-17 | 1,12 | 0,89 | −20 % | 3,01 | 2,27 | −25 % | 96,5 | 17,8 |
| pg84 | libron-17 | 1,58 | 1,14 | −28 % | 4,18 | 2,74 | −34 % | 42,7 | 19,1 |
| pg1322 (în afara prozei) | libron-17 | 2,75 | 2,02 | −27 % | 6,62 | 6,09 | −8 % | 0 | 0 |
| pg14741 (în afara prozei) | ptserif-18 | 1,45 | 1,03 | −29 % | 3,63 | 2,67 | −26 % | 3,3 | 2,8 |
| pg16527-8 (în afara prozei) | ptserif-18 | 1,33 | 0,97 | −27 % | 3,7 | 2,54 | −32 % | 64,6 | 28,6 |
| pg35323 (în afara prozei) | libron-17 | 1,46 | 1,15 | −21 % | 4,19 | 2,56 | −39 % | 17,9 | 7,2 |
| **proza** |  | 1,53 | 1,12 | −27 % | 4,23 | 2,8 | −34 % | 47,6 | 22,1 |

## 3. Proza pe trepte de mărime: r medie (r p95)

| condiția | treapta 0 | treapta 1 | treapta 2 | treapta 3 | treapta 4 |
|---|---:|---:|---:|---:|---:|
| greedy, fără despărțire | 2,33 (7,18) | 2,6 (8,22) | 2,89 (9,21) | 3,18 (10,33) | 3,71 (13,36) |
| Knuth–Plass, fără despărțire | 1,7 (4,47) | 1,89 (5,01) | 2,13 (5,68) | 2,37 (6,44) | 2,89 (8,01) |
| greedy, cu despărțire | 1,26 (3,4) | 1,39 (3,77) | 1,53 (4,23) | 1,68 (4,68) | 2 (5,77) |
| Knuth–Plass, cu despărțire | 0,94 (2,37) | 1,02 (2,58) | 1,12 (2,8) | 1,22 (3,08) | 1,45 (3,71) |

## 4. Timpul de paginare pe PC, treapta 2

Mediana celor 5 rulări; o rulare paginează toată cartea și construiește indexul paginilor.

| carte | greedy, fără despărțire (ms) | Knuth–Plass, fără despărțire (ms) | greedy, cu despărțire (ms) | Knuth–Plass, cu despărțire (ms) | K–P / greedy, cu despărțire |
|---|---:|---:|---:|---:|---:|
| libru-dubrovsky | 8,2 | 8,1 | 8,1 | 19,7 | 2,43 |
| pg11 | 6,6 | 7,2 | 6,3 | 12,8 | 2,03 |
| pg1342 | 32,7 | 36,4 | 33,7 | 79 | 2,34 |
| pg2701 | 62,1 | 63,6 | 56,7 | 123,7 | 2,18 |
| pg62916 | 12,9 | 15,2 | 14 | 30,6 | 2,19 |
| pg64597 | 10,4 | 11 | 10 | 23,7 | 2,37 |
| pg65565 | 14,9 | 15,6 | 14,5 | 36,1 | 2,49 |
| pg84 | 19 | 21,5 | 19,1 | 50,4 | 2,64 |
| pg1322 | 32,6 | 30,9 | 32,2 | 52,5 | 1,63 |
| pg14741 | 6,4 | 5,7 | 6 | 10 | 1,67 |
| pg16527-8 | 7,5 | 7,8 | 7,9 | 15,3 | 1,94 |
| pg35323 | 39,4 | 39,4 | 43 | 83,5 | 1,94 |
| **proza, total** | 166,8 | 178,6 | 162,4 | 376 | **2,32** |

## 5. Predicțiile

| | predicția | măsurat | |
|---|---|---|---|
| 1 | K–P scade r medie ≥ 15 % și r p95 ≥ 20 % (proză, despărțire, treapta 2) | r medie −27 %, r p95 −34 % | **trece** |
| 2 | fără despărțire r medie ≥ 1,5× mai mare, la amândoi algoritmii; K–P nu compensează | greedy 1,89×, K–P 1,9×; K–P fără despărțire 2,13 față de greedy cu despărțire 1,53 | **trece** |
| 3 | K–P are mai puține cratime consecutive | greedy 47,6, K–P 22,1 la 1000 de rânduri | **trece** |
| 4 | spațierea e cea mai proastă la treapta 4, la toate variantele | greedy, fără despărțire: maxim la treapta 4, Knuth–Plass, fără despărțire: maxim la treapta 4, greedy, cu despărțire: maxim la treapta 4, Knuth–Plass, cu despărțire: maxim la treapta 4 | **trece** |
| 5 | K–P ≤ 2× timpul greedy pe PC | 2,32× | **nu trece** |
