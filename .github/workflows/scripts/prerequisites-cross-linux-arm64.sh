#!/usr/bin/env bash

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

# Debian 11 moved to archive.debian.org when its support ended.
sed -i -e '/bullseye-updates/d' -e 's|deb.debian.org|archive.debian.org|' /etc/apt/sources.list
echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99archive

dpkg --add-architecture arm64

apt-get update -y -q
apt-get install -y -q --no-install-recommends \
    binutils-aarch64-linux-gnu \
    build-essential \
    crossbuild-essential-arm64 \
    gcc-aarch64-linux-gnu \
    git \
    libgcrypt20-dev:arm64 \
    libglib2.0-dev:arm64 \
    libgpg-error-dev:arm64 \
    libpixman-1-dev:arm64 \
    libsdl2-dev:arm64 \
    libslirp-dev:arm64 \
    ninja-build \
    python3-pip \
    zlib1g-dev:arm64 \
&& :

/usr/bin/pip3 install meson==1.7.0 tomli==2.2.1
