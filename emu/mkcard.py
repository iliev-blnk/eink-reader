#!/usr/bin/env python3
"""Imaginea unui card SD pentru emulator, dintr-un dosar.

    emu/mkcard.py CARD.img DOSAR [--gib 4]

Cardul iese ca unul cumpărat din magazin: SDHC, tabelă MBR cu o singură
partiție, FAT32 cu nume lungi. Legăturile simbolice din dosar se urmează,
deci merge direct pe un card făcut de sim/make_card.sh. Fișierul e rar
(sparse): pe disc ocupă doar cât datele. Mărimea trebuie să fie putere a lui 2
(o cere QEMU) și peste 2 GiB (altfel cardul n-ar mai fi SDHC).

Nu are nevoie de root, de mtools sau de montare. Datele din antete sunt fixe,
ca două rulări să dea aceeași imagine.
"""
import argparse
import os
import struct
import sys

SECTOR = 512
SPC = 8                      # sectoare pe cluster: 4 KiB
PART_START = 8192            # partiția începe la 4 MiB, ca pe cardurile obișnuite
RESERVED = 32
FATS = 2
DATE = ((2026 - 1980) << 9) | (1 << 5) | 1    # 1 ianuarie 2026
TIME = 12 << 11
EOC = 0x0FFFFFFF
SHORT_OK = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")


def short_name(name, used):
    """Numele 8.3 și dacă e nevoie de intrări pentru numele lung."""
    base, dot, ext = name.rpartition(".")
    if not dot or not base:
        base, ext = name, ""
    plain = (name == name.upper() and 1 <= len(base) <= 8 and len(ext) <= 3
             and set(base) <= SHORT_OK and set(ext) <= SHORT_OK)
    if plain:
        short = base.ljust(8) + ext.ljust(3)
        used.add(short)
        return short.encode("ascii"), False
    clean = lambda s: "".join(c if c in SHORT_OK else "_" for c in s.upper() if c not in " .")
    b, e = clean(base) or "_", clean(ext)[:3]
    for n in range(1, 1000000):
        tail = "~%d" % n
        short = (b[:8 - len(tail)] + tail).ljust(8) + e.ljust(3)
        if short not in used:
            used.add(short)
            return short.encode("ascii"), True
    raise SystemExit("prea multe nume asemănătoare: " + name)


def dir_entries(name, attr, cluster, size, used):
    short, need_long = short_name(name, used)
    out = b""
    if need_long:
        units = name.encode("utf-16-le")
        units = [units[i:i + 2] for i in range(0, len(units), 2)]
        if len(units) > 255:
            raise SystemExit("nume prea lung: " + name)
        if len(units) % 13:
            units.append(b"\0\0")
            units += [b"\xff\xff"] * (-len(units) % 13)
        csum = 0
        for c in short:
            csum = ((csum >> 1) | ((csum & 1) << 7)) + c & 0xFF
        count = len(units) // 13
        for seq in range(count, 0, -1):
            u = b"".join(units[(seq - 1) * 13:seq * 13])
            out += struct.pack("<B10sBBB12sH4s", seq | (0x40 if seq == count else 0), u[:10], 0x0F, 0,
                               csum, u[10:22], 0, u[22:26])
    out += struct.pack("<11sBBBHHHHHHHI", short, attr, 0, 0, TIME, DATE, DATE, cluster >> 16, TIME, DATE,
                       cluster & 0xFFFF, size)
    return out


class Card:
    def __init__(self, path, gib):
        self.total = gib * (1 << 30) // SECTOR
        self.part = self.total - PART_START
        # Mărimea unei tabele FAT, după formula din specificația Microsoft.
        self.fat_sectors = -(-(self.part - RESERVED) // ((256 * SPC + FATS) // 2))
        self.data_start = RESERVED + FATS * self.fat_sectors
        self.clusters = (self.part - self.data_start) // SPC
        self.fat = [0x0FFFFFF8, EOC]
        self.f = open(path, "wb")
        self.f.truncate(self.total * SECTOR)

    def write(self, sector, data):
        self.f.seek((PART_START + sector) * SECTOR)
        self.f.write(data)

    def store(self, data):
        """Scrie datele într-un lanț nou de clustere și întoarce primul."""
        count = max(1, -(-len(data) // (SPC * SECTOR)))
        first = len(self.fat)
        if first + count - 2 > self.clusters:
            raise SystemExit("nu încape pe card")
        self.fat += list(range(first + 1, first + count)) + [EOC]
        self.write(self.data_start + (first - 2) * SPC, data)
        return first

    def add_dir(self, src, parent):
        """Pune dosarul `src` pe card și întoarce clusterul lui. parent None = rădăcina."""
        used = set()
        body = b""
        me = len(self.fat)       # un dosar își știe clusterul înaintea copiilor: îl rezervă întâi
        self.fat.append(EOC)
        for name in sorted(os.listdir(src)):
            p = os.path.join(src, name)
            if os.path.isdir(p):
                body += dir_entries(name, 0x10, self.add_dir(p, me), 0, used)
            elif os.path.isfile(p):
                with open(p, "rb") as fh:
                    data = fh.read()
                body += dir_entries(name, 0x20, self.store(data) if data else 0, len(data), used)
        if parent is not None:
            dots = struct.pack("<11sBBBHHHHHHHI", b".".ljust(11), 0x10, 0, 0, TIME, DATE, DATE, me >> 16, TIME,
                               DATE, me & 0xFFFF, 0)
            up = 0 if parent == 2 else parent
            dots += struct.pack("<11sBBBHHHHHHHI", b"..".ljust(11), 0x10, 0, 0, TIME, DATE, DATE, up >> 16, TIME,
                                DATE, up & 0xFFFF, 0)
            body = dots + body
        # Primul cluster e cel rezervat; restul dosarului, dacă e mare, vine într-un lanț legat de el.
        csize = SPC * SECTOR
        self.write(self.data_start + (me - 2) * SPC, body[:csize])
        if len(body) > csize:
            self.fat[me] = self.store(body[csize:])
        return me

    def finish(self, label):
        used = len(self.fat) - 2
        boot = bytearray(SECTOR)
        struct.pack_into("<3s8sHBHBHHBHHHII", boot, 0, b"\xeb\x58\x90", b"MSDOS5.0", SECTOR, SPC, RESERVED, FATS,
                         0, 0, 0xF8, 0, 63, 255, PART_START, self.part)
        struct.pack_into("<IHHIHH12sBBBI11s8s", boot, 36, self.fat_sectors, 0, 0, 2, 1, 6, b"", 0x80, 0, 0x29,
                         0x1B5EAD00, label.ljust(11).encode("ascii"), b"FAT32   ")
        boot[510:512] = b"\x55\xaa"
        info = bytearray(SECTOR)
        struct.pack_into("<I", info, 0, 0x41615252)
        struct.pack_into("<III", info, 484, 0x61417272, self.clusters - used, len(self.fat))
        info[510:512] = b"\x55\xaa"
        for at in (0, 6):
            self.write(at, boot)
            self.write(at + 1, info)
        table = struct.pack("<%dI" % len(self.fat), *self.fat)
        for i in range(FATS):
            self.write(RESERVED + i * self.fat_sectors, table)
        mbr = bytearray(SECTOR)
        struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\xfe\xff\xff", 0x0C, b"\xfe\xff\xff", PART_START, self.part)
        mbr[510:512] = b"\x55\xaa"
        self.f.seek(0)
        self.f.write(mbr)
        self.f.close()
        return used


def main():
    ap = argparse.ArgumentParser(description="Imaginea unui card SD (SDHC, FAT32) dintr-un dosar.")
    ap.add_argument("image")
    ap.add_argument("dir")
    ap.add_argument("--gib", type=int, default=4, help="mărimea cardului în GiB, putere a lui 2 (implicit 4)")
    a = ap.parse_args()
    if a.gib < 4 or a.gib & (a.gib - 1) or a.gib > 32:
        sys.exit("mărimea trebuie să fie 4, 8, 16 sau 32 GiB")
    card = Card(a.image, a.gib)
    card.add_dir(a.dir, None)
    used = card.finish("NO NAME")
    print("%s: %d GiB, %d KiB ocupați" % (a.image, a.gib, used * SPC * SECTOR // 1024))


if __name__ == "__main__":
    main()
