#!/bin/sh
# Build and run the bare-metal ESP32-C3 SAR ADC test. See README.md.
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
# The startup stub and linker script are the I2C test's: nothing in them is
# specific to it.
BARE="$HERE/../esp32c3-i2c"

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

# Pad 5 is ADC2 channel 0, the one the firmware's volume dial is on. Pads 0
# and 4 pin both ends of the range; 1..3 are left out so the default shows.
PAD0=0
PAD4=4095
PAD5=1234
MACHINE="esp32c3,adc-pad0=$PAD0,adc-pad4=$PAD4,adc-pad5=$PAD5"

status=0

# The LOAD segment is RWX because text and stack share one region; harmless
# here and not worth a second segment, so silence just that warning.
"$CROSS_CC" -march=rv32imc_zicsr -mabi=ilp32 -O1 -ffreestanding -nostdlib \
    -Wall -DEXPECT_PAD0=$PAD0 -DEXPECT_PAD4=$PAD4 -DEXPECT_PAD5=$PAD5 \
    -T "$BARE/i2ctest.ld" -o "$WORK/adc.elf" \
    "$BARE/start.S" "$HERE/adctest.c" 2>&1 | grep -v "RWX permissions" || true
[ -f "$WORK/adc.elf" ] || { echo "FAIL: compile failed" >&2; exit 1; }

echo "--- registers ($MACHINE) ---"
# The guest spins once it has printed its verdict, so stop as soon as that
# shows up rather than sitting out a fixed timeout.
"$QEMU" -display none -monitor none -serial "file:$WORK/adc.log" \
    -machine "$MACHINE" -kernel "$WORK/adc.elf" > "$WORK/adc.err" 2>&1 &
pid=$!
n=0
while [ "$n" -lt 300 ] && kill -0 "$pid" 2>/dev/null &&
      ! grep -q "ADCTEST \(ALL OK\|FAILURES\)" "$WORK/adc.log" 2>/dev/null; do
    sleep 0.1
    n=$((n + 1))
done
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
if grep -q "ADCTEST START" "$WORK/adc.log" 2>/dev/null; then
    sed -n '/ADCTEST START/,/ADCTEST \(ALL OK\|FAILURES\)/p' "$WORK/adc.log"
else
    cat "$WORK/adc.err"
fi
grep -q "ADCTEST ALL OK" "$WORK/adc.log" 2>/dev/null || status=1

# A code that does not fit in 12 bits is a mistake in the invocation, and has
# to be refused rather than silently truncated into a plausible reading.
echo "--- out-of-range code (esp32c3,adc-pad5=4096) ---"
if timeout 10 "$QEMU" -display none -monitor none -serial none \
       -machine esp32c3,adc-pad5=4096 > "$WORK/range.log" 2>&1; then
    echo "started anyway FAIL"
    status=1
elif grep -q "adc-pad5: expected a raw ADC code" "$WORK/range.log"; then
    echo "rejected: $(cat "$WORK/range.log") ok"
else
    echo "failed for another reason FAIL"
    cat "$WORK/range.log"
    status=1
fi

if [ "$status" -eq 0 ]; then
    exit 0
fi
echo "FAIL: see the checks above" >&2
exit 1
