#!/usr/bin/env bash

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

apt-get update -y -q \
&& apt-get install -y -q --no-install-recommends \
    libglib2.0-0 \
    libpixman-1-0 \
    libsdl2-2.0-0 \
    libslirp0 \
&& :

# w/ SDL2  20 MB
# w/o SDL2  5 MB
