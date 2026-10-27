#!/usr/bin/env python3
"""ibv-convert: EPUB / FB2 / TXT -> fișiere de carte .ibv pentru cititorul e-ink.

    convert.py convert BOOK [BOOK ...] [-o OUT] [--lang ro] [--title T] [--author A]
                        [--no-hyphenate] [--no-cover] [--block 16384] [--stored]
    convert.py validate FILE.ibv [...]
    convert.py dump FILE.ibv [--records 40]
    convert.py preview-cover FILE.ibv OUT.png

Fiecare carte convertită se recitește și se validează înainte de a fi scrisă.
"""
from __future__ import annotations

import argparse
import pathlib
import sys
import time

import cover as cover_mod
import doc
import epub
import fb2
import ibv
import txt

READERS = {".epub": epub, ".fb2": fb2, ".txt": txt, ".text": txt}


def reader_for(path: pathlib.Path):
    name = path.name.lower()
    if name.endswith(".fb2.zip"):
        return fb2
    return READERS.get(path.suffix.lower())


def convert(src: pathlib.Path, *, lang=None, title=None, author=None, hyphenate=True,
            with_cover=True, block=ibv.DEFAULT_BLOCK_TARGET, compress=True):
    """Întoarce (octeții ibv, rândurile raportului)."""
    reader = reader_for(src)
    if reader is None:
        raise SystemExit(f"{src}: unknown format (expected .epub, .fb2, .fb2.zip or .txt)")
    t0 = time.perf_counter()
    data = src.read_bytes()
    b, meta, info = reader.parse(data, src.name)
    cover_bytes = info.pop("cover_bytes", None)
    if lang:
        meta["language"] = lang
    if not meta.get("language"):
        sample = " ".join(it.st.text for it in b.items[:300])
        meta["language"] = txt.guess_language(sample)
        info["language"] = "guessed"
    if title:
        meta["title"] = title
    if author:
        meta["author"] = author
    meta["title"] = meta.get("title") or src.stem
    cov = None
    if with_cover and cover_bytes:
        try:
            cov = cover_mod.load(cover_bytes)
        except Exception as e:          # o copertă stricată nu trebuie să oprească cartea
            info["cover"] = f"unreadable ({e.__class__.__name__})"
    book, rep = doc.finish(b, meta, hyphenate=hyphenate, cover=cov)
    meta = book.meta
    out = ibv.write(book, block_target=block, compress=compress)
    p = ibv.validate(out)
    dt = time.perf_counter() - t0

    lines = [f"{src.name}: {reader.__name__.upper()} -> {len(out):,} B "
             f"({len(out) / max(len(data), 1):.0%} of the input) in {dt:.2f} s"]
    lines.append(f"  {meta['title']!r}" + (f" by {meta['author']}" if meta.get("author") else "")
                 + f"  [{meta['language']}]  book_id {p.book_id.hex()}")
    lines.append(f"  {rep.paragraphs} paragraphs, {len(p.blocks)} blocks, {len(book.toc)} TOC entries, "
                 f"{rep.hyphenated_words:,} words hyphenated ({rep.hyphenation}), "
                 f"cover {f'{cov.width}x{cov.height}' if cov else 'none'}, "
                 f"{rep.chapter_pages} chapter pages (heading levels {rep.chapter_levels or 'none'})")
    extra = {k: v for k, v in info.items() if v not in (None, "")}
    if extra:
        lines.append("  " + ", ".join(f"{k} {v}" for k, v in extra.items()))
    if rep.replaced:
        lines.append("  replaced: " + ", ".join(f"U+{ord(c):04X} {c!r}×{n}" for c, n in sorted(rep.replaced.items())))
    if rep.missing:
        lines.append("  MISSING from the font: " + ", ".join(
            f"U+{ord(c):04X} {c!r}×{n}" for c, n in sorted(rep.missing.items(), key=lambda kv: -kv[1])[:20]))
    for w in rep.warnings[:10]:
        lines.append(f"  warning: {w}")
    if len(rep.warnings) > 10:
        lines.append(f"  ... and {len(rep.warnings) - 10} more warnings")
    return out, lines


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="convert.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("convert", help="convert books to .ibv")
    c.add_argument("inputs", nargs="+", type=pathlib.Path)
    c.add_argument("-o", "--out", type=pathlib.Path,
                   help="output file (one input) or directory (default: next to the input)")
    c.add_argument("--lang", help="override the book's language (BCP 47, e.g. ro, ru, en)")
    c.add_argument("--title")
    c.add_argument("--author")
    c.add_argument("--no-hyphenate", action="store_true")
    c.add_argument("--no-cover", action="store_true")
    c.add_argument("--block", type=int, default=ibv.DEFAULT_BLOCK_TARGET, help="block target size in bytes")
    c.add_argument("--stored", action="store_true", help="don't compress (for reading the hex)")
    v = sub.add_parser("validate", help="check .ibv files")
    v.add_argument("files", nargs="+", type=pathlib.Path)
    d = sub.add_parser("dump", help="print a .ibv file's structure")
    d.add_argument("file", type=pathlib.Path)
    d.add_argument("--records", type=int, default=40)
    pc = sub.add_parser("preview-cover", help="write a .ibv file's cover as PNG")
    pc.add_argument("file", type=pathlib.Path)
    pc.add_argument("png", type=pathlib.Path)
    a = ap.parse_args(argv)

    if a.cmd == "convert":
        if len(a.inputs) > 1 and a.out and a.out.suffix == ".ibv":
            ap.error("-o must be a directory when converting several books")
        failed, written = 0, set()
        for src in a.inputs:
            if a.out and (a.out.is_dir() or len(a.inputs) > 1):
                a.out.mkdir(parents=True, exist_ok=True)
                dst = a.out / (_stem(src) + ".ibv")
            else:
                dst = a.out or src.with_name(_stem(src) + ".ibv")
            if dst in written:          # book.epub și book.txt în același lot
                dst = dst.with_name(f"{_stem(src)}-{src.name[len(_stem(src)) + 1:].replace('.', '-')}.ibv")
            written.add(dst)
            try:
                out, lines = convert(src, lang=a.lang, title=a.title, author=a.author,
                                     hyphenate=not a.no_hyphenate, with_cover=not a.no_cover,
                                     block=a.block, compress=not a.stored)
            except Exception as e:             # o carte stricată nu trebuie să oprească un lot
                print(f"{src.name}: FAILED: {e.__class__.__name__}: {e}", file=sys.stderr)
                failed += 1
                continue
            dst.write_bytes(out)
            print("\n".join(lines))
            print(f"  -> {dst}")
        return 1 if failed else 0
    if a.cmd == "validate":
        bad = 0
        for f in a.files:
            try:
                p = ibv.validate(f.read_bytes())
                print(f"{f}: OK ({p.paragraph_count} paragraphs, {len(p.blocks)} blocks)")
            except ibv.FormatError as e:
                print(f"{f}: INVALID: {e}")
                bad += 1
        return 1 if bad else 0
    if a.cmd == "dump":
        print(ibv.dump(ibv.parse(a.file.read_bytes()), a.records))
        return 0
    if a.cmd == "preview-cover":
        p = ibv.parse(a.file.read_bytes())
        if not p.cover:
            print(f"{a.file}: no cover", file=sys.stderr)
            return 1
        cover_mod.to_image(p.cover).save(a.png)
        return 0
    return 2


def _stem(p: pathlib.Path) -> str:
    name = p.name
    for suf in (".fb2.zip", ".epub", ".fb2", ".txt", ".text"):
        if name.lower().endswith(suf):
            return name[: -len(suf)]
    return p.stem


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
