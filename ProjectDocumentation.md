# MiniOS Project Documentation

## Current Build Overview

### Project Structure
- **Source Code (`src/`)**:
  - `boot/`: Contains assembly files for bootloader and initialization.
  - `hal/`: Implements the Hardware Abstraction Layer (HAL), including memory management unit (MMU) and UART drivers.
  - `kernel/`: Core kernel logic, including the main entry point.
- **Include Files (`include/`)**:
  - `hal/`: Header files for HAL components.
  - `status.h`, `types.h`: General-purpose headers for status codes and type definitions.
- **Build Artifacts (`build/`)**:
  - `kernel.bin`: Compiled binary for the kernel.
  - `kernel.elf`: ELF file for debugging.
  - `obj/`: Object files for modular compilation.
- **Scripts (`scripts/`)**:
  - `run.sh`: Script to execute or test the kernel.

### Build System
- **Makefile**: Automates the build process, compiling source files into object files and linking into the final binary.
- **Toolchain**: `aarch64-elf-gcc` cross-compilation, `-std=c11 -ffreestanding -nostdlib`.

### Current Functionality
- **Bootloader**: Initializes ARM64 processor, parks secondary cores, drops to EL1, zeroes BSS.
- **HAL**: PL011 UART driver (polling mode), identity-mapped MMU with 1 GB blocks, cache enable.
- **Kernel**: Single `kernel_main()` entry point; enters WFE idle loop after boot.

---

## Kernel API — Design Notes (Sprint 1)

### Objective
Define a minimal, layered Kernel API to form the foundation for future process management and ML-inference scheduling. The API is intentionally small — expose only what is needed to support the SRS deliverables for this sprint.

### Design Principles
1. **No dynamic linking** — all components compiled together as a unikernel.
2. **Status codes everywhere** — every public function returns `Status`.
3. **Layered build-up** — HAL extensions first, then libc-replacement utilities, then memory management.
4. **ARM64 cooperative model** — interrupt handling deferred until HAL layer is stable.

### Planned Modules (this sprint)
| Module | Header | Purpose |
|--------|--------|---------|
| ARM64 helpers | `hal/arch.h` | Inline assembly for IRQ control, barriers, EL read |
| String utilities | `lib/string.h` | Freestanding `memset`, `memcpy`, `strlen` |
| Memory manager | `kernel/kmem.h` | Bump allocator for kernel heap |
| GIC driver | `hal/gic.h` | GICv2 interrupt controller init / enable / disable |
| Timer driver | `hal/timer.h` | ARM Generic Timer tick and microsecond delay |

### Out of Scope (next sprint)
- Thread/process creation and context switching
- Arena and pool allocators
- Scheduler

### Memory Layout (planned)
```
RAM origin: 0x40000000
.text / .rodata / .data / .bss  → low addresses
Heap  → after BSS, ~500 MB for ML tensor buffers
Stack → near top of 512 MB RAM, grows downward
```

### Status Codes to Add
- `STATUS_ERROR_POOL_EXHAUSTED` — for future allocator work

---

## Development Log

### Feb 23 — arch.h (ARM64 helpers)
- Created `include/hal/arch.h`.
- Provides `arch_enable_irq()`, `arch_disable_irq()`, `arch_irq_save()`,
  `arch_irq_restore()`, `arch_dsb()`, `arch_isb()`, `arch_get_el()`.
- No wfe/wfi helpers yet; those will be added when the idle thread concept
  is introduced in a later sprint.
