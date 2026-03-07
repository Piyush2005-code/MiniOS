# MiniOS

**A minimal ARM64 bare-metal unikernel designed for low-overhead machine learning inference on embedded platforms.**

MiniOS eliminates traditional OS abstractions — no syscalls, no virtual filesystem, no dynamic linker — to deliver deterministic, low-latency neural network execution in a single address space. The system targets the QEMU `virt` machine (Cortex-A53, 512 MB RAM) and is cross-compiled with `aarch64-elf-gcc -std=c11 -ffreestanding -nostdlib`.

---

## Project Team

| Member | Role |
|---|---|
| Piyush Singh Bhati | Tech Lead, Bootloader, HAL |
| Darpan Baviskar | Integration Lead |
| Harshit Saini | ML Runtime, Scheduler |
| Aashma Yadav | System Components, UART |

---

## Architecture Overview

```
+----------------------------------------------------------+
|          MiniOS ML Inference Unikernel (ARM64)           |
+----------------------------------------------------------+
|  ONNX Graph Parser  |  Execution Engine  |  Telemetry   |
+----------------------------------------------------------+
|       Cooperative Scheduler / Task Runtime               |
+----------------------------------------------------------+
|  UART / RUDP  |  Memory Manager  |  HAL (Timer, GIC)    |
+----------------------------------------------------------+
|  Cortex-A53 | MMU (3-level) | NEON SIMD | PL011 UART    |
+----------------------------------------------------------+
```

---

## Branch Structure

The repository is organized into eleven branches, each representing a discrete system layer or development phase. They are intended to be integrated in the order listed below.

### 1. `main`
The default branch. Contains the project-level README and serves as the integration target once all component branches reach a stable state.

### 2. `baremetalOS`
The initial bare-metal bring-up. Implements ARM64 exception level transitions (EL3 to EL1), 3-level MMU page tables with 4 KB granule, device/normal memory mapping, and a PL011 UART driver. Includes cross-platform Makefiles for macOS, Ubuntu, and Arch Linux. This branch establishes the hardware execution environment all subsequent branches build upon.

### 3. `BootLoader_and_HAL`
Extends the bare-metal kernel with a structured Hardware Abstraction Layer (HAL). Provides timer (`hal/timer.h`), UART (`hal/uart.h`), and GIC interrupt controller drivers behind stable C interfaces. Separates hardware-specific code from higher-level kernel logic.

### 4. `UART_Implementation`
Dedicated implementation of the serial communication protocol. Defines the framing, baud configuration, and character-level I/O routines used by both the kernel console and the RUDP network layer above it.

### 5. `build/kernel_api` — Sprint 1: Kernel API Layer
First sprint of the unified Kernel API (`kapi.h`). Integrates the GIC interrupt controller and ARM Generic Timer into the memory manager, updates the linker script, and establishes the status code taxonomy used across all modules. Verified by automated assertions during build.

### 6. `Kernel_API` — Sprint 2: Full Multithreaded API
Completes the Kernel API surface across five modules: HAL Timer (10 functions), Memory Manager (12 functions), Core KAPI (9 functions), Scheduler (16 functions), and HAL UART (5 functions) — 38 APIs total. All 23 automated assertions pass. This branch represents the stable ABI that the scheduler benchmark and ONNX runtime depend on.

### 7. `feat/onnx`
Implements the ONNX ML inference runtime. Ships an ONNX graph parser, an operator execution engine (Conv2D, MatMul, ReLU, Softmax, Add), a three-tier static memory allocator (bump allocator, pool allocator, tensor lifetime graph planner), and MMU-enforced memory protection regions. The graph memory planner uses greedy interval-graph coloring to maximize tensor memory reuse without dynamic allocation.

### 8. `net-protocol`
Implements RUDP (Reliable UDP) as a bare-metal network protocol for transmitting ML control frames and ONNX model payloads over a virtual Ethernet interface. Control frames use ACK/NACK with up to five retransmits; telemetry frames are best-effort. Large ONNX models are fragmented with stop-and-wait per fragment. CRC-16/CCITT provides integrity verification. A Python host-side client (`rudp_client.py`) is included for testing.

### 9. `unikernel`
Benchmarks ML inference throughput and latency against a general-purpose Linux VM running inside QEMU/KVM using the Unikraft unikernel framework. Quantifies the overhead reduction achievable by eliminating OS abstractions for single-workload inference.

### 10. `scheduler_benchmark`
Profiles four scheduling policies — FIFO, Round Robin, Multilevel Queue (MLQ), and Lottery — against an ML inference workload composed of five representative operators (Conv2D, MatMul, Softmax, ReLU, Add). Benchmarks run on QEMU ARM64 using the ARM Generic Timer at 62.5 MHz. Results and analysis charts are generated programmatically via Python/matplotlib.

### 11. `SRS-and-Reports`
Documentation branch. Contains the Software Requirements Specification (SRS v1.0, v2.0) authored in LaTeX, assignment reports, and a full suite of UML diagrams (component, deployment, use-case, activity, class, sequence, and timing diagrams).

---

## Build Requirements

- `aarch64-elf-gcc` (or `aarch64-linux-gnu-gcc`) for cross-compilation
- `QEMU` with `qemu-system-aarch64` for emulation
- `make` (branch-specific Makefiles: `Makefile.macos`, `Makefile.ubuntu`, `Makefile.arch`)

Each branch contains its own `Makefile` and build instructions in its respective README or documentation file.

---

## Status

Active development. All branches are in varying stages of completion. Refer to individual branch documentation for current build and test status.
