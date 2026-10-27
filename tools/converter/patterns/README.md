# Modele de despărțire în silabe

Sunt modele Liang din proiectul TeX hyph-utf8 (https://github.com/hyphenation/tex-hyphen, `hyph-utf8/tex/generic/hyph-utf8/patterns/txt/`), descărcate pe 2026-10-26. Le citește `hyphenate.py`. Fișierele `*.header.txt` păstrează neschimbat antetul original al fiecărui fișier de modele, cu drepturile de autor și licența lui.

| Limba | Fișiere | Drepturi de autor | Licență | Min. stânga/dreapta |
|---|---|---|---|---|
| ro | `hyph-ro.pat.txt` | Adrian Rezus, 1995–1996 | Nu e declarată. Distribuit cu TeX Live și hyph-utf8 din 1996. | 2 / 2 |
| ru | `hyph-ru.pat.txt`, `hyph-ru.hyp.txt` | Alexander I. Lebedev, 1999–2003 | LPPL 1.2 sau ulterioară | 2 / 2 |
| en-us | `hyph-en-us.pat.txt`, `hyph-en-us.hyp.txt` | Gerard D.C. Kuiken, 1990–2005 | Permisivă. Copierea și distribuirea sunt permise dacă se păstrează nota (vezi antetul). | 2 / 3 |
| en-gb | `hyph-en-gb.pat.txt`, `hyph-en-gb.hyp.txt` | Dominik Wujastyk, Graham Toal, 1992–2016 | MIT | 2 / 3 |

`*.pat.txt` are câte un model pe rând. `*.hyp.txt` are câte un cuvânt de excepție pe rând, cu punctele de despărțire scrise ca `-`.

Modelele românești nu au licență declarată. Asta privește doar distribuirea convertorului. Fișierele `.ibv` pe care le produce nu conțin modelele, ci doar cratimele moi puse cu ajutorul lor. Dacă asta contează la publicarea codului, rezerva e dicționarul LibreOffice `hyph_ro_RO`, sub GPL/LGPL/MPL.
