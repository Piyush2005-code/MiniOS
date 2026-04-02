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

if [ ! -f "$PROJECT_DIR/flash.img" ]; then
    echo "Creating empty 64MB flash.img..."
    dd if=/dev/zero of="$PROJECT_DIR/flash.img" bs=1M count=64
fi

QEMU_BASE="-machine virt -cpu cortex-a53 -m 512M -nographic -kernel $KERNEL -drive if=pflash,file=$PROJECT_DIR/flash.img,format=raw,index=1 -netdev user,id=net0,hostfwd=udp::9000-:9000 -device virtio-net-device,netdev=net0"
QEMU_DOCKER_FLAGS="-machine virt -cpu cortex-a53 -m 512M -nographic -kernel /workspace/build/kernel.elf -drive if=pflash,file=/workspace/flash.img,format=raw,index=1 -netdev user,id=net0,hostfwd=udp::9000-:9000 -device virtio-net-device,netdev=net0"

# Check if qemu-system-aarch64 is available natively
if command -v qemu-system-aarch64 &> /dev/null; then
    # Use native QEMU
    if [ "$1" = "debug" ]; then
        echo "Starting QEMU in debug mode (GDB port 1234)..."
        echo "Connect with: aarch64-linux-gnu-gdb $KERNEL"
        echo "Then:         target remote :1234"
        qemu-system-aarch64 $QEMU_BASE -S -gdb tcp::1234
    else
        echo "Starting MiniOS in QEMU (Ctrl+A then X to exit)..."
        qemu-system-aarch64 $QEMU_BASE
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
            "apk add qemu-system-aarch64 > /dev/null 2>&1 && qemu-system-aarch64 $QEMU_DOCKER_FLAGS"
    fi
fi
