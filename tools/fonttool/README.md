# fonttool

Transformă fonturi TTF/OTF în fonturi bitmap IBF pentru cititor (formatul: `../../spec/font-format.md`). Python 3 cu Pillow compilat cu libraqm (HarfBuzz), care vine în wheel-urile standard Pillow.

    ./fetch-fonts.sh                                     # Literata, PT Serif, Noto Serif -> fonts/ (OFL, neurmărite în git)
    python3 fonttool.py family literata --sizes 16,18,20 -o out/fam    # -> out/fam/literata-18-r.ibf, -b, -i, -bi
    # setul dispozitivului (spec/font-format.md, secțiunea 4): out/fam/libron-* și ptserif-* se copiază în /fonts/ pe card
    python3 fonttool.py family libron --sizes 15,16,17,18,20 -o out/fam
    python3 fonttool.py family ptserif --sizes 16,17,18,19,21 -o out/fam
    python3 fonttool.py build --ttf X.ttf --size 18 --style i -o x.ibf # o singură față
    python3 fonttool.py c --ttf NotoSans-Regular.ttf --size 16 --name font_ui16 --out ../../firmware/components/core/fonts/font_ui16.c
    python3 fonttool.py validate out/fam/*.ibf
    python3 fonttool.py dump out/fam/literata-18-r.ibf --sample "Șa Va"
    python3 fonttool.py sheet out/fam/*-18-r.ibf -o sheet.png          # foaie de specimen, desenată cum desenează dispozitivul
    python3 -m unittest test_fonttool                                  # 23 de teste, ~4 s

Opțiunile lui `build`: `--render mono|gray` (gray = cu antialiasing și prag la `--threshold`, mai neted pentru serifele subțiri), `--axes wght=700`, `--opsz` (mărimea optică din mărimea în pixeli la 130 dpi), `--ranges`, `--no-kern`.

## Ce face o compilare

1. Desenează fiecare punct de cod din intervale (latin, românesc, chirilic, punctuație; aceeași listă după care convertorul verifică cărțile) prin HarfBuzz, ca bitmapul și avansul să vină de la aceeași glifă. Literata schimbă glifele după mărimea optică, iar FreeType singur nu, ceea ce lăsa cândva un gol după fiecare „w” cursiv.
2. Punctele de cod care lipsesc din font primesc un înlocuitor (U+2011 → '-', o literă cu diacritic → litera de bază) sau sunt omise și raportate.
3. Kerning: ajustările de perechi ale fontului, măsurate prin HarfBuzz. Literele cu diacritice fac kerning ca litera de bază (`kern_as`), iar doar perechile unde asta ar fi greșit se păstrează ca excepții. Literata a scăzut astfel de la 7.596 de perechi la 3.844.
4. Scrie fișierul și îl recitește prin codecul de referință `ibf.py`, care verifică fiecare regulă din specificație.

## Candidați (text curent, fața regular)

| Familie | Desenare | 16 px | 18 px | 20 px | Rânduri pe pagină la 18 px |
|---|---|---|---|---|---|
| Literata | gray, opsz | 35,7 KB | 38,2 KB | 41,2 KB | 12 |
| PT Serif | mono | 28,3 KB | 30,5 KB | 32,1 KB | 14 |
| Noto Serif | mono | 19,1 KB | 20,7 KB | 22,7 KB | 13 |

Aceeași mărime în pixeli nu e aceeași mărime vizuală: Literata 16 e cam cât Noto Serif 18. Comparația se face în simulator: `sim compare BOOK.ibv -o cmp.png out/fam/literata-16 out/fam/ptserif-18 …`.
