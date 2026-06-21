import pexpect
import sys

child = pexpect.spawn("qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 2048M -nographic -kernel build/kernel.elf -drive if=pflash,file=flash.img,format=raw,index=1 -netdev user,id=net0 -device virtio-net-device,netdev=net0", encoding="utf-8")
child.logfile = sys.stdout

child.expect("miniOS>")
child.sendline("onnx_bench /storage/alexnet_tiny.onnx 1")

try:
    child.expect(["GEMM mismatch", "INFERENCE_ERROR", "Saved"], timeout=20)
    print("Found result.")
except Exception as e:
    print(e)
child.terminate(True)
