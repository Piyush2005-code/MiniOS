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
           -DUNITY_INCLUDE_DOUBLE \
           -DMINIOS_TYPES_H \
           -include stdint.h \
           -include stddef.h \
           -include stdbool.h \
           "-DREG32(a)=(*((volatile uint32_t*)(uintptr_t)(a)))" \
           "-DREG64(a)=(*((volatile uint64_t*)(uintptr_t)(a)))"

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
KERNEL_CMD   = ../../src/kernel/cmd.c
KERNEL_FS    = ../../src/kernel/fs_cmds.c
NET_UDP      = ../../src/net/udp.c
NET_IPV4     = ../../src/net/ipv4.c
NET_SFU      = ../../src/net/sfu.c
ONNX_TYPES   = ../../src/onnx/onnx_types.c
ONNX_GRAPH   = ../../src/onnx/onnx_graph.c
ONNX_LOADER  = ../../src/onnx/onnx_loader.c
ONNX_RUNTIME = ../../src/onnx/onnx_runtime.c

.PHONY: all test test_types test_status test_mem test_sched test_kapi \
    test_string test_cmd test_fs_integration test_network test_onnx \
    test_expected_fail test_cmd_gap \
    clean

# ---- Default: build and run all tests ----
all: test

test: test_types test_status test_mem test_sched test_kapi \
    test_string test_cmd test_fs_integration test_network test_onnx

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

test_string: test_string.bin
	./test_string.bin

test_string.bin: test_string.c $(UNITY_SRC) $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_cmd: test_cmd.bin
	./test_cmd.bin

test_cmd.bin: test_cmd_component.c $(UNITY_SRC) \
              stubs/uart_capture_stub.c stubs/cmd_deps_stub.c $(KERNEL_CMD)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_fs_integration: test_fs_integration.bin
	./test_fs_integration.bin

test_fs_integration.bin: test_fs_integration.c $(UNITY_SRC) \
                         stubs/uart_capture_stub.c stubs/cmd_deps_stub.c \
                         stubs/ulfs_stub.c $(KERNEL_CMD) $(KERNEL_FS) $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_network: test_network.bin
	./test_network.bin

test_network.bin: test_network.c $(UNITY_SRC) \
                 stubs/uart_capture_stub.c stubs/timer_stub.c stubs/net_stack_stub.c \
                 $(NET_UDP) $(NET_IPV4) $(NET_SFU) $(KERNEL_STR)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_onnx: test_onnx.bin
	./test_onnx.bin

test_onnx.bin: test_onnx_runtime_parser.c $(UNITY_SRC) \
              stubs/uart_capture_stub.c stubs/timer_stub.c stubs/arch_stub.c \
              stubs/heap_stub.c stubs/thread_yield_stub.c stubs/status_stub.c \
              $(KERNEL_MEM) $(KERNEL_STR) \
              $(ONNX_TYPES) $(ONNX_GRAPH) $(ONNX_LOADER) $(ONNX_RUNTIME)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

test_expected_fail: test_cmd_gap

test_cmd_gap: test_cmd_gap.bin
	./test_cmd_gap.bin

test_cmd_gap.bin: test_cmd_gap.c $(UNITY_SRC) \
                  stubs/uart_capture_stub.c stubs/cmd_deps_stub.c $(KERNEL_CMD)
	$(CC) $(CFLAGS) $^ -o $@

# ---------------------------------------------------------------------------

clean:
	rm -f *.bin
