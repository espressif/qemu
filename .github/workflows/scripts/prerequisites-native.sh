#!/usr/bin/env bash

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

# Debian 11 moved to archive.debian.org when its support ended.
sed -i -e '/bullseye-updates/d' -e 's|deb.debian.org|archive.debian.org|' /etc/apt/sources.list
echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99archive

apt-get update -y -q
apt-get install -y -q --no-install-recommends \
    build-essential \
    git \
    libgcrypt-dev \
    libglib2.0-dev \
    libpixman-1-dev \
    libsdl2-dev \
    libslirp-dev \
    ninja-build \
    python3-pip \
    wget \
    zlib1g-dev \
&& :

# Even though ./configure installs meson, just specify a version
/usr/bin/pip3 install meson==1.7.0 tomli==2.2.1
