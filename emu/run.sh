#!/bin/sh
# Rulează firmware-ul adevărat pe placa emulată.
#
#   emu/run.sh CARD -o DOSAR [--keys "m n n M"] [--hold m] [--battery MV]
#                            [--firmware DOSAR_BUILD] [--realtime] [-- opțiuni QEMU]
#
# CARD e un dosar (se face din el o imagine de card cu emu/mkcard.py) sau o
# imagine gata făcută. Firmware-ul scrie pe card, deci se lucrează mereu pe o
# copie, DOSAR/card.img; originalul rămâne neatins.
#
# În DOSAR ajung: cadrele NNN.pbm (ce s-a văzut pe panou după fiecare
# reîmprospătare), jurnal.txt (ce a făcut placa), uart.log (jurnalul serial al
# firmware-ului) și imaginile de lucru flash.bin, efuse.bin, card.img.
#
# Sintaxa pentru --keys e cea din `sim ui` (vezi emu/README.md). Fără --keys
# emulatorul merge până îl oprești cu Ctrl-C. Implicit timpul e numărat în
# instrucțiuni (rapid și la fel la fiecare rulare); cu --realtime merge după
# ceasul calculatorului.
set -e
here=$(cd "$(dirname "$0")" && pwd)
qemu=${IBV_QEMU:-$here/build/qemu-system-xtensa}
fw=$here/../firmware/build
card= out= keys= hold= battery= realtime=0

while [ $# -gt 0 ]; do
    case $1 in
        -o) out=$2; shift 2 ;;
        --keys) keys=$2; shift 2 ;;
        --hold) hold=$2; shift 2 ;;
        --battery) battery=$2; shift 2 ;;
        --firmware) fw=$2; shift 2 ;;
        --realtime) realtime=1; shift ;;
        --) shift; break ;;
        -*) echo "opțiune necunoscută: $1" >&2; exit 2 ;;
        *) card=$1; shift ;;
    esac
done
if [ -z "$card" ] || [ -z "$out" ]; then
    sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
fi
[ -x "$qemu" ] || { echo "lipsește emulatorul: rulează întâi emu/build.sh" >&2; exit 1; }
for f in bootloader/bootloader.bin partition_table/partition-table.bin eink-reader.bin; do
    [ -f "$fw/$f" ] || { echo "lipsește $fw/$f: construiește întâi firmware-ul (idf.py build)" >&2; exit 1; }
done

rm -rf "$out"
mkdir -p "$out"
out=$(cd "$out" && pwd)

# Memoria flash de 16 MB, așa cum o scrie `idf.py flash`.
python3 - "$out/flash.bin" "$fw" <<'EOF'
import sys
image = bytearray(b"\xff" * (16 << 20))
for at, name in ((0x0, "bootloader/bootloader.bin"), (0x8000, "partition_table/partition-table.bin"),
                 (0x10000, "eink-reader.bin")):
    data = open(sys.argv[2] + "/" + name, "rb").read()
    image[at:at + len(data)] = data
open(sys.argv[1], "wb").write(image)
EOF
# eFuse: gol, doar cu versiunea cipului (v0.3), fără de care bootloaderul refuză imaginea.
python3 -c 'import sys; b = bytearray(1024); b[38] = 0x0c; open(sys.argv[1], "wb").write(b)' "$out/efuse.bin"

if [ -d "$card" ]; then
    python3 "$here/mkcard.py" "$out/card.img" "$card" >/dev/null
else
    cp --sparse=always "$card" "$out/card.img"
fi

# În valorile opțiunii -M virgula se scrie dublată.
esc() { printf %s "$1" | sed 's/,/,,/g'; }
machine="ibv-reader,out=$(esc "$out")"
[ -n "$keys" ] && machine="$machine,keys=$(esc "$keys")"
[ -n "$hold" ] && machine="$machine,hold=$hold"
[ -n "$battery" ] && machine="$machine,battery=$battery"
clock="-icount shift=3,sleep=off"
[ $realtime = 1 ] && clock=

# -m 8M: PSRAM-ul modulului N16R8. Mai mult de atât ocupă tot spațiul de adrese
# de date și firmware-ul nu-și mai poate mapa partițiile.
# shellcheck disable=SC2086
exec "$qemu" -M "$machine" -m 8M \
    -drive "file=$out/flash.bin,if=mtd,format=raw" \
    -drive "file=$out/efuse.bin,if=none,format=raw,id=efuse" \
    -global driver=nvram.esp32s3.efuse,property=drive,value=efuse \
    -global driver=ssi_psram,property=is_octal,value=true \
    -drive "file=$out/card.img,if=none,format=raw,id=ibvsd" \
    -nographic -serial "file:$out/uart.log" -monitor none $clock "$@"
