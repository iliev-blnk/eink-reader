#!/bin/sh
# Construiește emulatorul: QEMU-ul Espressif la o versiune fixă, plus mașina
# „ibv-reader” din emu/qemu/.
#
#   emu/build.sh
#
# Sursele QEMU ajung în emu/build/qemu (ignorat de git); alt loc se dă prin
# IBV_QEMU_SRC, de exemplu un QEMU deja descărcat. La prima rulare se descarcă
# doar versiunea fixată, nu toată istoria. Rulările următoare recompilează doar
# ce s-a schimbat în emu/qemu/hw-ibv.
#
# Are nevoie de: git, ninja, meson, python3, gcc, glib, pixman, libgcrypt.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=${IBV_QEMU_SRC:-$here/build/qemu}
url=https://github.com/espressif/qemu.git
commit=7411393a2f61e6f0b05af00276d30780769e0839     # ramura esp-develop, QEMU 9.2.2

if [ ! -d "$src/.git" ]; then
    mkdir -p "$src"
    git -C "$src" init -q
    git -C "$src" remote add origin "$url"
    git -C "$src" fetch -q --depth 1 origin "$commit"
    git -C "$src" checkout -q FETCH_HEAD
fi
if [ "$(git -C "$src" rev-parse HEAD)" != "$commit" ]; then
    echo "$src nu e la versiunea $commit" >&2
    exit 1
fi

# Legătura cu restul QEMU-ului, cele două reparații din modelul cardului SD și
# ceasurile din somnul ușor (temporizatorul sistemului stă, RC lent la 136 kHz).
if git -C "$src" apply --reverse --check "$here/qemu/integrare.patch" 2>/dev/null; then
    :
else
    # Un arbore cu o versiune mai veche a patch-ului se readuce întâi la sursa curată.
    git -C "$src" apply --check "$here/qemu/integrare.patch" 2>/dev/null || git -C "$src" checkout -q -- .
    git -C "$src" apply "$here/qemu/integrare.patch"
fi
mkdir -p "$src/hw/ibv"
for f in "$here"/qemu/hw-ibv/*; do
    cmp -s "$f" "$src/hw/ibv/$(basename "$f")" || cp "$f" "$src/hw/ibv/"
done

if [ ! -f "$src/build/build.ninja" ]; then
    mkdir -p "$src/build"
    (cd "$src/build" && ../configure --target-list=xtensa-softmmu --without-default-features \
        --enable-tcg --enable-gcrypt --enable-fdt=internal --enable-pixman \
        --disable-slirp --disable-strip --disable-user --disable-capstone --disable-vnc \
        --disable-gtk --disable-sdl --disable-docs --disable-werror --disable-tools \
        --disable-guest-agent)
fi

# Compilarea cere câțiva GB de memorie; cu systemd se pune o limită, ca să nu
# tragă tot sistemul după ea.
jobs=${IBV_JOBS:-6}
if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope -q true 2>/dev/null; then
    (cd "$src/build" && systemd-run --user --scope -q -p MemoryMax=6G ninja -j"$jobs" qemu-system-xtensa)
else
    (cd "$src/build" && ninja -j"$jobs" qemu-system-xtensa)
fi

mkdir -p "$here/build"
ln -sf "$src/build/qemu-system-xtensa" "$here/build/qemu-system-xtensa"
echo "gata: $here/build/qemu-system-xtensa"
