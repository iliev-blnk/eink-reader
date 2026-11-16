#!/bin/sh
# Testul emulatorului: aceleași scenarii rulate de simulatorul de pe PC și de
# firmware-ul adevărat pe placa emulată, comparate cadru cu cadru.
#
#   emu/test.sh [DOSAR_DE_LUCRU]      implicit emu/build/test, refăcut de la zero
#
# Simulatorul rulează nucleul portabil cu un panou și un card închipuite.
# Emulatorul rulează binarul întreg: driverul panoului, SPI, FatFs peste
# cardul SD, butoanele cu întreruperi, somnul adânc și trezirea. Dacă cele două
# dau aceleași cadre, tot ce e sub nucleul portabil și-a făcut treaba.
#
# Are nevoie de firmware construit (firmware/build) și de emu/build.sh rulat.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/..
sim=$root/sim/build/sim
work=${1:-$here/build/test}
[ -d "$root/sim/build" ] || cmake -S "$root/sim" -B "$root/sim/build" >/dev/null
cmake --build "$root/sim/build" >/dev/null
rm -rf "$work"
mkdir -p "$work"
fail=0

# Un scenariu `sim ui` și același scenariu în emulator, pe un card cu layout.ibv.
both() {
    name=$1 keys=$2
    mkdir -p "$work/$name-card/perf" "$work/$name-sim"
    [ -d "$work/$name-card/books" ] || cp "$root/sim/test/layout.ibv" "$work/$name-card/"
    # Cardul emulatorului se face înaintea rulării simulatorului, care scrie și el pe card.
    python3 "$here/mkcard.py" "$work/$name-card.img" "$work/$name-card" >/dev/null
    "$sim" ui "$work/$name-card" -o "$work/$name-sim" --idle --keys "$keys" >/dev/null
    if ! "$here/run.sh" "$work/$name-card.img" -o "$work/$name-emu" --keys "$keys" 2>"$work/$name-qemu.txt"; then
        echo "EȘUAT $name: emulatorul s-a oprit cu eroare, vezi $work/$name-qemu.txt"
        fail=1
        return
    fi
    if python3 "$here/compare.py" "$work/$name-emu" "$work/$name-sim" --diff "$work/$name-diff" >"$work/$name.txt"; then
        echo "ok    $name: $(tail -n 1 "$work/$name.txt")"
    else
        echo "EȘUAT $name:"
        sed 's/^/  /' "$work/$name.txt"
        fail=1
    fi
}

# Scenariile din sim/test/run.sh (acolo e scris ce face fiecare).
both ui "m m n n m n n m M n m n n n m m p M n"
both auto "m m m n n n n n n m P t:60 n n b:3350 t:200"

# Un card ca la cititor: cărțile corpusului și fonturile dispozitivului. Se
# deschide Moby Dick (indexul de pagini se construiește pe cipul emulat), se
# citește, se adoarme și se trezește în carte, apoi meniul cărții. Se sare
# peste el dacă lipsesc corpusul sau fonturile.
if "$root/sim/make_card.sh" "$work/carte-card" >/dev/null 2>&1; then
    both carte "m n n n n m n n n n n n n n p p M n n n P n n m n n n n P p p p p p p m n n n M n n"
else
    echo "sărit carte: lipsesc corpusul (corpus/out) sau fonturile (tools/fonttool/out/fam)"
fi

# Jurnalul de măsurători scris de firmware pe cardul emulat, prin FatFs și
# driverul SD adevărat, trebuie să-l citească tools/perf/perf.py la fel ca pe
# al simulatorului.
if [ -f "$work/auto-emu/card.img" ]; then
    if python3 "$here/rdcard.py" "$work/auto-emu/card.img" "$work/auto-out" perf >/dev/null 2>"$work/perf.txt" \
        && python3 "$root/tools/perf/perf.py" "$work"/auto-out/perf/*.csv >"$work/perf.txt" 2>&1 \
        && grep -q "toate: 13 (auto 12, ÎNAINTE 1)" "$work/perf.txt" \
        && grep -q "oprit, baterie descărcată" "$work/perf.txt"; then
        echo "ok    perf: jurnalul de pe cardul emulat citit de perf.py"
    else
        echo "EȘUAT perf:"; sed 's/^/  /' "$work/perf.txt"; fail=1
    fi
fi

# Aplicația de punere în funcțiune (MENIU ținut la pornire): numărătoarea cu
# reîmprospătări parțiale, redesenarea, somnul adânc și trezirea din buton.
# Simulatorul n-o are, deci aici se verifică doar că merge până la capăt fără
# avertismente și cu numărul așteptat de cadre.
mkdir -p "$work/bringup-card"
if "$here/run.sh" "$work/bringup-card" -o "$work/bringup-emu" --hold m --keys "n n N p m M p n" \
        2>"$work/bringup-qemu.txt" \
    && grep -q "gata: 9 cadre, 0 avertismente" "$work/bringup-emu/jurnal.txt" \
    && grep -q "trezire ÎNAPOI · nr. 2" "$work/bringup-emu/uart.log"; then
    echo "ok    bringup: 9 cadre, somn și trezire din buton"
else
    echo "EȘUAT bringup: vezi $work/bringup-emu/jurnal.txt și uart.log"
    fail=1
fi

# Aceeași rulare a doua oară trebuie să dea aceleași evenimente în aceeași
# ordine. Timpii nu se compară: cele două nuclee sunt emulate pe rând, iar
# locul în care QEMU trece de la unul la altul diferă puțin între rulări, deci
# o pornire poate ieși cu câteva milisecunde mai lungă sau mai scurtă.
events() { sed 's/^ *[0-9.]* *//; s/ după [0-9]* ms//' "$1"; }
if "$here/run.sh" "$work/ui-card.img" -o "$work/ui-emu2" --keys "m m n n m n n m M n m n n n m m p M n" \
        2>/dev/null \
    && events "$work/ui-emu/jurnal.txt" >"$work/ev1.txt" && events "$work/ui-emu2/jurnal.txt" >"$work/ev2.txt" \
    && cmp -s "$work/ev1.txt" "$work/ev2.txt" \
    && python3 "$here/compare.py" "$work/ui-emu2" "$work/ui-sim" >/dev/null; then
    echo "ok    repetare: a doua rulare, aceleași evenimente și aceleași cadre"
else
    echo "EȘUAT repetare: două rulări ale aceluiași scenariu diferă ($work/ui-emu, $work/ui-emu2)"
    fail=1
fi
exit $fail
