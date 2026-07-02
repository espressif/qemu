#!/bin/sh
# Usage: ./test_qemu.sh <soc-target> <path-to-bin-or-build-dir>
#
# If the path points to an ESP-IDF build directory (containing flash_args),
# esptool.py merge_bin is used to produce a complete flash image automatically.
# Otherwise the file is used as-is (must be a valid 2/4/8/16 MB flash image).
#
# Examples:
#   ./test_qemu.sh esp32c6 /path/to/hello_world/build
#   ./test_qemu.sh esp32c3 /tmp/my_flash_image.bin

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <soc-target> <path-to-bin-or-build-dir>"
    exit 1
fi

SOC_TARGET="$1"
INPUT="$2"
FLASH_SIZE="2MB"
QEMU="$(cd "$(dirname "$0")" && pwd)/build/qemu-system-riscv32"

if [ -d "$INPUT" ] && [ -f "$INPUT/flash_args" ]; then
    BUILD_DIR="$(cd "$INPUT" && pwd)"
    IMAGE="/tmp/${SOC_TARGET}_merged_flash.bin"
    echo "Merging flash image from build directory: $BUILD_DIR"
    (cd "$BUILD_DIR" && python3 -m esptool --chip "$SOC_TARGET" merge_bin \
        --output "$IMAGE" --fill-flash-size "$FLASH_SIZE" \
        $(cat flash_args))
else
    IMAGE="$INPUT"
fi

echo "Running: $SOC_TARGET with $IMAGE"
"$QEMU" -nographic -icount 3 -machine "$SOC_TARGET" \
    -drive file="$IMAGE",if=mtd,format=raw
