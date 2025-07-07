#!/bin/bash
mkdir -p linux-aarch64
rm -f linux-aarch64/v06x_libretro.so
make platform=linux-portable-aarch64 CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ V=1 $*
cp v06x_libretro.so linux-aarch64
aarch64-linux-gnu-strip linux-aarch64/*.so
