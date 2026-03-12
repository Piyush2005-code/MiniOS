# MiniOS Test Suite

This directory contains the complete test suite for MiniOS.

## Structure

```
tests/
  unity/          Unity single-file test framework (v2.x)
  host/           Host-side Unity tests (x86_64, no QEMU needed)
    stubs/        Minimal stubs for kernel code compiled on host
    Makefile      Builds and runs all host tests
  qemu/           QEMU-side tests (ARM64, run inside QEMU)
    test_uart.c        UT-UART-001..021
    test_timer.c       UT-TIMER-001..017
    test_mmu.c         UT-MMU-001..017
    test_ctx.c         UT-CTX-001..008
    test_exception.c   CT-EXC-001..004
    test_system.c      ST-BOOT, ST-INIT, ST-API, ST-BENCH, ST-MEM, ST-STAB
    test_runner_main.c Top-level runner (replaces kernel_main when TEST=1)
```

## Host Tests (no QEMU required)

Host tests cover: `types`, `status`, `mem` (KMEM API), `sched` (thread + embedded policy fixtures), and `kapi`.

```bash
cd tests/host && make test         # Run ALL host test suites
make test_types                    # UT-TYPES-001..017
make test_status                   # UT-STAT-001..006
make test_mem                      # UT-MEM-001..045 + CT-MEM-001..007
make test_sched                    # UT-SCHED-001..054 + CT-SCHED-001..014
make test_kapi                     # CT-KAPI-001..004
```

Or from the project root:

```bash
make -C tests/host test
```

## QEMU Tests

QEMU tests run the kernel in QEMU with a test runner substituting `kernel_main`:

```bash
# Build and run in one step:
make TEST=1 test_qemu

# Or build then run separately:
make TEST=1
make TEST=1 run
```

**Expected output format:**

```
[TEST] UT-UART-001 PASS
[TEST] UT-UART-002 PASS
...
[SUITE] 82 PASS, 0 FAIL
```

QEMU exits with code `0` on all-pass, `1` on any failure.

## All-in-One CI Script

```bash
bash scripts/run_tests.sh
```

Exits `0` only if both host tests and QEMU tests pass.

## Test Naming Convention

| Prefix | Pattern | Example |
|--------|---------|---------|
| UT     | Unit test for a single function | `UT-MEM-007` |
| CT     | Component test (multiple functions) | `CT-MEM-001` |
| ST     | System test (full boot, black-box) | `ST-BOOT-001` |

## Notes

- Host tests use Unity (single `unity.c` + `unity.h` — no installation needed).
- `test_sched.c` includes self-contained fixtures for multi-policy scheduling
  algorithms (FCFS, SJF, RR, HRRN, Priority, MLQ, Lottery). These policy
  algorithms are independent of the actual `thread.c` implementation.
- Lottery tests use a fixed seed (`12345`) per the test spec.
- QEMU tests require `qemu-system-aarch64` and an ARM64 cross-compiler.
