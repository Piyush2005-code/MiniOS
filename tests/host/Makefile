# ============================================================================
# Host Test Makefile — MiniOS
# Compiles and runs all host-side Unity tests on x86_64 with native gcc.
# No QEMU required.
# ============================================================================

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g \
           -Istubs \
           -I../../include \
           -I../unity \
           -DUNITY_INCLUDE_DOUBLE

UNITY_SRC  = ../unity/unity.c

STUB_SRCS  = stubs/uart_stub.c \
             stubs/timer_stub.c \
             stubs/arch_stub.c \
             stubs/heap_stub.c \
             stubs/status_stub.c \
             stubs/context_stub.c

KERNEL_MEM   = ../../src/kernel/kmem.c
KERNEL_THRD  = ../../src/kernel/thread.c
KERNEL_STR   = ../../src/lib/string.c

.PHONY: all test test_types test_status test_mem test_sched test_kapi clean

# ---- Default: build and run all tests ----
all: test

test: test_types test_status test_mem test_sched test_kapi

# ---- Individual targets ----

test_types: test_types.bin
	./test_types.bin

test_types.bin: test_types.c $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_status: test_status.bin
	./test_status.bin

test_status.bin: test_status.c $(UNITY_SRC) stubs/status_stub.c $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_mem: test_mem.bin
	./test_mem.bin

test_mem.bin: test_mem.c $(UNITY_SRC) $(STUB_SRCS) \
              $(KERNEL_MEM) $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_sched: test_sched.bin
	./test_sched.bin

test_sched.bin: test_sched.c $(UNITY_SRC) $(STUB_SRCS) \
                $(KERNEL_MEM) $(KERNEL_THRD) $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_kapi: test_kapi.bin
	./test_kapi.bin

test_kapi.bin: test_kapi.c $(UNITY_SRC) stubs/uart_stub.c \
               stubs/timer_stub.c stubs/arch_stub.c $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

clean:
	rm -f *.bin
