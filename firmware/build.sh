#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GerbilHamster Records
# Build GHR AMBIENT -> build/ghr_ambient_app.bin (the file the Windows installer takes).
# Needs JieLi's Linux toolchain (clang 4.0.1 for the chip's pi32v2 core), binutils' readelf, sha256sum.
#   mkdir -p ~/.jieli && curl -fL https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C ~/.jieli
#   export JIELI_TOOLCHAIN=$(ls -d ~/.jieli/jieli-linux-toolchains-* | sort | tail -1)
set -e
cd "$(dirname "$0")"
J=${JIELI_TOOLCHAIN:?set JIELI_TOOLCHAIN to the JieLi toolchain folder - see the top of build.sh}
CF="-target pi32v2 -O2 -ffreestanding -fno-builtin -nostdlib -Wall"
mkdir -p build
"$J/pi32v2/bin/clang" $CF -c crt0.S -o build/crt0.o
"$J/pi32v2/bin/clang" $CF -c main.c -o build/main.o
"$J/pi32v2/bin/ld" -T app.ld build/crt0.o build/main.o -o build/fw.elf
# nothing from any library: every symbol must be ours
if readelf -s build/fw.elf | grep -q " UND [a-zA-Z_]"; then echo "undefined symbols"; exit 1; fi
# the bootloader maps only .text (code + constants): .data must stay empty
if [ "$(readelf -S build/fw.elf | awk '$3==".data"{print $7}')" != "000000" ]; then echo ".data is not empty"; exit 1; fi
"$J/common/bin/objcopy" -O binary -j .text build/fw.elf build/ghr_ambient_app.bin
echo "build/ghr_ambient_app.bin: $(wc -c < build/ghr_ambient_app.bin) bytes, sha256 $(sha256sum build/ghr_ambient_app.bin | cut -d' ' -f1)"
