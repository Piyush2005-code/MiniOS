# MiniOS: Bare-Metal Bootstrap for ARM64 ML Inference Unikernel

## Overview
This repository contains the **minimal hardware abstraction layer (HAL)** for the ML Inference Unikernel project as defined in the SRS (Version 3.0). It is not a general-purpose OS; it is the foundation for a **single-address-space, deterministic ML inference appliance**.

**Goal:** Provide a stable, working bootstrap so developers can immediately begin implementing ONNX graph processing and NEON-optimized kernels in C, rather than debugging boot code.

## Code Architecture
The bootstrap consists of 4 stages mirroring the SRS requirements:

1.  **Hardware Initialization (FR-001 to FR-005):**
    - Sets Exception Level to EL1 (kernel mode).
    - Installs a dummy exception vector table.
    - Enables the ARM Generic Timer (implicitly via EL1).
2.  **FP/SIMD Enablement (FR-013):**
    - The `cpacr_el1` register is configured to enable Advanced SIMD (NEON) and FPU at EL1. This allows C code to use vector intrinsics (`arm_neon.h`) immediately.
3.  **Memory Management (FR-016 to FR-020):**
    - Enables the MMU with a **flat identity map**. This allows the data cache to be enabled.
    - Allocates a 64KB stack for C execution.
    - **Static Allocation:** This bootstrap uses a static allocator pattern (stack and BSS). The dynamic static allocator for tensors (SRS FR-016) is the *next* logical step for the team to implement.
4.  **Communication Interface (FR-021):**
    - Provides a `print()` function over the PL011 UART (QEMU `virt` machine default).
    - Semihosting fallback is included for debugging in QEMU.

## Build Instructions
**Prerequisites:**
- Clang/LLVM (or GCC `aarch64-none-elf`)
- QEMU (>= 5.0.0)
- GNU Make

**Steps:**
```bash
# Clone / copy files into a directory
# Build the ELF and binary
make clean && make

# Run in QEMU
make run