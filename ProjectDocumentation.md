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

### Feb 24 (morning) — String utility library
- Created `include/lib/string.h` and `src/lib/string.c`.
- Provides `memset`, `memcpy`, `strlen` — needed by future memory manager
  and kernel internals (no libc available in freestanding build).
- Added `src/lib/string.c` to Makefile sources.

### Feb 24 (afternoon) — kmem.h interface
- Created `include/kernel/kmem.h`.
- Defines bump allocator interface only (no arena/pool this sprint).
- `KMEM_Init`, `KMEM_Alloc(size, alignment)`, `KMEM_GetFreeSpace`, `KMEM_GetStats`.
- Uses linker symbols `_heap_start` / `_heap_end` — requires linker script update.
- Implementation to follow tomorrow.

### Feb 25 — Bump allocator implementation + bug fix
- Initially implemented `src/kernel/kmem.c` with incorrect alignment
  formula: `ptr + alignment` when it should be
  `(ptr + alignment - 1) & ~(alignment - 1)`.
  Bug caused allocations with alignment > 8 to land on wrong addresses.
- Fixed same day in a follow-up commit.  Added brief manual verification
  using UART output to confirm 64-byte aligned allocations.

### Feb 26 — GICv2 driver + CPU interface fix
- Created `include/hal/gic.h` (GICv2 interface: init, enable, disable, ack, eoi).
- Initial `gic.c` was missing GICC_PMR and GICC_BPR initialisation, so
  **no interrupts reached the processor** despite the distributor being armed.
- Fixed afternoon of same day: set `GICC_PMR = 0xFF` and `GICC_BPR = 0`.

### Feb 27 — ARM Generic Timer driver + overflow fix
- Created `include/hal/timer.h` and `src/hal/timer.c`.
- `HAL_Timer_Init` reads `CNTFRQ_EL0` (62.5 MHz on QEMU virt/cortex-a53).
- Initial `HAL_Timer_Enable` multiplied `timer_freq * interval_us` as
  `uint32_t`, causing silent overflow — 10 ms interval was ~15× too short.
- Fixed evening of same day: promoted intermediate to `uint64_t`.
- `HAL_Timer_DelayUs` / `HAL_Timer_GetElapsedUs` / `HAL_Timer_Reload` added.

**Note:** Timer callbacks / system tick counter are out of scope for this
sprint.  The caller integrates via the IRQ handler directly.

### Mar 1 — Memory manager integration + linker script update
- `linker.ld` updated:
  - Stack moved from 8 MB offset to `ORIGIN(RAM) + LENGTH(RAM) - 64 KB`
    (near top of 512 MB RAM).
  - `_heap_end` = `_stack_top - 256 KB` to leave a guard for stack overflow.
  - Available heap grows from ~7.9 MB to ~499 MB.
- `kernel_main` updated to call `KMEM_Init()` after MMU init.
- Build confirmed: `KMEM` reports ~523 944 KB free heap in QEMU.
- GIC and Timer init will follow in the next commit.
