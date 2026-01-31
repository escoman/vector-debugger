#!/bin/bash

# Configuration
IMAGE="ubuntu:20.04"
ARCH="arm64"
SRC_DIR=$(pwd)
BUILD_SCRIPT="platform/libretro/mk-aarch64.sh"
CORE_NAME="v06x_libretro.so"

echo "--- Initializing Multi-Arch Support ---"
sudo podman run --rm --privileged docker.io/multiarch/qemu-user-static --reset -p yes

echo "--- Starting Unattended Build for ArkOS (Glibc 2.31) ---"

podman run --rm \
    --arch=$ARCH \
    -v "$SRC_DIR":/work:Z \
    -w /work \
    $IMAGE \
    /bin/bash -c "
        apt update && \
        apt install -y build-essential && \
        cd platform/libretro && \
        chmod +x mk-aarch64.sh && \
        ./mk-aarch64.sh && \
        echo '--- Build Complete ---'
    "

# Verification
if [ -f "platform/libretro/$CORE_NAME" ]; then
    echo "SUCCESS: $CORE_NAME generated."
    echo "Linking check:"
    objdump -p "platform/libretro/$CORE_NAME" | grep GLIBC
else
    echo "ERROR: Build failed. Core not found."
    exit 1
fi
