#!/usr/bin/env bash

set -euo pipefail

brew install \
  glib \
  libgcrypt \
  libslirp \
  ninja \
  pixman \
  pkg-config \
  sdl2 \
&& :

# workaround if deprecated module 'distutils.version' is missing
# https://peps.python.org/pep-0632/#migration-advice
# https://pypi.org/project/looseversion/
#
# Used for building only, not for qemu run-time
#
# Also if something goes wrong, 'setup-python' action can be used
# as a general solution, https://github.com/actions/runner-images/issues/8932#issuecomment-1836013315)
PYFIX_FILE=/usr/local/Cellar/glib/2.78.1/share/glib-2.0/codegen/utils.py
if [ -f "${PYFIX_FILE}" ] ; then
  python3 -m pip install --upgrade pip
  python3 -m pip install looseversion

  sed -i '' "s/distutils.version/looseversion/" "${PYFIX_FILE}"
fi

# dbg
command -v python3
python3 --version
python3 -m pip freeze
