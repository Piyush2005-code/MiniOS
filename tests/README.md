# MiniOS Test Suite

This directory contains the complete test suite for MiniOS: host-side Unity tests
(no QEMU) and QEMU-side tests that run on the bare-metal ARM64 kernel image.

---

## Directory Structure

```
tests/
  unity/                  Unity v2.x framework (single-file, no install needed)
    unity.c               Unity implementation
    unity.h               Unity public API
    unity_internals.h     Unity internals
  host/                   Host-side Unity tests (x86_64, compiled with gcc)
    Makefile              Builds and runs all host test suites
    test_types.c          UT-TYPES-001..017
    test_status.c         UT-STAT-001..006
    test_mem.c            UT-MEM-001..045 + CT-MEM-001..007
    test_sched.c          UT-SCHED-001..054 + CT-SCHED-001..014
    test_kapi.c           CT-KAPI-001..004
    stubs/                Minimal host stubs for kernel HAL code
      arch_stub.c         arch_irq_save / arch_irq_restore (no-ops)
      context_stub.c      cpu_context_switch stub
      hal/arch.h          Stub header to prevent ARM64 asm inclusion
      heap_stub.c         Fake 1 MB heap + heap_stub_reset()
      status_stub.c       STATUS_ToString() for host build
      timer_stub.c        HAL_Timer_GetTicks / DelayUs (monotonic counter)
      uart_stub.c         HAL_UART_PutString / PutChar (no-ops)
  qemu/                   QEMU-side tests (ARM64, run inside QEMU)
    test_runner_main.c    Top-level runner; replaces kernel_main when TEST=1
    test_uart.c           UT-UART-001..021
    test_timer.c          UT-TIMER-001..017
    test_mmu.c            UT-MMU-001..017
    test_ctx.c            UT-CTX-001..008
    test_exception.c      CT-EXC-001..004
    test_system.c         ST-BOOT-001..006, ST-INIT-001..005,
                          ST-API-001..005, ST-BENCH-001..010,
                          ST-MEM-STRESS-001..003, ST-STAB-001..003
```

---

## Host Tests (no QEMU required)

Host tests compile against native `gcc` on `x86_64`. The stubs in `host/stubs/`
replace ARM64 HAL functions with no-ops or simple software models so that the
kernel logic (memory manager, scheduler, types) can be exercised without hardware.

### Test Suites

| Target         | File             | Test IDs                              | Count |
|----------------|------------------|---------------------------------------|-------|
| `test_types`   | `test_types.c`   | UT-TYPES-001..017                     | 17    |
| `test_status`  | `test_status.c`  | UT-STAT-001..006                      | 6     |
| `test_mem`     | `test_mem.c`     | UT-MEM-001..045 + CT-MEM-001..007     | 52    |
| `test_sched`   | `test_sched.c`   | UT-SCHED-001..054 + CT-SCHED-001..014 | 68    |
| `test_kapi`    | `test_kapi.c`    | CT-KAPI-001..004                      | 4     |
| **Total**      |                  |                                       | **147** |

### Running Host Tests

```bash
# From the tests/host directory — run ALL suites:
cd tests/host && make test

# Or run individual suites:
make test_types      # UT-TYPES-001..017
make test_status     # UT-STAT-001..006
make test_mem        # UT-MEM-001..045 + CT-MEM-001..007
make test_sched      # UT-SCHED-001..054 + CT-SCHED-001..014
make test_kapi       # CT-KAPI-001..004

# Clean build artifacts:
make clean
```

From the project root:

```bash
make -C tests/host test
```

### Notable Behaviours

- **`test_mem`** reinitialises the fake heap (`heap_stub_reset()` + `KMEM_Init()`)
  before every Unity `setUp()`, ensuring each test starts from a clean state.
- **`test_sched`** (`UT-SCHED-001..027`) exercises the real `SCHED_Init` /
  `THREAD_Create` API. Tests `UT-SCHED-028..054` and `CT-SCHED-001..014` use an
  embedded multi-policy fixture (FCFS, SJF, RR, HRRN, Priority, MLQ, Lottery)
  that lives entirely within `test_sched.c`.
- **Lottery tests** (`UT-SCHED-051..054`, `CT-SCHED-013..014`) use a fixed LCG
  seed of `12345` to guarantee deterministic results.
- **`test_kapi`** stubs out IRQ save/restore and cache-flush operations; tests
  verify behavioural contracts rather than actual hardware effects.

---

## QEMU Tests

QEMU tests build the full kernel with `TEST=1`, which substitutes
`test_runner_main.c` for the normal `kernel_main`. The runner initialises
all HAL subsystems (UART, MMU, Memory, Timer, Scheduler), then calls each
test module in sequence.

### Test Suites (QEMU)

| Module              | File                  | Test IDs                                                                 | Count |
|---------------------|-----------------------|--------------------------------------------------------------------------|-------|
| UART unit tests     | `test_uart.c`         | UT-UART-001..021                                                         | 21    |
| Timer unit tests    | `test_timer.c`        | UT-TIMER-001..017                                                        | 17    |
| MMU unit tests      | `test_mmu.c`          | UT-MMU-001..017                                                          | 17    |
| Context switch      | `test_ctx.c`          | UT-CTX-001..008                                                          | 8     |
| Exception vectors   | `test_exception.c`    | CT-EXC-001..004                                                          | 4     |
| System tests        | `test_system.c`       | ST-BOOT-001..006, ST-INIT-001..005, ST-API-001..005, ST-BENCH-001..010, ST-MEM-STRESS-001..003, ST-STAB-001..003 | 32    |
| **Total**           |                       |                                                                          | **99** |

### Running QEMU Tests

```bash
# Build and run in one step (from project root):
make TEST=1 test_qemu

# Or build then run separately:
make TEST=1
make TEST=1 run
```

### Expected Output Format

```
========================================
  MiniOS Test Suite — QEMU Runner
========================================

--- UT-UART ---
[TEST] UT-UART-001 PASS
[TEST] UT-UART-002 PASS
...
--- UT-TIMER ---
[TEST] UT-TIMER-001 PASS
...
[SUITE] 99 PASS, 0 FAIL
```

QEMU exits with code `0` when all tests pass, `1` when any test fails (via
AArch64 semihosting `SYS_EXIT`).

---

## All-in-One CI Script

```bash
bash scripts/run_tests.sh
```

Exits `0` only if **both** host tests and QEMU tests pass.

---

## Test Naming Convention

| Prefix | Meaning                                         | Example            |
|--------|-------------------------------------------------|--------------------|
| `UT`   | Unit test — a single function or module         | `UT-MEM-007`       |
| `CT`   | Component test — multiple functions together    | `CT-MEM-001`       |
| `ST`   | System test — full boot path, black-box         | `ST-BOOT-001`      |

---

## Prerequisites

| Requirement              | Used by        |
|--------------------------|----------------|
| `gcc` (native x86_64)    | Host tests     |
| `qemu-system-aarch64`    | QEMU tests     |
| `aarch64-linux-gnu-gcc`  | QEMU tests     |
| Unity v2.x (bundled)     | Host tests     |

No external test framework installation is needed — Unity is vendored in
`unity/` as two files (`unity.c` + `unity.h`).
