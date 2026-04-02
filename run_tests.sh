#!/bin/bash
docker run --rm -v $(pwd):/workspace -w /workspace alpine:latest sh -c "apk add --no-cache qemu-system-aarch64 >/dev/null && timeout 5s qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 512M -nographic -kernel build/kernel.elf -netdev user,id=net0,hostfwd=udp::9000-:9000 -device virtio-net-device,netdev=net0 > output_final.log 2>&1"
cat output_final.log
