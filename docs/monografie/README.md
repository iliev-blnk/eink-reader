# Monografia

Lucrarea scrisă, în LaTeX: `monografie.tex`, cu planșele desenate în MetaPost
(`planse/`) și figurile citite direct din `measurements/` la fiecare compilare.

```sh
docs/monografie/build.sh      # → docs/monografie/build/monografie.pdf
```

## Ce îi trebuie

LuaLaTeX (TeX Live), `mpost`, `python3`, `git`, `curl`.

`build.sh` aduce singur, în `.dep/` (ignorat de git), ce lipsește:

- [fiziko](https://github.com/jemmybutton/fiziko) (GPL-3.0, Sergey Slyusarev),
  la un commit fix, pentru planșe;
- pachetele TeX Live care lipsesc dintr-o instalare minimă (`luaotfload`,
  `lualibs`, `lm`, `lm-math`, `lualatex-math`, `metapost`), din arhiva CTAN,
  numai dacă `kpsewhich` nu le găsește. Pe Arch se pot pune și din sistem:
  `pacman -S texlive-luatex texlive-metapost` (restul se aduce oricum).

Fontul e Libron, cel al cititorului; dacă lipsește, îl aduce
`tools/fonttool/fetch-fonts.sh`. Despărțirea în silabe folosește tiparele
convertorului (`tools/converter/patterns/hyph-ro.pat.txt`), iar paragrafele
sunt rupte cu parametrii dispozitivului (`firmware/components/core/layout.c`).

## Planșa II

Paginile din Planșa II se desenează cu simulatorul. Trebuie construite înainte
`sim/build/sim`, fonturile (`tools/fonttool/out/fam`) și cartea
`corpus/out/pg64597.ibv`; fără ele planșa rămâne cu un loc gol, iar restul
lucrării se compilează la fel.

## Ce e în ea

Toate cifrele vin din fișierele din depozit, iar fiecare are pe margine
fișierul din care vine. Nimic nu a rulat încă pe hardware, iar lucrarea o spune
(„Declarație asupra limitelor”, § I, și § VI).
