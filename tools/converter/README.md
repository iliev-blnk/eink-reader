# ibv-convert

Turns EPUB, FB2 and TXT books into `.ibv` files for the reader (format: `../../spec/book-format.md`). It is a Python 3 CLI that needs Pillow and numpy (`pip install pillow numpy`).

    python3 convert.py convert book.epub                 # -> book.ibv next to it
    python3 convert.py convert *.epub *.txt -o out/      # a batch; one bad book doesn't stop it
    python3 convert.py convert old.txt --lang ro --title "Moara cu noroc" --author "Ioan Slavici"
    python3 convert.py validate out/*.ibv
    python3 convert.py dump out/book.ibv --records 40    # header, META, TOC, blocks, first records
    python3 convert.py preview-cover out/book.ibv cover.png

Other `convert` options: `--no-hyphenate`, `--no-cover`, `--block BYTES` (block target, default 16384) and `--stored` (no compression, so the hex is readable).

Every book is read back and validated before it is written. The report lists the encoding (TXT), how paragraphs were found, the TOC size, how many words were hyphenated, characters replaced or missing from the font, and warnings.

## Files

| File | Task | What it does |
|---|---|---|
| `convert.py` | | the CLI |
| `txt.py` | CONV-1 | TXT: encoding detection, Gutenberg/lib.ru headers, paragraph mode, headings, breaks, verse, printed contents lists |
| `epub.py` | CONV-2 | EPUB 2/3: OPF, spine, nav/NCX TOC, XHTML → records, CSS bold/italic, cover |
| `fb2.py` | CONV-9 | FB2 and `.fb2.zip` |
| `doc.py` | | the shared `Builder` the readers fill, and `finish()` (below) |
| `textnorm.py` | CONV-3 | NFC, Romanian ş/ţ → ș/ț (and ã/º/þ legacy typing), quotes per language, dashes, ellipsis, spaces |
| `hyphenate.py` | CONV-4 | Liang's algorithm with the TeX patterns in `patterns/` |
| `glyphs.py` | CONV-5 | font coverage against `../fonttool/fonttool.py`'s ranges, fallbacks, U+FFFD |
| `cover.py` | CONV-6 | cover → fit 240×416 → contrast stretch → Floyd–Steinberg → 1 bpp |
| `ibv.py` | CONV-7, SPEC-1 | the format: write, parse, validate, dump |
| `test_ibv.py`, `test_convert.py` | CONV-10 | tests |

## Pipeline

A reader parses the input into a `doc.Builder`: styled text, paragraph ends with a kind, breaks, anchors and TOC entries. `doc.finish()` does the rest, the same for every input:

1. Text clean-up: normalize, map missing glyphs, tidy line breaks.
2. A `<br>`-joined paragraph whose lines are all short (≤ 80 characters, mean ≤ 55) is a stanza, so it becomes VERSE lines with BREAKs between stanzas.
3. Structure: drop empty paragraphs, merge repeated breaks, drop breaks at the edges and next to headings, no indent after a heading or break.
4. Hyphenation: soft hyphens in PARA and QUOTE only. Verse and headings aren't hyphenated.
5. Paragraphs over 8 KB are split into CONT records. Styles become byte runs, and TOC targets become paragraph numbers.

Then `ibv.write()` and `ibv.validate()`.

## Tests

    python3 -m unittest test_ibv test_convert

66 tests, about 20 s. `test_convert.py` builds its EPUB and FB2 inputs in code, so it needs no files. `TestCorpus` also converts every book in `../../corpus/files/` when that folder exists; see `../../corpus/README.md` and `fetch.sh`.

## Hyphenation patterns

These are the TeX hyph-utf8 patterns. `patterns/README.md` lists each file's source, copyright and licence. In short: en-gb is MIT, en-us is permissive with the notice kept, and ru is LPPL. ro has no stated licence (the fallback is the LibreOffice dictionary). The patterns aren't copied into the `.ibv` files, only the soft hyphens they produce.

The algorithm is written here (about 120 lines) rather than taken from pyphen. That keeps it small enough to port to the firmware if the device ever needs to hyphenate itself, and it reads the TeX files directly.
