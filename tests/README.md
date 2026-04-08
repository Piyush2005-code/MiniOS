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
| `test_string`  | `test_string.c`  | UT-STR-001..012                       | 12    |
| `test_cmd`     | `test_cmd_component.c` | CT-CMD-001..011                 | 11    |
| `test_fs_integration` | `test_fs_integration.c` | IT-FS-001..006           | 6     |
| `test_network` | `test_network.c` | CT/IT/UT-NET-001..012                 | 12    |
| `test_onnx`    | `test_onnx_runtime_parser.c` | CT/UT/IT-ONNX-001..014 (non-contiguous IDs) | 14    |
| **Total**      |                  |                                       | **202** |

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
make test_string     # UT-STR-001..012
make test_cmd        # CT-CMD-001..011
make test_fs_integration  # IT-FS-001..006
make test_network    # CT/IT/UT-NET-001..012
make test_onnx       # ONNX parser/runtime regressions (001..014)

# Known-gap (expected-fail) suite that captures general-purpose shell
# expectations not implemented in MiniOS command parsing:
make test_expected_fail

# Clean build artifacts:
make clean
```

From the project root:

```bash
make -C tests/host test
```

### Current Bug-Revealing Failures (09-Apr-2026)

The host suite now includes several edge-case tests designed to expose real
defects in parsing and validation logic. These are currently failing by design
until the underlying code is fixed.

1. `CT-CMD-007` (`test_cmd_component.c`): argument cap truncation bug.
  - Failure: expected last capped token `k`, actual token `k l m`.
  - Bug to fix: tokenizer should stop and terminate tokenization cleanly at
    `CMD_MAX_ARGS` instead of returning a merged tail token.
2. `CT-CMD-008` (`test_cmd_component.c`): empty command names accepted.
  - Failure: registration returns success for `""`.
  - Bug to fix: reject zero-length command names in `CMD_Register`.
3. `CT-CMD-009` (`test_cmd_component.c`): whitespace in command names accepted.
  - Failure: registration returns success for names containing spaces.
  - Bug to fix: enforce token-safe command names (no whitespace separators).
4. `CT-CMD-010` (`test_cmd_component.c`): exact duplicate names accepted.
  - Failure: second registration of same name returns success.
  - Bug to fix: prevent duplicate command table entries.
5. `CT-CMD-011` (`test_cmd_component.c`): case-insensitive duplicates accepted.
  - Failure: `CaseDup` and `casedup` can both register.
  - Bug to fix: duplicate check must be case-insensitive.
6. `UT-NET-010` (`test_network.c`): null handler accepted by `UDP_Bind`.
  - Failure: expected reject (`-1`), actual success (`0`).
  - Bug to fix: input validation should reject null callback pointers.
7. `UT-NET-011` (`test_network.c`): UDP header length underflow accepted.
  - Failure: malformed datagram is dispatched to handler.
  - Bug to fix: enforce `udp.length >= sizeof(UDPHdr_t)` before dispatch.
8. `UT-NET-012` (`test_network.c`): UDP declared length overflow accepted.
  - Failure: datagram whose declared length exceeds frame length is dispatched.
  - Bug to fix: enforce `udp.length <= received_frame_length`.
9. `UT-ONNX-RUNTIME-013` (`test_onnx_runtime_parser.c`): prefix matching bug.
  - Failure: `ExecuteUpTo("node")` matches `"node_exact"` and returns success.
  - Bug to fix: require exact string match for node lookup.
10. `UT-ONNX-RUNTIME-014` (`test_onnx_runtime_parser.c`): schedule membership bug.
  - Failure: `ExecuteUpTo` reports success when target node is not in schedule.
  - Bug to fix: return error if target node is absent from execution schedule.

Until these defects are resolved, `make -C tests/host test` is expected to fail.
Use individual targets when working pass-only areas (for example
`test_string` and `test_fs_integration`).

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

## Unified Test Script

```bash
bash run_tests.sh
```

Default run executes all host UT/CT/IT suites and verifies the expected-fail gap
case. Use the QEMU-inclusive mode when needed:

```bash
bash run_tests.sh --with-qemu
```

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
