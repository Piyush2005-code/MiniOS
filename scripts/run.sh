#!/bin/bash
# scripts/run.sh — Launch MiniOS-NetProtocol in QEMU
#
# Sets up a tap0 interface on the host so RUDP frames can be
# exchanged between the ARM64 unikernel and a host-side client.
#
# Usage:
#   ./scripts/run.sh [virtio|lan9118]
#
# Prerequisites (Linux host):
#   sudo apt-get install qemu-system-aarch64
#   sudo ip tuntap add dev tap0 mode tap user $(whoami)
#   sudo ip link set tap0 up
#   sudo ip addr add 192.168.100.1/24 dev tap0

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
KERNEL="$REPO_ROOT/build/kernel.bin"

if [ ! -f "$KERNEL" ]; then
    echo "[run.sh] kernel.bin not found — run 'make' first"
    exit 1
fi

# Guest MAC:  52:54:00:12:34:56  (LOCAL_MAC in main.c)
# Host tap MAC: 52:54:00:12:34:57 (PEER_MAC in main.c)

echo "[run.sh] Starting QEMU ARM64 with RUDP net-protocol kernel"
echo "  Guest MAC:  52:54:00:12:34:56"
echo "  Host tap:   tap0 (52:54:00:12:34:57)"
echo "  Press Ctrl-A X to exit QEMU"
echo ""

qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a53 \
    -m 256M \
    -nographic \
    -kernel "$KERNEL" \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device virtio-net-device,netdev=net0,mac=52:54:00:12:34:56