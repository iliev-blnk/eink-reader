#!/bin/sh
# Compilează fuzzerele (libFuzzer + ASan + UBSan) în build/.
# Implicit pe 32 de biți (-m32), ca pe ESP32-S3: size_t, long și pointerii au 4
# octeți, deci depășirile din calculele de mărimi apar ca pe dispozitiv.
# FUZZ_M32= ./build.sh compilează pe 64 de biți.
set -eu
cd "$(dirname "$0")"
FW=../../firmware/components
CORE=$FW/core
UI=$FW/ui
M32=${FUZZ_M32--m32}
mkdir -p build
CFLAGS="$M32 -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
        -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -include memfile.h -I. -I$CORE/include -I$UI/include"
SRC="memfile.c ../../sim/inflate_zlib.c $CORE/crc32.c $CORE/font.c $CORE/gfx.c $CORE/ibv.c $CORE/layout.c
     $CORE/fontsel.c $CORE/pageidx.c $CORE/fonts/font_ui16.c $CORE/fonts/font_ui12.c $UI/sidecar.c $UI/settings.c"
for t in ibv font state; do
    # shellcheck disable=SC2086
    clang $CFLAGS -o build/fuzz_$t fuzz_$t.c $SRC -lz
    echo "build/fuzz_$t"
done
# Verificarea exhaustivă a decodorului UTF-8 nu e fuzzer: compilată normal, pe 64 de biți.
clang -O2 -I$CORE/include -o build/utf8_toate utf8_toate.c $CORE/font.c $CORE/crc32.c
echo build/utf8_toate
python3 -I seeds.py
