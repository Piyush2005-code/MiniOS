#!/bin/bash
# =============================================================================
# ubuntu_qemu_boot.sh — Launch Ubuntu in QEMU for ONNX benchmarking
#
# QEMU flags are IDENTICAL to MiniOS (see qemu_flags.log at project root):
#   -machine virt
#   -cpu cortex-a57
#   -m 2048
#   -smp 1
#   -nographic
#
# Usage:
#   ./scripts/ubuntu_bench/ubuntu_qemu_boot.sh <path-to-ubuntu-image.img> [<kernel>]
#
# If you have a pre-built Ubuntu cloud image with a separate kernel:
#   ./ubuntu_qemu_boot.sh ubuntu-22.04-server-cloudimg-arm64.img
#
# If using a full disk image that contains its kernel internally, QEMU's
# -kernel flag can be omitted; adjust as needed for your image type.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

DISK_IMG="${1:-}"
KERNEL="${2:-}"

if [[ -z "$DISK_IMG" ]]; then
    echo "Usage: $0 <ubuntu-disk.img> [optional-kernel]"
    echo ""
    echo "Example:"
    echo "  $0 ubuntu-22.04-arm64.img"
    exit 1
fi

# ---- Canonical QEMU flags (same as MiniOS) ----
QEMU_BASE_FLAGS=(
    -machine virt
    -device virtio-rng-pci
    -cpu cortex-a57
    -m 2048
    -smp 1
    -nographic
    -bios /usr/share/edk2/aarch64/QEMU_EFI.fd
)

# ---- Disk ----
DISK_FLAGS=(-drive "if=virtio,file=${DISK_IMG},format=qcow2")

# ---- Cloud Init Seed ----
if [[ -f "seed.iso" ]]; then
    DISK_FLAGS+=(-drive "if=virtio,format=raw,file=seed.iso")
fi

# ---- Optional kernel override ----
KERNEL_FLAGS=()
if [[ -n "$KERNEL" ]]; then
    KERNEL_FLAGS=(-kernel "$KERNEL" -append "console=ttyAMA0 root=/dev/vda1 rw")
fi

# ---- Networking (same forwarding as MiniOS) ----
NET_FLAGS=(
    -netdev user,id=net0,hostfwd=tcp::2222-:22
    -device virtio-net-device,netdev=net0
)

echo "=============================================="
echo "  Ubuntu QEMU Benchmark Environment"
echo "=============================================="
echo "  Disk   : $DISK_IMG"
echo "  CPU    : cortex-a57"
echo "  Memory : 2048 MB"
echo "  SMP    : 1"
echo "  Network: hostfwd tcp 2222→22"
echo ""
echo "  After boot, SSH into the VM:"
echo "    ssh -p 2222 ubuntu@localhost"
echo ""
echo "  Inside the VM, run setup:"
echo "    sudo apt update && sudo apt install -y python3 python3-pip"
echo "    pip3 install onnxruntime numpy"
echo ""
echo "  Thread pinning (add to /etc/environment):"
echo "    OMP_NUM_THREADS=1"
echo "    OPENBLAS_NUM_THREADS=1"
echo "    MKL_NUM_THREADS=1"
echo "    ONNXRUNTIME_NUM_THREADS=1"
echo ""
echo "  Suppress background noise:"
echo "    sudo systemctl stop cron avahi-daemon bluetooth ModemManager 2>/dev/null || true"
echo "    sudo systemctl disable cron avahi-daemon bluetooth 2>/dev/null || true"
echo ""
echo "  Dry run:"
echo "    python3 /bench/run_bench.py \\"
echo "      --model  /bench/models/squeezenet.onnx \\"
echo "      --input  /bench/inputs/squeezenet.npy \\"
echo "      --runs 1 --warmup 0 --out /bench/results/"
echo "=============================================="
echo ""
echo "Starting QEMU (Ctrl+A then X to exit)..."
echo ""

exec qemu-system-aarch64 \
    "${QEMU_BASE_FLAGS[@]}" \
    "${DISK_FLAGS[@]}" \
    "${KERNEL_FLAGS[@]}" \
    "${NET_FLAGS[@]}"
