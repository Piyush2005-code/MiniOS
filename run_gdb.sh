#!/bin/bash
qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 2048M -nographic -kernel build/kernel.elf -drive if=pflash,file=flash.img,format=raw,index=1 -netdev user,id=net0 -device virtio-net-device,netdev=net0 -s -S &
QEMU_PID=$!
sleep 1
cat << 'GDB' > gdb_script.txt
target remote :1234
b *0x40001D48
c
bt
info registers
kill
quit
GDB
aarch64-elf-gdb build/kernel.elf -x gdb_script.txt > gdb_out.txt 2>&1
kill $QEMU_PID 2>/dev/null
cat gdb_out.txt
