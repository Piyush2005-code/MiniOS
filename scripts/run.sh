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

QEMU_FLAGS="-machine virt -cpu cortex-a53 -m 512M -nographic -kernel $KERNEL"

if [ "$1" = "debug" ]; then
    echo "Starting QEMU in debug mode (GDB port 1234)..."
    echo "Connect with: aarch64-elf-gdb $KERNEL"
    echo "Then:         target remote :1234"
    qemu-system-aarch64 $QEMU_FLAGS -S -s
else
    echo "Starting MiniOS in QEMU (Ctrl+A then X to exit)..."
    qemu-system-aarch64 $QEMU_FLAGS
fi
