# Hyphenation patterns

These are Liang patterns from the TeX hyph-utf8 project (https://github.com/hyphenation/tex-hyphen, `hyph-utf8/tex/generic/hyph-utf8/patterns/txt/`), downloaded 2026-10-26. `hyphenate.py` reads them. The `*.header.txt` files hold each pattern file's original header, which includes its copyright and licence, unchanged.

| Language | Files | Copyright | Licence | Min. left/right |
|---|---|---|---|---|
| ro | `hyph-ro.pat.txt` | Adrian Rezus, 1995–1996 | None stated. Distributed with TeX Live and hyph-utf8 since 1996. | 2 / 2 |
| ru | `hyph-ru.pat.txt`, `hyph-ru.hyp.txt` | Alexander I. Lebedev, 1999–2003 | LPPL 1.2 or later | 2 / 2 |
| en-us | `hyph-en-us.pat.txt`, `hyph-en-us.hyp.txt` | Gerard D.C. Kuiken, 1990–2005 | Permissive. Copying and distribution are allowed if the notice is kept (see the header). | 2 / 3 |
| en-gb | `hyph-en-gb.pat.txt`, `hyph-en-gb.hyp.txt` | Dominik Wujastyk, Graham Toal, 1992–2016 | MIT | 2 / 3 |

`*.pat.txt` holds one pattern per line. `*.hyp.txt` holds one exception word per line, with its break points written as `-`.

The Romanian patterns have no stated licence. This only affects distributing the converter. The `.ibv` files it produces don't contain the patterns, only the soft hyphens placed by them. If that matters for publishing the code, the fallback is the LibreOffice `hyph_ro_RO` dictionary, which is GPL/LGPL/MPL.
