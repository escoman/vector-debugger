#!/bin/bash
mkdir -p linux-arm32
rm -f linux-arm32/v06x_libretro.so
make platform=linux-portable CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ V=1 $*
strip v06x_libretro.so
cp v06x_libretro.so linux-arm32/
