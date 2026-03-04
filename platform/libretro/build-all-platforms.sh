#!/bin/bash

set -e

rm -f linux-aarch64/*.so
rm -f linux-arm32/*.so
rm -f linux-x86_64/*.so
rm -f win64/*.so

make clean && ./mk.sh -j  && strip v06x_libretro.dll && cp v06x_libretro.dll win64/
make clean && podman run --rm -it -v $PWD/../..:/work:Z -w /work/platform/libretro libretro-builder ./mk-arm32.sh -j
make clean && podman run --rm -it -v $PWD/../..:/work:Z -w /work/platform/libretro libretro-builder ./mk-aarch64.sh -j
make clean && make -j && mv v06x_libretro.so linux-x86_64/

zip -u v06x_libretro.zip

cat BANNER.txt >.zipcomment.txt
echo " v06x-libretro built on $(date -R) git: $(git rev-parse --short HEAD)" >>.zipcomment.txt
echo "" >>.zipcomment.txt
zip -z v06x_libretro.zip <.zipcomment.txt
rm -f .zipcomment.txt

unzip -l v06x_libretro.zip

