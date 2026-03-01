Because ArkOS uses older glibc version, it's easier to build in a container.

Build a podman container:

  podman build -t libretro-builder .

Build for aarch64:

  podman run --rm -it -v $PWD/../..:/work:Z -w /work/platform/libretro libretro-builder ./mk-aarch64.sh
