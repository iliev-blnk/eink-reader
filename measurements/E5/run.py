#!/usr/bin/env python3
"""E5, partea de pe PC: mărimile formatului IBV pe variante (vezi METODA.md).

    python3 measurements/E5/run.py

Citește cele 20 de cărți distincte din corpus/files și le convertește în memorie
cu fiecare variantă. Scrie date/carti.csv, date/blocuri.csv, date/timp.csv și
date/versiune.txt. Nu păstrează fișierele .ibv.
"""
import csv
import hashlib
import os
import pathlib
import platform
import statistics
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "converter"))
import convert  # noqa: E402
import ibv      # noqa: E402

FILES = ROOT / "corpus" / "files"
BOOKS = ["libru-dubrovsky.txt", "pg11.epub", "pg1322.epub", "pg1342.epub", "pg14741.epub", "pg16527-8.txt",
         "pg2701.epub", "pg35323.epub", "pg62916.epub", "pg64597.epub", "pg65565.epub", "pg84.epub",
         # v2 (2026-11-18): cărțile adăugate în MEAS-1
         "libru-geroi.fb2.zip", "pg1524.epub", "pg4300.epub", "ws-amintiri.epub", "ws-ion.epub",
         "ws-kapitanskaya.epub", "ws-luceafarul.epub", "ws-moara-cu-noroc.epub"]
VARIANTS = {                     # numele -> argumentele lui convert.convert
    "implicit": {},
    "stocat": {"compress": False},
    "fara_coperta": {"with_cover": False},
    "fara_despartire": {"hyphenate": False},
}
BLOCKS = [4096, 8192, 16384, 32768]
TIMING_RUNS = 3


def measure(data: bytes) -> dict:
    p = ibv.validate(data)
    sec = {k: v[1] for k, v in p.sections.items()}
    small_end = max(off + length for k, (off, length, _c) in p.sections.items() if k not in ("TEXT", "COVR"))
    raw = [b.raw_len for b in p.blocks]
    stored = [b.stored_len for b in p.blocks]
    plain = sum(len(r.text.replace(ibv.SHY, "").encode()) for r in p.records())
    return {
        "fisier": len(data),
        "antet_director": 44 + 16 * len(p.sections),
        "meta": sec.get("META", 0), "toc": sec.get("TOC ", 0), "blks": sec.get("BLKS", 0),
        "covr": sec.get("COVR", 0), "text": sec.get("TEXT", 0),
        "text_brut": sum(raw), "text_simplu": plain, "text_cu_cratime": p.text_bytes,
        "deschidere": small_end,
        "blocuri": len(p.blocks),
        "bloc_stocat_mediana": int(statistics.median(stored)), "bloc_stocat_max": max(stored),
        "bloc_brut_mediana": int(statistics.median(raw)), "bloc_brut_max": max(raw),
    }


def main():
    out = HERE / "date"
    out.mkdir(exist_ok=True)
    rows, block_rows, time_rows = [], [], []
    for name in BOOKS:
        src = FILES / name
        book = name.split(".")[0]
        for var, kw in VARIANTS.items():
            data, _ = convert.convert(src, **kw)
            rows.append({"carte": book, "varianta": var, "sursa": src.stat().st_size, **measure(data)})
        for blk in BLOCKS:
            data, _ = convert.convert(src, block=blk)
            m = measure(data)
            if m["bloc_brut_max"] > ibv.MAX_BLOCK_RAW:
                sys.exit(f"{name}: un bloc brut de {m['bloc_brut_max']} B trece de limita formatului")
            block_rows.append({"carte": book, "bloc_tinta": blk, **m})
        for run in range(1, TIMING_RUNS + 1):
            t0 = time.perf_counter()
            convert.convert(src)
            time_rows.append({"carte": book, "rulare": run, "s": f"{time.perf_counter() - t0:.4f}"})
        print(f"{book} gata", flush=True)

    for fname, data in (("carti.csv", rows), ("blocuri.csv", block_rows), ("timp.csv", time_rows)):
        with open(out / fname, "w", newline="") as f:
            w = csv.DictWriter(f, list(data[0]))
            w.writeheader()
            w.writerows(data)

    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", "HEAD", "--", "tools/converter"]).returncode
    cpu = next((l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name")), "?")
    with open(out / "versiune.txt", "w") as f:
        f.write(f"data {time.strftime('%Y-%m-%d')}\ncommit {commit}{' (cu modificări necomise)' if dirty else ''}\n"
                f"python {platform.python_version()}\nprocesor {cpu}\n# sha256 surse\n")
        for name in BOOKS:
            f.write(f"{hashlib.sha256((FILES / name).read_bytes()).hexdigest()}  {name}\n")


if __name__ == "__main__":
    main()
