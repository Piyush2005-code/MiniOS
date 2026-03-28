# MiniOS - ARM64 ML Inference Unikernel

MiniOS is a specialized unikernel operating system designed to execute machine learning inference workloads on ARM64-based embedded platforms. By eliminating traditional operating system overhead such as filesystems, multi-user management, and POSIX compatibility, it treats ML computation graphs as the primary execution unit to achieve maximum performance and predictability.

## Project Overview

MiniOS targets machine learning inference on ARM64 systems, specifically optimized for the QEMU virt machine (Cortex-A53). It operates in a single address space with no user/kernel boundary, utilizing a flat 512MB RAM segment.

### Design Philosophy

- Single Address Space: One flat 512MB RAM segment for efficiency.
- Cooperative Execution: Threads yield voluntarily to minimize scheduling overhead.
- Static Allocation: Memory is pre-allocated; no runtime malloc or free.
- Graph-Centric: Optimized for hosting and executing ML operator pipelines.
- Minimalism: Footprint under 256KB with no dependencies beyond the compiler runtime.

### Target Hardware

- Primary: QEMU virt machine (Cortex-A53 CPU, 512 MB RAM).
- Physical Targets: Raspberry Pi 3/4, NVIDIA Jetson Nano, generic ARM64 boards.
- Toolchain: aarch64-elf-gcc 10+ or Clang 12+.

---

## System Architecture

The system is composed of a Hardware Abstraction Layer (HAL), a Kernel Core, and Application Threads.

```mermaid
graph TB
    subgraph "Physical Hardware / QEMU virt"
        HW[ARM64 Cortex-A53 CPU]
        MMIO[Memory-Mapped I/O\nUART · GIC · Timer]
        RAM[512 MB DRAM\n0x40000000-0x5FFFFFFF]
    end

    subgraph "MiniOS Kernel Image"
        BOOT[boot.S\n_start entry point]
        subgraph HAL[Hardware Abstraction Layer]
            UART[uart.c\nPL011 Serial Driver]
            MMU[mmu.c\nMMU + Cache Setup]
            GIC[gic.c\nGICv2 Interrupt Controller]
            TMR[timer.c\nARM Generic Timer]
            ARCH[arch.h\nInline ARM64 Primitives]
        end
        subgraph KERNEL[Kernel Core]
            MAIN[main.c\nkernel_main()  IRQ Dispatch]
            KMEM[kmem.c\nBump · Arena · Pool Allocators]
            THREAD[thread.c\nCooperative Scheduler + TCBs]
            CTX[context.S\nCPU Context Switch]
        end
        subgraph LIB[Freestanding Libraries]
            STR[string.c\nmemset · memcpy · strlen]
        end
        subgraph APP[Application Threads]
            INF[inference_thread\nSimulated ML Workload]
            MON[monitor_thread\nMemory + Uptime Monitor]
            IDLE[idle thread\nWFE low-power loop]
        end
    end

    BOOT --> HAL
    BOOT --> KERNEL
    KERNEL --> APP
    HAL --> MMIO
    MMIO --> HW
    RAM --> KERNEL
```

---

## Key Subsystems

### Kernel Memory Manager (KMEM)

The KMEM module implements three allocation strategies over the heap:
1. Bump Allocator: Permanent kernel allocations such as thread stacks.
2. Arena Allocator: Resettable regions for per-inference-cycle tensor data.
3. Pool Allocator: Fixed-size object recycling for operator descriptors.

### Threading and Cooperative Scheduler

MiniOS uses a cooperative multithreading model with a 4-level priority scheduler:
- HIGH (0): Critical tasks.
- NORMAL (1): Standard background tasks.
- LOW (2): Housekeeping and daemons.
- IDLE (3): Low-power wait loop.

### Background Daemons

The system includes built-in daemons for housekeeping:
- clock_daemon: Tracks wall-clock seconds.
- memwatch_daemon: Monitors heap usage and warns at 80% threshold.
- runtime_daemon: Reports system uptime and thread counts.
- shell_daemon: Provides the interactive UART command shell.

### Command Framework and Shell

The interactive shell provides a CLI for system management. Built-in commands include:
- help: Display available commands.
- uptime: Show system running time.
- memstat: Display memory allocation statistics.
- ps: List active threads and their states.
- clear: Clear the terminal screen.

---

## Directory Structure

- Makefile: Build system configuration.
- linker.ld: Kernel image memory layout.
- include/: Header files for HAL, Kernel, and Lib.
- src/: Source code for Boot, HAL, Kernel core, and Utilities.
- scripts/: Helper scripts for running and testing.
- results/: Performance and benchmark logs.

---

## Build and Run

To compile the kernel and run it in QEMU:

1. Compile all sources:
   make

2. Launch in QEMU:
   make run

3. Launch with GDB debug:
   make debug

To exit QEMU, use Ctrl+A followed by X.

---

## API Reference Summary

The kernel provides a unified API for development:
- HAL API: UART, MMU, GIC, and Timer management.
- KMEM API: Memory allocation and statistics.
- Threading API: Thread creation, yielding, and sleeping.
- Daemon API: Registry and status for background tasks.
- Command API: Custom command registration.

For detailed function signatures, refer to the PROJECT_DOCUMENTATION.md file.

---
End of README - MiniOS Project
