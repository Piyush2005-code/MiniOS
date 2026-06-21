#!/usr/bin/env python3
import subprocess
import os
import sys
import json
import time
import fcntl

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS_DIR = os.path.join(PROJECT_DIR, "results")
KERNEL = os.path.join(PROJECT_DIR, "build", "kernel.elf")
FLASH = os.path.join(PROJECT_DIR, "flash.img")

os.makedirs(RESULTS_DIR, exist_ok=True)

MODELS = [
    "tiny_mlp.onnx",
    "lenet5.onnx",
    "conv_bn_net.onnx",
    "alexnet_tiny.onnx",
    "vgg_nano.onnx",
    "resnet_micro.onnx",
    "transformer_tiny.onnx",
    "mnist_mlp.onnx",
    "squeezenet_nano.onnx",
    "mobilenet_tiny.onnx"
]

def run_benchmarks():
    cmd = [
        "qemu-system-aarch64",
        "-machine", "virt",
        "-cpu", "cortex-a53",
        "-m", "2048M",
        "-nographic",
        "-kernel", KERNEL,
        "-drive", f"if=pflash,file={FLASH},format=raw,index=1",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-device,netdev=net0"
    ]
    
    print(f"Starting QEMU...")
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=0 # Unbuffered
    )
    
    fd = proc.stdout.fileno()
    fl = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    def read_until(prompt, timeout=10.0):
        start = time.time()
        buf = ""
        while time.time() - start < timeout:
            try:
                chunk = os.read(fd, 4096).decode('utf-8')
                if chunk:
                    buf += chunk
                    sys.stdout.write(chunk)
                    sys.stdout.flush()
                    if prompt in buf:
                        return buf
            except BlockingIOError:
                time.sleep(0.01)
        return buf

    print("Waiting for boot...")
    boot_text = read_until("miniOS>", timeout=10.0)
    if "miniOS>" not in boot_text:
        print("\nFailed to boot MiniOS.")
        proc.kill()
        return

    time.sleep(0.5)

    for model in MODELS:
        print(f"\n>>> Benchmarking {model}...")
        
        proc.stdin.write(f"bench_iter /storage/{model} 50\n")
        proc.stdin.flush()
        
        out_buf = read_until("miniOS>", timeout=120.0)
        
        # Parse JSON from the output buffer
        lines = out_buf.split('\n')
        done = False
        for line in lines:
            line = line.strip()
            if line.startswith("{") and "\"iters\":" in line:
                try:
                    data = json.loads(line)
                    out_path = os.path.join(RESULTS_DIR, f"{model}_minios_50.json")
                    with open(out_path, "w") as f:
                        json.dump(data, f, indent=2)
                    print(f"\n    Saved {out_path}")
                except json.JSONDecodeError:
                    print("\n    Failed to decode JSON.")
                done = True
                break
            elif "status\":\"" in line:
                print(f"\n    Error reported by MiniOS: {line}")
                done = True
                break
        
        if not done:
            print(f"\n    Timeout/Failure waiting for {model}")
                
    proc.kill()
    print("\nBenchmarks complete.")

if __name__ == "__main__":
    if not os.path.exists(FLASH):
        with open(FLASH, "wb") as f:
            f.write(b"\0" * (64 * 1024 * 1024))
    run_benchmarks()
