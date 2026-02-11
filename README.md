# MiniOS: Bare‑Metal ARM64 Unikernel for ML Inference

This repository contains a **minimal, production‑ready bootstrap** for an ARM64 unikernel optimised for ML inference (as specified in SRS v3.0). It provides:

- ARMv8‑A boot code (EL3 → EL2 → EL1) with exception vectors
- MMU initialisation with identity‑mapped RAM (Normal Cacheable) and UART mapping (Device)
- FP/SIMD (NEON) enablement
- PL011 UART driver (115200 baud) for console output
- A clean C environment (`main()`) – start coding your ML runtime immediately!

---

## 🚀 Quick Start (Choose Your OS)

### 🍏 macOS

1. **Install prerequisites** (Homebrew):
   ```bash
   brew install aarch64-elf-gcc qemu
   ```
2. **Build and Run:
   ```bash
   make -f Makefile.macos clean
   make -f Makefile.macos
   make -f Makefile.macos run
   ```

### 🐧 Arch Linux

1. **Install prerequisites** (Homebrew):   
   ```bash
   sudo pacman -S aarch64-linux-gnu-gcc aarch64-linux-gnu-binutils qemu-system-aarch64
   ```
2. **Build and Run:
   ```bash
   make -f Makefile.arch clean
   make -f Makefile.arch
   make -f Makefile.arch run
   ```

### 🐧 Ubuntu 22.04

1. **Install prerequisites** (Homebrew):   
   ```bash
   sudo apt update
   sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm
   ```
2. **Build and Run:
   ```bash
   make -f Makefile.ubuntu clean
   make -f Makefile.ubuntu
   make -f Makefile.ubuntu run
   ```
