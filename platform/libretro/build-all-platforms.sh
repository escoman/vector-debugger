#!/bin/bash

set -e

rm -f linux-aarch64/*.so
rm -f linux-arm32/*.so
rm -f linux-x86_64/*.so
rm -f win64/*.so

make clean && ./mk.sh -j  && cp v06x_libretro.dll win64/
make clean && podman run --rm -it -v $PWD/../..:/work:Z -w /work/platform/libretro libretro-builder ./mk-arm32.sh -j
make clean && podman run --rm -it -v $PWD/../..:/work:Z -w /work/platform/libretro libretro-builder ./mk-aarch64.sh -j
make clean && make -j && mv v06x_libretro.so linux-x86_64/*



