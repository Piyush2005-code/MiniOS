#!/bin/bash
# ============================================================================
# run.sh — Launch MiniOS in QEMU
#
# Usage:
#   ./scripts/run.sh           # Normal run
#   ./scripts/run.sh debug     # Debug mode (GDB port 1234)
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
KERNEL="$PROJECT_DIR/build/kernel.elf"

if [ ! -f "$KERNEL" ]; then
    echo "Error: kernel.elf not found. Run 'make' first."
    exit 1
fi

QEMU_FLAGS="-machine virt -cpu cortex-a53 -m 512M -nographic -kernel /workspace/build/kernel.elf"

# Check if qemu-system-aarch64 is available natively
if command -v qemu-system-aarch64 &> /dev/null; then
    # Use native QEMU
    if [ "$1" = "debug" ]; then
        echo "Starting QEMU in debug mode (GDB port 1234)..."
        echo "Connect with: aarch64-linux-gnu-gdb $KERNEL"
        echo "Then:         target remote :1234"
        qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 512M -nographic -kernel "$KERNEL" -S -s
    else
        echo "Starting MiniOS in QEMU (Ctrl+A then X to exit)..."
        qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 512M -nographic -kernel "$KERNEL"
    fi
else
    # Use Docker with QEMU
    echo "Using Docker with QEMU (qemu-system-aarch64 not found locally)..."
    if [ "$1" = "debug" ]; then
        echo "Debug mode not supported with Docker. Install qemu-system-aarch64 natively."
        exit 1
    else
        echo "Starting MiniOS in QEMU (Ctrl+A then X to exit)..."
        docker run --rm -v "$PROJECT_DIR:/workspace" -it alpine:latest sh -c \
            "apk add qemu-system-aarch64 > /dev/null 2>&1 && qemu-system-aarch64 $QEMU_FLAGS"
    fi
fi
