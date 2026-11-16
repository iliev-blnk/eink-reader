#!/usr/bin/env python3
"""Scoate fișierele dintr-o imagine de card SD, după o rulare în emulator.

    emu/rdcard.py CARD.img DOSAR [CALE_PE_CARD]

Citește ce a lăsat firmware-ul pe card (jurnale de măsurători, poziții, semne
de carte, indexuri de pagini) fără root și fără montare. Cu CALE_PE_CARD se
scoate doar acel fișier sau dosar, de exemplu `perf`. Înțelege tabela MBR sau
un card fără tabelă, FAT32 și numele lungi: atât cât scrie emu/mkcard.py și
biblioteca FatFs din firmware.
"""
import argparse
import os
import struct
import sys

SECTOR = 512


class Card:
    def __init__(self, path):
        self.f = open(path, "rb")
        first = self.read(0, 1)
        if first[510:512] != b"\x55\xaa":
            sys.exit("nu e o imagine de card: lipsește semnătura din primul sector")
        self.start = 0
        if first[82:87] != b"FAT32":
            kind, self.start = first[450], struct.unpack_from("<I", first, 454)[0]
            if kind not in (0x0B, 0x0C):
                sys.exit("prima partiție nu e FAT32 (tip 0x%02x)" % kind)
        boot = self.read(self.start, 1)
        if boot[82:87] != b"FAT32" or struct.unpack_from("<H", boot, 11)[0] != SECTOR:
            sys.exit("partiția nu e FAT32 cu sectoare de 512 octeți")
        self.spc = boot[13]
        reserved, fats = struct.unpack_from("<HB", boot, 14)
        fat_sectors, = struct.unpack_from("<I", boot, 36)
        self.root, = struct.unpack_from("<I", boot, 44)
        self.fat_at = self.start + reserved
        self.data_at = self.fat_at + fats * fat_sectors
        self.fat_entries = fat_sectors * SECTOR // 4

    def read(self, sector, count):
        self.f.seek(sector * SECTOR)
        return self.f.read(count * SECTOR)

    def chain(self, cluster):
        """Datele unui lanț de clustere."""
        out, seen = [], set()
        while 2 <= cluster < 0x0FFFFFF8:
            if cluster in seen or cluster >= self.fat_entries:
                sys.exit("lanț de clustere stricat la %d" % cluster)
            seen.add(cluster)
            out.append(self.read(self.data_at + (cluster - 2) * self.spc, self.spc))
            self.f.seek(self.fat_at * SECTOR + cluster * 4)
            cluster = struct.unpack("<I", self.f.read(4))[0] & 0x0FFFFFFF
        return b"".join(out)

    def listdir(self, cluster):
        """Intrările unui dosar: (nume, e_dosar, cluster, mărime)."""
        data, long_parts, out = self.chain(cluster), [], []
        for at in range(0, len(data), 32):
            e = data[at:at + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                long_parts = []
                continue
            if e[11] == 0x0F:
                long_parts.insert(0, e[1:11] + e[14:26] + e[28:32])
                continue
            if e[11] & 0x08:        # eticheta volumului
                long_parts = []
                continue
            if long_parts:
                name = b"".join(long_parts).decode("utf-16-le").split("\0")[0]
            else:
                base, ext = e[0:8].decode("cp437").rstrip(), e[8:11].decode("cp437").rstrip()
                # Biții 3 și 4: numele scurt, respectiv extensia, sunt cu litere mici.
                if e[12] & 0x08:
                    base = base.lower()
                if e[12] & 0x10:
                    ext = ext.lower()
                name = base + ("." + ext if ext else "")
            long_parts = []
            if name in (".", ".."):
                continue
            first = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
            out.append((name, bool(e[11] & 0x10), first, struct.unpack_from("<I", e, 28)[0]))
        return out

    def extract(self, cluster, dest):
        os.makedirs(dest, exist_ok=True)
        count = 0
        for name, is_dir, first, size in self.listdir(cluster):
            target = os.path.join(dest, name)
            if is_dir:
                count += self.extract(first, target)
            else:
                with open(target, "wb") as fh:
                    fh.write(self.chain(first)[:size] if size else b"")
                count += 1
        return count

    def find(self, path):
        """Intrarea de la o cale de pe card, fără deosebire între litere mari și mici."""
        cluster, entry = self.root, None
        for part in [p for p in path.split("/") if p]:
            if entry and not entry[1]:
                return None
            entry = next((e for e in self.listdir(cluster) if e[0].lower() == part.lower()), None)
            if not entry:
                return None
            cluster = entry[2]
        return entry


def main():
    ap = argparse.ArgumentParser(description="Scoate fișierele dintr-o imagine de card SD (FAT32).")
    ap.add_argument("image")
    ap.add_argument("dest")
    ap.add_argument("path", nargs="?", default="", help="doar acest fișier sau dosar de pe card")
    a = ap.parse_args()
    card = Card(a.image)
    if not a.path.strip("/"):
        count = card.extract(card.root, a.dest)
    else:
        entry = card.find(a.path)
        if not entry:
            sys.exit("nu există pe card: " + a.path)
        name, is_dir, first, size = entry
        if is_dir:
            count = card.extract(first, os.path.join(a.dest, name))
        else:
            os.makedirs(a.dest, exist_ok=True)
            with open(os.path.join(a.dest, name), "wb") as fh:
                fh.write(card.chain(first)[:size] if size else b"")
            count = 1
    print("%s: %d fișiere scoase în %s" % (a.image, count, a.dest))


if __name__ == "__main__":
    main()
