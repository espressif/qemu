#!/bin/sh
# Build and run the bare-metal ESP32-C3 I2C tests. See README.md.
#
# Two guests from one source file: the controller test, and the bit-banged
# software-master test. Both must pass.
#
# Usage: ./run.sh
# Env:   QEMU        path to qemu-system-riscv32
#                    (default: ../../build/qemu-system-riscv32)
#        CROSS_CC    riscv32 bare-metal C compiler
#                    (default: riscv32-esp-elf-gcc from PATH, else the newest
#                    toolchain under ~/.espressif)
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
QEMU="${QEMU:-$HERE/../../build/qemu-system-riscv32}"

if [ -z "${CROSS_CC:-}" ]; then
    if command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then
        CROSS_CC=riscv32-esp-elf-gcc
    else
        CROSS_CC="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc \
                    2>/dev/null | tail -1)"
    fi
fi

if [ -z "$CROSS_CC" ] || ! command -v "$CROSS_CC" >/dev/null 2>&1; then
    echo "SKIP: no riscv32 bare-metal compiler found; set CROSS_CC" >&2
    exit 77
fi
if [ ! -x "$QEMU" ]; then
    echo "FAIL: $QEMU is not executable; set QEMU" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Both guests expect chips at 0x58 (reg 0x10 = 0x23) and 0x3a (reg 0x00 =
# 0x18), and nothing at 0x08 or 0x5b so the NACK path is covered.
CHIPS="-device oro-i2c-chip,address=0x58,name=ioexp,regs=0x10:0x23
-device oro-i2c-chip,address=0x3a,name=leds,regs=0x00:0x18"

# build <elf> [extra cflags...]
build() {
    out="$1"; shift
    # The LOAD segment is RWX because text and stack share one region; harmless
    # here and not worth a second segment, so silence just that warning.
    "$CROSS_CC" -march=rv32imc_zicsr -mabi=ilp32 -O1 -ffreestanding -nostdlib \
        -Wall "$@" -T "$HERE/i2ctest.ld" -o "$out" \
        "$HERE/start.S" "$HERE/i2ctest.c" 2>&1 | grep -v "RWX permissions" || true
    [ -f "$out" ] || { echo "FAIL: compile failed ($out)" >&2; exit 1; }
}

# run <label> <elf> <machine>
run() {
    label="$1"; elf="$2"; machine="$3"
    echo "--- $label ($machine) ---"
    # shellcheck disable=SC2086
    timeout 60 "$QEMU" -nographic -machine "$machine" -kernel "$elf" \
        $CHIPS > "$WORK/$label.log" 2>&1 || true
    sed -n '/I2CTEST START/,/I2CTEST \(ALL OK\|FAILURES\)/p' "$WORK/$label.log"
    grep -q "I2CTEST ALL OK" "$WORK/$label.log" || return 1
}

status=0

# No "i2c-pads", so the board pad check stays out of the way -- this guest
# pokes the controller directly and never touches the GPIO matrix. Keeping an
# invocation without the property is deliberate: it is what covers the
# fail-open path, and no bit-bang bridge is instantiated either.
build "$WORK/ctrl.elf"
run controller "$WORK/ctrl.elf" esp32c3 || status=1

# The bit-banged guest needs the board's pads declared, because that is what
# the bridge onto the chips' I2C bus is bound to. It bit-bangs 7/6 (the bus)
# and 1/2 (not the bus, which must find nothing).
build "$WORK/bitbang.elf" -DTEST_BITBANG
run bitbang "$WORK/bitbang.elf" esp32c3,i2c-pads=7/6 || status=1

if [ "$status" -eq 0 ]; then
    exit 0
fi
echo "FAIL: see the checks above" >&2
exit 1
