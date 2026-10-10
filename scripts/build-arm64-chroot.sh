#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(dirname "$REPO_ROOT")"
ROOTFS="${1:-$WORKSPACE/arm64/rootfs}"
MIRROR="${MIRROR:-http://deb.debian.org/debian/}"
SUITE="${SUITE:-sid}"

DEPS=(
  build-essential cmake ninja-build python3 pkg-config ca-certificates
  qtbase5-dev qtdeclarative5-dev qtwebengine5-dev libqt5webchannel5-dev
  libqt5x11extras5-dev qttools5-dev libmpv-dev libsdl2-dev libx11-dev
  libxrandr-dev zlib1g-dev libminizip-dev libgl1-mesa-dev
  qml-module-qtquick-controls qml-module-qtwebengine qml-module-qtwebchannel
  qml-module-qtquick-window2
)

if [[ $EUID -ne 0 ]]; then
  echo "error: must run as root (chroot and bind mounts)." >&2
  exit 1
fi

if [[ ! -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]]; then
  echo "error: no qemu-aarch64 binfmt handler registered." >&2
  echo "       apt install qemu-user-static binfmt-support" >&2
  exit 1
fi
if ! grep -q 'flags:.*F' /proc/sys/fs/binfmt_misc/qemu-aarch64; then
  echo "error: the qemu-aarch64 binfmt handler lacks the F (fix-binary) flag," >&2
  echo "       so arm64 binaries will not run inside a chroot." >&2
  exit 1
fi

if [[ ! -d $ROOTFS ]]; then
  echo "==> creating arm64 $SUITE rootfs in $ROOTFS"
  mkdir -p "$(dirname "$ROOTFS")"
  mmdebstrap --architectures=arm64 --variant=minbase \
    --include="$(IFS=,; echo "${DEPS[*]}")" \
    "$SUITE" "$ROOTFS" "$MIRROR"
else
  echo "==> reusing existing rootfs $ROOTFS"
fi

ARCH_IN_ROOTFS=$(chroot "$ROOTFS" dpkg --print-architecture)
if [[ $ARCH_IN_ROOTFS != arm64 ]]; then
  echo "error: rootfs reports architecture '$ARCH_IN_ROOTFS', expected arm64." >&2
  exit 1
fi
echo "==> rootfs architecture: $ARCH_IN_ROOTFS"
chroot "$ROOTFS" /bin/sh -c 'gcc -dumpmachine'

mkdir -p "$ROOTFS/src" "$ROOTFS/web-client" "$ROOTFS/build"
cleanup() {
  umount -l "$ROOTFS/src" "$ROOTFS/web-client" 2>/dev/null || true
  umount -l "$ROOTFS/proc" "$ROOTFS/sys" "$ROOTFS/dev/pts" 2>/dev/null || true
}
trap cleanup EXIT

mount --bind "$REPO_ROOT" "$ROOTFS/src"
mount -o remount,ro,bind "$ROOTFS/src"
mount --bind "$WORKSPACE/vendor/web-client" "$ROOTFS/web-client"
mount -o remount,ro,bind "$ROOTFS/web-client"
mount -t proc proc "$ROOTFS/proc"
mount -t sysfs sys "$ROOTFS/sys"
mount -t devpts devpts "$ROOTFS/dev/pts" 2>/dev/null || true

echo "==> configuring"
chroot "$ROOTFS" /bin/bash -c '
  set -euo pipefail
  cd /build
  cmake -GNinja -DQTROOT=/usr -DCMAKE_BUILD_TYPE=Release \
        -DWEB_CLIENT_LOCAL_DIR=/web-client /src
'
echo "==> building (qemu-emulated; this takes a while)"
chroot "$ROOTFS" /bin/bash -c 'ninja -C /build'

echo "==> result"
chroot "$ROOTFS" /bin/bash -c '
  file /build/src/plexmediaplayer
  /build/src/plexmediaplayer --help 2>&1 | head -5 || true
'
echo
echo "Binary: $ROOTFS/build/src/plexmediaplayer"
