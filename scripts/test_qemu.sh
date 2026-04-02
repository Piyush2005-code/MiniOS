#!/bin/bash
cd /home/vashu/workspace/college/prml/MiniOS
# Ensure qemu is installed
if ! command -v qemu-system-aarch64 &> /dev/null; then
    echo "QEMU not found natively. Please wait for installation to finish."
    exit 1
fi
echo "Running native QEMU..."
timeout 15 qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a53 \
  -m 256M \
  -nographic \
  -serial stdio \
  -kernel build/kernel.elf \
  -netdev user,id=net0,hostfwd=udp::9000-:9000 \
  -device virtio-net-device,netdev=net0 > build/qemu_serial.log 2>&1
echo "Exit code: $?"
cat build/qemu_serial.log
