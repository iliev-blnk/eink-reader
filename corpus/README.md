# Test corpus (MEAS-1)

Books used to test the converter (CONV-10) and, later, the layout and the device. The books themselves aren't tracked. `./fetch.sh` downloads them into `files/` and skips any that are already there. Converted output goes to `out/` (also untracked):

    ./fetch.sh
    cd ../tools/converter
    python3 convert.py convert ../../corpus/files/* -o ../../corpus/out
    python3 -m unittest test_convert      # TestCorpus converts and validates every file in files/

All books are public domain. Project Gutenberg texts are public domain in the US. Their licence asks that the "Project Gutenberg" name be removed if the text is redistributed, and the converter drops the PG header and footer (`pg-boilerplate`). Pushkin died in 1837, so his text is public domain everywhere; lib.ru only hosts it.

## Books

| File | Book | Lang | Source | What it tests |
|---|---|---|---|---|
| `pg11.epub` | Carroll, *Alice's Adventures in Wonderland* | en | [PG 11](https://www.gutenberg.org/ebooks/11) | short; poems inside prose |
| `pg84.epub` | Shelley, *Frankenstein* | en | [PG 84](https://www.gutenberg.org/ebooks/84) | letters, many chapters |
| `pg1342.epub` | Austen, *Pride and Prejudice* | en | [PG 1342](https://www.gutenberg.org/ebooks/1342) | 24 MB EPUB (illustrations), long |
| `pg2701.epub` | Melville, *Moby-Dick* | en | [PG 2701](https://www.gutenberg.org/ebooks/2701) | long; Greek and Hebrew words (missing glyphs) |
| `pg2701.txt` | same, plain text | en | [PG 2701](https://www.gutenberg.org/ebooks/2701) | TXT reader against the EPUB on the same book; printed contents list |
| `pg1322.epub` | Whitman, *Leaves of Grass* | en | [PG 1322](https://www.gutenberg.org/ebooks/1322) | long verse lines, 378 TOC entries |
| `pg35323.epub` | Eminescu, *Poezii* | ro | [PG 35323](https://www.gutenberg.org/ebooks/35323) | verse in `<p>` + `<br>`; typed without diacritics |
| `pg64597.epub` | Caragiale, *Nuvele* | ro | [PG 64597](https://www.gutenberg.org/ebooks/64597) | Romanian prose, dialogue |
| `pg64597.txt` | same, plain text | ro | [PG 64597](https://www.gutenberg.org/ebooks/64597) | TXT against EPUB; title-page lines |
| `pg62916.epub` | Slavici, *Povești* | ro | [PG 62916](https://www.gutenberg.org/ebooks/62916) | Romanian prose, ș/ț |
| `pg65565.epub` | Regina Maria, *Țara mea* | ro | [PG 65565](https://www.gutenberg.org/ebooks/65565) | Romanian prose |
| `pg14741.epub` | Derzhavin, *Духовные оды* | ru | [PG 14741](https://www.gutenberg.org/ebooks/14741) | Russian verse, small-caps headings |
| `pg16527-8.txt` | Rachinsky, *1001 задача для умственного счёта* | ru | [PG 16527](https://www.gutenberg.org/ebooks/16527) | CP1251 plain text; numbers, short paragraphs |
| `libru-dubrovsky.txt` | Pushkin, *Дубровский* | ru | [lib.ru](http://lib.ru/LITRA/PUSHKIN/dubrowskij.txt) | KOI8-R; lib.ru header and `\x14…\x15` headings |
| `libru-dubrovsky-cp1251.txt` | same, converted with `iconv` | ru | (made by `fetch.sh`) | CP1251 detection against KOI8-R on the same text |

15 files, 13 distinct books. The plan asks for about 20. Still missing:

- a real FB2 book. FB2 is covered by a synthetic file in `test_convert.py` only. The usual FB2 sources (Flibusta, lib.rus.ec) have unclear rights, so add your own `.fb2` / `.fb2.zip` to `files/` and rerun.
- a Romanian TXT in CP1250 or ISO-8859-16. CP1250 detection is covered by `TestEncoding` only.

## Results (2026-10-26)

Converter at ibv-convert 0.1, 16 KB blocks, DEFLATE, hyphenation on. Times are on the dev laptop and include parsing, hyphenation, writing and validation.

| File | In | Out | Out/in | Records | Blocks | TOC | Hyphenated words | Cover | Time |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|
| libru-dubrovsky.txt | 136 KB | 90 KB | 67 % | 429 | 19 | 21 | 12,549 | – | 0.73 s |
| libru-dubrovsky-cp1251.txt | 136 KB | 90 KB | 67 % | 429 | 19 | 21 | 12,549 | – | 0.70 s |
| pg11.epub | 188 KB | 74 KB | 40 % | 1,034 | 11 | 16 | 4,091 | 240×331 | 0.42 s |
| pg84.epub | 432 KB | 202 KB | 47 % | 850 | 30 | 32 | 17,715 | 240×368 | 1.12 s |
| pg1322.epub | 840 KB | 361 KB | 43 % | 14,550 | 47 | 378 | 328 | 240×320 | 1.27 s |
| pg1342.epub | 24,284 KB | 344 KB | 1 % | 2,430 | 52 | 63 | 28,971 | 240×373 | 1.87 s |
| pg14741.epub | 140 KB | 82 KB | 60 % | 4,041 | 13 | 53 | 779 | 240×360 | 0.40 s |
| pg16527-8.txt | 168 KB | 77 KB | 46 % | 1,005 | 18 | 0 | 12,436 | – | 0.74 s |
| pg2701.epub | 796 KB | 600 KB | 75 % | 2,954 | 86 | 145 | 42,792 | 240×378 | 2.81 s |
| pg2701.txt | 1,248 KB | 589 KB | 47 % | 2,823 | 86 | 137 | 42,940 | – | 3.21 s |
| pg35323.epub | 468 KB | 458 KB | 98 % | 22,951 | 71 | 282 | 14,443 | 240×360 | 2.26 s |
| pg62916.epub | 416 KB | 143 KB | 34 % | 1,559 | 23 | 10 | 22,214 | 240×370 | 0.98 s |
| pg64597.epub | 176 KB | 113 KB | 64 % | 977 | 17 | 12 | 16,240 | 240×360 | 0.76 s |
| pg64597.txt | 232 KB | 107 KB | 47 % | 967 | 17 | 15 | 16,244 | – | 0.66 s |
| pg65565.epub | 372 KB | 158 KB | 43 % | 1,079 | 25 | 16 | 24,607 | 240×355 | 1.00 s |

Out/in compares against the source file, which for EPUB is already zipped, so the ratio says little. Covers are 1 bpp, at most 240×416, and make up part of the output.

The same book from two sources comes out almost the same size (Moby-Dick 600 / 589 KB, Caragiale 113 / 107 KB; the EPUB versions carry a cover). Both Dubrovsky encodings give byte-identical books.

Verse books hyphenate few words (Whitman 328, Derzhavin 779) because VERSE records are never hyphenated. Eminescu has many records because every verse line is one.

## Known source problems (not converter bugs)

- **Eminescu (pg35323)** is typed without diacritics ("si" for "și"). The converter can't put them back.
- **Derzhavin (pg14741)** has no gaps between stanzas in the source, so the poems come out as one long stanza.
- **Moby-Dick** has 7 Greek and Hebrew letters the font doesn't cover. They become U+FFFD and are listed as MISSING in the report.
- **Rachinsky (pg16527-8.txt)** has no recognisable headings, so the book has no TOC.
- **Caragiale TXT (pg64597.txt)**: all-caps title-page lines ("SCRIITORII ROMÂNI") look like headings and end up in the TOC (15 entries against 12 in the EPUB). TXT has no markup, so this is expected.
