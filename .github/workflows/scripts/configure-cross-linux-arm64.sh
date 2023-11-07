#!/usr/bin/env bash

set -euo pipefail

TARGET=${TARGET:-xtensa-softmmu}
VERSION=${VERSION:-dev}

# Replacing libgcrypt method to 'pkg-config' for crossbuilding on Linux
sed -z -i "s/\(.*dependency('libgcrypt'.*method: '\)config-tool\('.*\)/\1pkg-config\2/g" -- meson.build

echo DBG
./configure --help

./configure \
    --bindir=bin \
    --cross-prefix=aarch64-linux-gnu- \
    --datadir=share/qemu \
    --enable-gcrypt \
    --enable-sdl \
    --enable-slirp \
    --enable-stack-protector \
    --extra-cflags=-Werror \
    --prefix=${PWD}/install/qemu \
    --target-list=${TARGET} \
    --with-pkgversion="${VERSION}" \
    --with-suffix="" \
    --without-default-features \
|| { cat meson-logs/meson-log.txt && false; }
