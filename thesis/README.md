# Lucrarea de diplomă

Sursa lucrării, în LaTeX. Capitolele se scriu pe măsură ce se închide faza lor (TH-3).

    thesis/build.sh          # → thesis/build/teza.pdf (LuaLaTeX + BibTeX)
    python3 thesis/verifica_surse.py   # reverifică referințele (rețea)

`build.sh` iese cu eroare dacă o citare sau o trimitere rămâne nedefinită. Despărțirea în silabe folosește aceleași modele ca și convertorul (`tools/converter/patterns/hyph-ro.pat.txt`), ca în monografie.

| Fișier | Ce e |
|---|---|
| `teza.tex` | documentul principal; preambulul e provizoriu |
| `capitole/02-stadiu-actual.tex` | capitolul 2, stadiul actual (TH-2) |
| `referinte.bib` | referințele, BibTeX |
| `verifica_surse.py` | verificarea automată a referințelor |

## Formatul

Șablonul facultății și stilul citărilor nu se știu încă (TH-1). Până atunci, `teza.tex` folosește clasa `report` și stilul numeric `unsrtnat`. Capitolele folosesc doar `\citep`, `\ref` și medii LaTeX obișnuite (`table`, `tabularx`, `booktabs`), deci trec în șablon fără rescriere. BibTeX se poate converti în orice stil de citare, iar dacă șablonul e Word, `pandoc` citește și `.tex`, și `.bib`.

## Verificarea surselor

Nicio referință nu e scrisă din memorie. Pentru fiecare intrare din `referinte.bib`:

- **articolele** (DOI): titlul, anul și autorii sunt luați din Crossref. Rezumatul, din Crossref sau OpenAlex, e citit ca să se știe ce susține lucrarea. Textul nu atribuie unei lucrări nimic ce nu apare în rezumat;
- **specificațiile** (EPUB 3.3, RFC 1951, UAX #14, CSS Text 4) sunt citite la sursă, iar pasajul pe care se sprijină textul e căutat în ele;
- **depozitele** (KOReader, CrossPoint, libros etc.): licența și datele vin din API-ul GitHub. Afirmațiile despre cod (cum se rup rândurile, ce cere memoria) sunt verificate în sursă, la commitul notat în intrare;
- **foile de catalog** (panoul E037A75, ESP32-S3) sunt citite în PDF, la versiunea notată.

Câmpul `verificat` al fiecărei intrări (ignorat de BibTeX) spune pe scurt ce s-a găsit la sursă. `year` e anul versiunii citite: data documentului sau a ultimului commit din depozit la accesare. Sursele fără dată au „f.a.”. `verifica_surse.py` refăce automat partea mecanică: Crossref pentru DOI-uri, HTTP 200 pentru adrese. Ultima rulare: 22.11.2026, 25 din 25.

Ce nu s-a putut verifica a rămas pe dinafară: paginile Amazon și NXP (blochează descărcarea automată), statutul oficial al contului GitHub „pocketbook”, afirmațiile despre Open Book Touch și despre panourile Inkplate găsite doar în rezumate de căutare.
