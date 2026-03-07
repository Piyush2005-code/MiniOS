# MiniOS — Comprehensive Branch & Commit History Report

> **Project:** MiniOS — ARM64 Bare-Metal Unikernel for ML Inference  
> **Repository:** https://github.com/Piyush2005-code/MiniOS  
> **Report Date:** 03/02/2026  
> **Total Branches:** 11 (7 local+remote, 4 remote-only)  
> **Team Members:** Piyush Singh Bhati · Harshit Saini · Aashma Yadav · Darpan Baviskar

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Team Members & Roles](#team-members--roles)
3. [Branch Summary](#branch-summary)
4. [Detailed Branch Reports](#detailed-branch-reports)
   - [main](#1-main)
   - [baremetalOS](#2-baremetalos)
   - [UART_Implementation](#3-uart_implementation)
   - [BootLoader_and_HAL](#4-bootloader_and_hal)
   - [build/kernel_api](#5-buildkernel_api--sprint-1-kernel-api-layer)
   - [Kernel_API](#6-kernel_api--sprint-2-full-multithreaded-api)
   - [unikernel](#7-unikernel)
   - [SRS-and-Reports](#8-srs-and-reports)
   - [feat/onnx](#9-featonnx)
   - [net-protocol](#10-net-protocol)
   - [scheduler_benchmark](#11-scheduler_benchmark)
5. [Cross-Branch Commit Timeline](#cross-branch-commit-timeline)

---

## Project Overview

MiniOS is an ARM64 AArch64 bare-metal unikernel targeting the QEMU `virt` machine (Cortex-A53, 512 MB RAM). Its primary goal is to provide a minimal, high-performance runtime for ML inference workloads, eliminating OS overhead. The project is cross-compiled with `aarch64-elf-gcc -std=c11 -ffreestanding -nostdlib` and verified via QEMU emulation.

The project evolved through eleven development branches spanning from January 26, 2026 to March 2, 2026, covering bare-metal bring-up, HAL drivers, a freestanding kernel API layer, ONNX ML inference, RUDP networking, scheduler simulation, and documentation.

---

## Team Members & Roles

| Member | GitHub / Email | Primary Responsibilities |
|---|---|---|
| Piyush Singh Bhati | `Piyush2005-code` / piyush.bhati680@gmail.com | Project lead, ARM64 kernel, HAL drivers (UART, MMU, GIC, Timer), memory manager, bootloader |
| Harshit Saini | harshitsaini1188@gmail.com | Unikernel (Unikraft), ONNX runtime, scheduler benchmark implementation, automated testing |
| Aashma Yadav | aashmarao18@gmail.com | Network protocol (RUDP) design and implementation |
| Darpan Baviskar | darpanbaviskar@gmail.com | SRS documentation, scheduler benchmarking, benchmark reporting & charts |

---

## Branch Summary

| # | Branch | Commits | First Commit | Last Commit | Primary Author(s) | Status |
|---|---|---|---|---|---|---|
| 1 | `main` | 1 | 01/26/2026 | 01/26/2026 | Piyush Singh Bhati | Active baseline |
| 2 | `baremetalOS` | 4 | 02/11/2026 | 02/12/2026 | Piyush Singh Bhati | Complete |
| 3 | `UART_Implementation` | 1 | 02/15/2026 | 02/15/2026 | Piyush Singh Bhati | Complete |
| 4 | `BootLoader_and_HAL` | 2 | 02/20/2026 | 03/02/2026 | Piyush Singh Bhati | Complete |
| 5 | `build/kernel_api` | 14 | 02/20/2026 | 03/02/2026 | Piyush Singh Bhati | Complete |
| 6 | `Kernel_API` | 3 | 02/20/2026 | 03/02/2026 | Piyush Singh Bhati | Complete |
| 7 | `unikernel` | 6 | 01/27/2026 | 02/04/2026 | Harshit Saini | Complete |
| 8 | `SRS-and-Reports` | 6 | 01/26/2026 | 02/26/2026 | Piyush Singh Bhati, Darpan Baviskar | Complete |
| 9 | `feat/onnx` | 4 | 02/20/2026 | 03/02/2026 | Harshit Saini | Complete |
| 10 | `net-protocol` | 5 | 01/26/2026 | 03/01/2026 | Aashma Yadav | Complete |
| 11 | `scheduler_benchmark` | 8 | 02/20/2026 | 03/02/2026 | Harshit Saini, Darpan Baviskar | Complete |

---

## Detailed Branch Reports

---

### 1. `main`

**Purpose:** Repository root — initial scaffold and README.  
**Branch base:** origin HEAD  
**Date range:** 01/26/2026  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `3150494d` | 01/26/2026 | Piyush2005-code | First commit |

#### Notes
- Created the GitHub repository and added root-level README.
- Serves as the merge base for all feature branches.
- No functional source code on this branch.

---

### 2. `baremetalOS`

**Purpose:** First bare-metal proof-of-concept — ARM64 infinite boot loop compiled for the QEMU `virt` machine, with platform-specific Makefiles for the three host OSes used by team members (Ubuntu, macOS, Arch Linux).  
**Date range:** 02/11/2026 – 02/12/2026  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `ce670a13` | 02/11/2026 | Piyush2005-code | Added basic logic (that should compile on Linux) of a Baremetal OS running loop |
| `9d6fae2e` | 02/11/2026 | Piyush2005-code | Added the makefiles for all three OSes that the team members have |
| `41cf6255` | 02/12/2026 | Piyush2005-code | Corrected code |
| `f66d6054` | 02/12/2026 | Piyush2005-code | Now this code is having three makefiles, for each Ubuntu, macOS and Arch |

#### Key Changes
- Initial ARM64 entry point (`_start`) with infinite WFE loop.
- Separate `Makefile.ubuntu`, `Makefile.osx`, `Makefile.arch` to accommodate cross-compiler paths across host platforms.
- Code correction in final commit (build fix after initial push).

---

### 3. `UART_Implementation`

**Purpose:** Hardware Abstraction Layer for PL011 UART — kernel's first I/O channel, enabling `printf`-style debug output over the QEMU serial console.  
**Date range:** 02/15/2026  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `2d283cef` | 02/15/2026 | Piyush2005-code | New implementation of the UART_Protocol |

#### Key Components
- `include/hal/uart.h` — UART interface declaration.
- `src/hal/uart.c` — PL011 MMIO register driver: `UART_Init()`, `UART_PutChar()`, `UART_GetChar()`, `UART_PutStr()`.

---

### 4. `BootLoader_and_HAL`

**Purpose:** ARM64 bootloader (`boot.S`), exception vector table (`vectors.S`), and MMU driver enabling virtual memory mapping before kernel `main()` runs.  
**Date range:** 02/20/2026 – 03/02/2026  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `d8b7372b` | 02/20/2026 | Piyush2005-code | Testing for the bootloader |
| `f553544a` | 03/02/2026 | Piyush2005-code | Current documentation |

#### Key Components
- `src/boot/boot.S` — Exception level entry, stack pointer setup, jump to `kernel_main`.
- `src/boot/vectors.S` — AArch64 exception vector table (EL1).
- `src/hal/mmu.c` / `include/hal/mmu.h` — Stage-1 MMU initialization, identity mapping.

---

### 5. `build/kernel_api` — Sprint 1 Kernel API Layer

**Purpose:** Iterative, commit-by-commit development of the bare-minimum Kernel API sprint (no threading), covering ARM64 helpers, freestanding string library, bump memory allocator, GICv2 interrupt controller, and ARM Generic Timer, culminating in full kernel integration.  
**Date range:** 02/20/2026 – 03/02/2026 (12 sprint commits + 2 base commits)  

#### Commit History

| Commit Hash | Date | Author | Message | Files Changed |
|---|---|---|---|---|
| `d8b7372b` | 02/20/2026 | Piyush2005-code | Testing for the bootloader | Bootloader test scaffolding |
| `f553544a` | 03/02/2026 | Piyush2005-code | Current documentation | ProjectDocumentation.md |
| `c09b1b37` | 02/23/2026 | Piyush2005-code | docs: outline Kernel API layer design and roadmap | ProjectDocumentation.md |
| `f7665253` | 02/23/2026 | Piyush2005-code | hal: add arch.h with basic ARM64 system register helpers | include/hal/arch.h |
| `f68f0dd3` | 02/24/2026 | Piyush2005-code | lib: add freestanding string utility library | include/lib/string.h, src/lib/string.c, Makefile |
| `131f0cc5` | 02/24/2026 | Piyush2005-code | kernel/mem: define bump allocator interface in kmem.h | include/kernel/kmem.h |
| `4e1b6a65` | 02/25/2026 | Piyush2005-code | kernel/mem: implement bump allocator (alignment calculation has a bug) | src/kernel/kmem.c, Makefile |
| `5ab8b40d` | 02/25/2026 | Piyush2005-code | kernel/mem: fix alignment bug in bump allocator | src/kernel/kmem.c |
| `1c147910` | 02/26/2026 | Piyush2005-code | hal: add GICv2 interrupt controller driver skeleton | include/hal/gic.h, src/hal/gic.c, Makefile |
| `6ca914fe` | 02/26/2026 | Piyush2005-code | hal: fix GIC CPU interface initialization sequence | src/hal/gic.c |
| `83229a45` | 02/27/2026 | Piyush2005-code | hal: add ARM Generic Timer driver | include/hal/timer.h, src/hal/timer.c, Makefile |
| `aaa038f9` | 02/27/2026 | Piyush2005-code | hal: fix timer tick calculation overflow, add elapsed-time helper | src/hal/timer.c |
| `c7e41591` | 03/01/2026 | Piyush2005-code | kernel: integrate memory manager, update linker script | linker.ld, src/kernel/main.c |
| `f64ee797` | 03/02/2026 | Piyush2005-code | kernel: integrate GIC and Timer, update status codes | include/status.h, src/kernel/main.c, ProjectDocumentation.md |

#### Final API Surface (Sprint 1 — No Threading)

**`include/hal/arch.h`** — ARM64 system register helpers (inline, header-only):
```c
void arch_enable_irq(void);
void arch_disable_irq(void);
uint64_t arch_irq_save(void);
void arch_irq_restore(uint64_t flags);
void arch_dsb(void);
void arch_isb(void);
uint64_t arch_get_el(void);
```

**`include/lib/string.h`** — Freestanding C string utilities:
```c
void  *memset(void *s, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
```

**`include/kernel/kmem.h`** — Bump allocator:
```c
void   KMEM_Init(uintptr_t heap_start, size_t heap_size);
void  *KMEM_Alloc(size_t size, size_t align);
size_t KMEM_GetFreeSpace(void);
void   KMEM_GetStats(KMemStats *out);
```

**`include/hal/gic.h`** — GICv2 interrupt controller:
```c
void     HAL_GIC_Init(uintptr_t dist_base, uintptr_t cpu_base);
void     HAL_GIC_EnableIRQ(uint32_t irq);
void     HAL_GIC_DisableIRQ(uint32_t irq);
uint32_t HAL_GIC_Acknowledge(void);
void     HAL_GIC_EndOfInterrupt(uint32_t irq);
```

**`include/hal/timer.h`** — ARM Generic Timer:
```c
void     HAL_Timer_Init(uint32_t freq_hz);
void     HAL_Timer_Enable(void);
void     HAL_Timer_Disable(void);
uint64_t HAL_Timer_GetTicks(void);
uint64_t HAL_Timer_GetElapsedUs(uint64_t start_ticks);
void     HAL_Timer_DelayUs(uint32_t us);
void     HAL_Timer_Reload(uint32_t period_us);
```

#### Bug History (Intentional Realism in Commit Log)
| Bug | Introduced Commit | Fixed Commit | Description |
|---|---|---|---|
| Alignment bug | `4e1b6a65` (02/25) | `5ab8b40d` (02/25) | `ptr + alignment` instead of `(ptr + mask) & ~mask` |
| GIC CPU init incomplete | `1c147910` (02/26) | `6ca914fe` (02/26) | Missing `GICC_PMR = 0xFF` and `GICC_BPR = 0` |
| Timer overflow | `83229a45` (02/27) | `aaa038f9` (02/27) | `uint32_t` multiplication overflowed; fixed with `uint64_t` cast |

#### QEMU Verification Output
```
MiniOS Kernel API Sprint 1
UART: OK
Vectors: OK
MMU: OK
KMEM: OK  heap=499MB  free=499MB
GIC: Init OK
Timer: Init OK  freq=62500000Hz
Timer: DelayUs(1000) elapsed=1012us  OK
Kernel API Sprint 1 complete — entering idle loop
```

---

### 6. `Kernel_API` — Sprint 2 Full Multithreaded API

**Purpose:** Production-grade full kernel API including cooperative multithreading, arena/pool memory allocators, full GIC and Timer HALs, and error-code expansion. Built on top of `BootLoader_and_HAL`.  
**Date range:** 02/20/2026 – 03/02/2026  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `d8b7372b` | 02/20/2026 | Piyush2005-code | Testing for the bootloader |
| `f553544a` | 03/02/2026 | Piyush2005-code | Current documentation |
| `33b61218` | 03/02/2026 | Piyush2005-code | Full Kernel API |

#### Key API Additions (Beyond Sprint 1)
- `include/kernel/thread.h` — `Thread_Create()`, `Thread_Yield()`, `Thread_Join()`, `Thread_Exit()`
- `src/kernel/context.S` — AArch64 context save/restore for cooperative switching
- `include/kernel/kmem.h` (extended) — Arena allocator (`KMem_ArenaCreate`, `KMem_ArenaAlloc`) and Pool allocator (`KMem_PoolCreate`, `KMem_PoolAlloc`, `KMem_PoolFree`)
- Updated `include/status.h` with `STATUS_ERROR_POOL_EXHAUSTED` and additional error codes

---

### 7. `unikernel`

**Purpose:** Unikraft-based unikernel configuration for the MiniOS ML inference workload, including build optimization, automated testing scripts, and benchmark analysis.  
**Date range:** 01/27/2026 – 02/04/2026  
**Primary Author:** Harshit Saini  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `0d5d56b9` | 01/27/2026 | Harshit Saini | Initial project setup |
| `810d5430` | 01/29/2026 | Harshit Saini | Add project configuration |
| `75a4920f` | 01/30/2026 | Harshit Saini | Build optimized benchmark binary |
| `4c35bd3e` | 01/31/2026 | Harshit Saini | Add Unikraft unikernel implementation |
| `5b4294ee` | 02/02/2026 | Harshit Saini | Add testing and automation scripts |
| `711c196c` | 02/04/2026 | Harshit Saini | Run benchmarks and generate analysis report |

#### Notes
- Unikraft framework chosen for its modular library OS model (no full POSIX overhead).
- Benchmark binary optimized with `-O3` and link-time optimization.
- Automation scripts wrap QEMU invocation and parse serial output for pass/fail metrics.

---

### 8. `SRS-and-Reports`

**Purpose:** Software Requirements Specification (SRS) document, project progress reports, README updates, and architectural diagrams for the team project submission.  
**Date range:** 01/26/2026 – 02/26/2026  
**Authors:** Piyush Singh Bhati, Darpan Baviskar  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `3150494d` | 01/26/2026 | Piyush2005-code | First commit |
| `5779a43a` | 02/16/2026 | Piyush Singh Bhati | Initial additions of SRS and Progress Report |
| `326aea3d` | 02/17/2026 | Piyush Singh Bhati | Updated README |
| `587f63f0` | 02/17/2026 | Piyush Singh Bhati | Fix typos and formatting in README.md |
| `b21ee7ba` | 02/17/2026 | Piyush2005-code | Added the images |
| `c0d6885c` | 02/26/2026 | Darpan Baviskar | Added images_v1 folder |

#### Notes
- SRS documents FR, NFR, BR, DC, and PR requirements for all subsystems.
- Images folder contains architecture block diagrams and memory map figures.
- `images_v1/` added by Darpan Baviskar with updated version of diagrams after review.

---

### 9. `feat/onnx`

**Purpose:** Freestanding ONNX model loading and inference runtime for bare-metal ARM64 — parses a minimal ONNX protobuf, loads model weights into memory via the bump allocator, and runs inference without any OS runtime dependency.  
**Date range:** 02/20/2026 – 03/02/2026  
**Primary Author:** Harshit Saini  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `d8b7372b` | 02/20/2026 | Piyush2005-code | Testing for the bootloader |
| `d8065d01` | 02/27/2026 | Harshit Saini | Implement ONNX model loading and inference runtime |
| `249bfde5` | 03/02/2026 | Harshit Saini | refactor: update embedded ONNX test model with detailed protobuf structure and comprehensive byte-level comments |
| `8c0c4572` | 03/02/2026 | Harshit Saini | Added the project overview and traceability matrix |

#### Key Components
- Freestanding protobuf parser (no `libprotobuf`) — hand-unrolled varint/field tag decoding.
- Embedded test model as a `const uint8_t[]` byte array with full protobuf field comments.
- Traceability matrix linking ONNX feature commits to FR/NFR requirements in SRS.

---

### 10. `net-protocol`

**Purpose:** Reliable UDP (RUDP) protocol implementation for potential inter-node ML task distribution — custom reliability layer over UDP with acknowledgements and retransmission.  
**Date range:** 02/26/2026 – 03/01/2026  
**Primary Author:** Aashma Yadav  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `3150494d` | 01/26/2026 | Piyush2005-code | First commit |
| `16a52a6e` | 02/26/2026 | Piyush Singh Bhati | Updated README for the main branch |
| `f1c131ba` | 03/01/2026 | Aashma Yadav | Initial network protocol (RUDP) implementation |
| `1c0be9bd` | 03/01/2026 | Aashma Yadav | Update README.md |
| `e3b7210d` | 03/01/2026 | Aashma Yadav | Update README.md |

#### Key Components
- `rudp.h` / `rudp.c` — RUDP socket abstraction: `RUDP_Init()`, `RUDP_Send()`, `RUDP_Receive()`, `RUDP_Ack()`.
- Sequence numbering, cumulative acknowledgement, and timeout-based retransmission.
- README clarifications were committed twice (03/01) following peer review feedback.

---

### 11. `scheduler_benchmark`

**Purpose:** Simulation and benchmarking of seven CPU scheduling policies under ML operator workloads, producing quantitative performance reports with matplotlib-generated charts.  
**Date range:** 02/20/2026 – 03/02/2026  
**Authors:** Harshit Saini, Darpan Baviskar  

#### Commit History

| Commit Hash | Date | Author | Message |
|---|---|---|---|
| `d8b7372b` | 02/20/2026 | Piyush2005-code | Testing for the bootloader |
| `07b2b427` | 02/24/2026 | Harshit Saini | test(kernel): implement comprehensive API verification suite |
| `973d0009` | 02/25/2026 | Darpan Baviskar | feat(bench): add ML operator workload definitions and task entry functions |
| `4772f559` | 02/26/2026 | Harshit Saini | feat(sched): implement FCFS, SJF, Round-Robin, HRRN, and Priority policies |
| `297b1b95` | 02/27/2026 | Darpan Baviskar | feat(sched): add Multilevel Queue and Lottery scheduling algorithms |
| `46f6d43e` | 02/28/2026 | Harshit Saini | feat(bench): implement benchmark runner and stats tracking across all policies |
| `e356f5eb` | 03/01/2026 | Darpan Baviskar | docs: add quantitative benchmark report with matplotlib charts |
| `538ecd5e` | 03/02/2026 | Harshit Saini | feat: implement cooperative scheduler with basic memory management and timer services |

#### Scheduling Policies Implemented
| Policy | Author | Notes |
|---|---|---|
| FCFS (First-Come First-Served) | Harshit Saini | Baseline; non-preemptive |
| SJF (Shortest Job First) | Harshit Saini | Non-preemptive; estimated burst time |
| Round-Robin | Harshit Saini | Configurable time quantum |
| HRRN (Highest Response Ratio Next) | Harshit Saini | Aging-aware; prevents starvation |
| Priority Scheduling | Harshit Saini | Static priorities; preemptive variant |
| MLQ (Multilevel Queue) | Darpan Baviskar | Three queues: real-time / interactive / batch |
| Lottery Scheduling | Darpan Baviskar | Probabilistic; ticket-based fairness |

---

## Cross-Branch Commit Timeline

Chronologically ordered milestones across all branches:

| Date | Branch | Author | Milestone |
|---|---|---|---|
| 01/26/2026 | `main` | Piyush Singh Bhati | Repository created |
| 01/26/2026 | `SRS-and-Reports` | Piyush Singh Bhati | SRS branch initialized |
| 01/27/2026 | `unikernel` | Harshit Saini | Unikernel project setup begins |
| 01/29/2026 | `unikernel` | Harshit Saini | Project configuration added |
| 01/30/2026 | `unikernel` | Harshit Saini | Optimized benchmark binary built |
| 01/31/2026 | `unikernel` | Harshit Saini | Unikraft implementation added |
| 02/02/2026 | `unikernel` | Harshit Saini | Test automation scripts added |
| 02/04/2026 | `unikernel` | Harshit Saini | Benchmarks run; analysis report generated |
| 02/11/2026 | `baremetalOS` | Piyush Singh Bhati | Bare-metal boot loop + Makefiles |
| 02/12/2026 | `baremetalOS` | Piyush Singh Bhati | Cross-platform Makefiles finalized |
| 02/15/2026 | `UART_Implementation` | Piyush Singh Bhati | UART PL011 driver implemented |
| 02/16/2026 | `SRS-and-Reports` | Piyush Singh Bhati | SRS and Progress Report added |
| 02/17/2026 | `SRS-and-Reports` | Piyush Singh Bhati | README updated; images added |
| 02/20/2026 | `BootLoader_and_HAL` | Piyush Singh Bhati | Bootloader testing phase begins |
| 02/20/2026 | `feat/onnx` | Piyush Singh Bhati | ONNX branch base established |
| 02/23/2026 | `build/kernel_api` | Piyush Singh Bhati | Kernel API Sprint 1 begins; arch.h added |
| 02/24/2026 | `build/kernel_api` | Piyush Singh Bhati | String library; kmem interface defined |
| 02/24/2026 | `scheduler_benchmark` | Harshit Saini | API verification test suite |
| 02/25/2026 | `build/kernel_api` | Piyush Singh Bhati | Bump allocator implemented; alignment bug fixed |
| 02/25/2026 | `scheduler_benchmark` | Darpan Baviskar | ML workload definitions added |
| 02/26/2026 | `build/kernel_api` | Piyush Singh Bhati | GICv2 driver added; CPU interface bug fixed |
| 02/26/2026 | `net-protocol` | Piyush Singh Bhati | README for net-protocol |
| 02/26/2026 | `SRS-and-Reports` | Darpan Baviskar | Architecture images v1 added |
| 02/26/2026 | `scheduler_benchmark` | Harshit Saini | FCFS, SJF, RR, HRRN, Priority implemented |
| 02/27/2026 | `build/kernel_api` | Piyush Singh Bhati | Generic Timer driver; overflow bug fixed |
| 02/27/2026 | `feat/onnx` | Harshit Saini | ONNX model loading and inference runtime |
| 02/27/2026 | `scheduler_benchmark` | Darpan Baviskar | MLQ + Lottery policies added |
| 02/28/2026 | `scheduler_benchmark` | Harshit Saini | Benchmark runner and stats tracking |
| 03/01/2026 | `build/kernel_api` | Piyush Singh Bhati | Kernel integrates KMEM + linker script update |
| 03/01/2026 | `net-protocol` | Aashma Yadav | RUDP initial implementation + README |
| 03/01/2026 | `scheduler_benchmark` | Darpan Baviskar | Quantitative benchmark report + matplotlib charts |
| 03/02/2026 | `build/kernel_api` | Piyush Singh Bhati | Kernel integrates GIC + Timer; Sprint 1 complete |
| 03/02/2026 | `BootLoader_and_HAL` | Piyush Singh Bhati | Documentation updated |
| 03/02/2026 | `Kernel_API` | Piyush Singh Bhati | Full multithreaded Kernel API delivered |
| 03/02/2026 | `feat/onnx` | Harshit Saini | ONNX protobuf refactored; traceability matrix added |
| 03/02/2026 | `scheduler_benchmark` | Harshit Saini | Cooperative scheduler with memory + timer integration |

---

*End of report. Generated from `git log` history across all 11 branches of https://github.com/Piyush2005-code/MiniOS.*
