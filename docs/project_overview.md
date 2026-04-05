# MiniOS — ML Inference Unikernel for ARM64

> **Project**: Specialized unikernel OS for machine learning inference on ARM64 embedded platforms  
> **Repository**: https://github.com/Piyush2005-code/MiniOS.git  
> **Team**: Piyush Singh Bhati (Tech Lead), Darpan Baviskar (Integration Lead), Harshit (ML Runtime), Aashma (System Components)  
> **Submitted to**: Dr. Romi Banerjee  

---

## System Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    MiniOS ML Inference Unikernel                    │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐  │
│  │  ONNX Graph   │  │  Execution   │  │  Performance Monitoring  │  │
│  │  Processing   │  │  Engine      │  │  & Telemetry             │  │
│  │  Pipeline     │  │  (Operators) │  │                          │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────────────┘  │
│         │                 │                                         │
│  ┌──────┴─────────────────┴─────────────────────────────────────┐  │
│  │              Cooperative Scheduler / Runtime                   │  │
│  └──────────────────────────┬────────────────────────────────────┘  │
│                             │                                       │
│  ┌──────────────┐  ┌───────┴──────┐  ┌──────────────────────────┐  │
│  │  UART/RUDP   │  │   Memory     │  │  Hardware Abstraction    │  │
│  │  Protocols   │  │   Manager    │  │  Layer (HAL)             │  │
│  └──────────────┘  └──────────────┘  └──────────────────────────┘  │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  ARM64 Hardware: Cortex-A53/A72 │ MMU │ NEON SIMD │ PL011 UART    │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Branch-by-Branch Implementation Status

### 1. `BootLoader_and_HAL` — ARM64 Bootloader & Hardware Abstraction Layer

**Owner**: Piyush Singh Bhati  
**Status**: ✅ Complete  
**SRS Coverage**: FR-001 through FR-005, FR-021, DC-001, SR-003, PR-006

This branch contains the production-quality bootloader and HAL for the QEMU virt machine. It is the foundation upon which all other modules are built.

#### Boot Sequence (`src/boot/boot.S`)
The ARM64 entry point handles the complete processor initialization:
- **Secondary core parking**: Only core 0 proceeds; others enter low-power WFE loop
- **Exception Level transition**: Detects current EL (EL3/EL2/EL1) and drops down to EL1 through proper ERET sequences
- **EL3→EL2**: Configures SCR_EL3 (NS=1, RW=1), sets SPSR for EL2h mode
- **EL2→EL1**: Configures HCR_EL2 (RW=1 for AArch64), disables FP/SIMD traps, enables timer access from EL1, initializes SCTLR_EL1
- **At EL1**: Enables FP/SIMD via CPACR_EL1, sets up SP_EL1 from `_stack_top`, zeros `.bss` section, branches to `kernel_main()`

#### Exception Vector Table (`src/boot/vectors.S`)
- Full ARMv8-A vector table with 16 entries (4 groups × 4 vectors), 2KB-aligned
- Each vector entry is 128 bytes, using macro-based stubs
- Common handler saves ESR_EL1, ELR_EL1, FAR_EL1 and calls `HAL_Exception_Handler()` in C
- Groups: Current EL SP0, Current EL SPx, Lower EL AArch64, Lower EL AArch32

#### MMU & Cache Management (`src/hal/mmu.c`, `include/hal/mmu.h`)
- **Page tables**: 4KB-aligned L1 table with 1GB block descriptors
  - Entry 0 (`0x00000000–0x3FFFFFFF`): Device-nGnRnE memory (UART, GIC MMIO)
  - Entry 1 (`0x40000000–0x7FFFFFFF`): Normal Write-Back cacheable RAM
- **MAIR_EL1**: Attr0 = Device-nGnRnE (0x00), Attr1 = Normal WB (0xFF)
- **TCR_EL1**: T0SZ=32 (4GB VA), 4KB granule, Inner Shareable, WB cache policy
- **Cache enable**: SCTLR_EL1 bits M (MMU), C (D-cache), I (I-cache)
- API: `HAL_MMU_Init()`, `HAL_MMU_InvalidateTLB()`, `HAL_MMU_CleanInvalidateDCache()`

#### PL011 UART Driver (`src/hal/uart.c`, `include/hal/uart.h`)
- Base address: `0x09000000` (QEMU virt PL011)
- Configuration: 115200 baud, 8N1, FIFO enabled, polling mode
- API: `HAL_UART_Init()`, `HAL_UART_PutChar()`, `HAL_UART_GetChar()`, `HAL_UART_PutString()`, `HAL_UART_PutHex()`, `HAL_UART_PutDec()`

#### Kernel Entry (`src/kernel/main.c`)
- Initializes UART → Installs exception vectors → Initializes MMU → Enters idle loop
- Status reporting system with `STATUS_ToString()` covering 16 error codes
- Exception handler prints ESR/ELR/FAR registers and halts

#### Supporting Infrastructure
- **`include/types.h`**: Fixed-width types without libc dependency (`uint8_t` through `uint64_t`, `size_t`, `bool`, `REG32`/`REG64` macros)
- **`include/status.h`**: Unified `Status` enum for all error handling
- **`linker.ld`**: Memory layout at `0x40000000` (QEMU virt RAM start), sections: `.text` → `.rodata` → `.data` → `.bss`, stack at `0x40800000`
- **`Makefile`**: Cross-compilation with `aarch64-linux-gnu-gcc`, QEMU launch script
- **`scripts/run.sh`**: QEMU command to boot `kernel.bin`

#### Final File Tree
```
├── Makefile
├── linker.ld
├── scripts/run.sh
├── include/
│   ├── types.h
│   ├── status.h
│   └── hal/
│       ├── mmu.h
│       └── uart.h
└── src/
    ├── boot/
    │   ├── boot.S
    │   └── vectors.S
    ├── hal/
    │   ├── mmu.c
    │   └── uart.c
    └── kernel/
        └── main.c
```

---

### 2. `baremetalOS` — Early Prototype (Baremetal Loop)

**Owner**: Piyush Singh Bhati  
**Status**: ✅ Complete (superseded by `BootLoader_and_HAL`)  
**SRS Coverage**: FR-001, FR-002, FR-021

This was an earlier prototype that validated the basic ARM64 boot flow before the full HAL was implemented.

#### Key Differences from Production HAL
- Uses `semihosting` (HLT instruction) for debug output alongside UART
- Simpler MMU with L1→L2 table hierarchy (2MB blocks covering 64MB)
- Single-file boot assembly (`start.S`) combining boot + exception vectors
- Provided Makefiles for Ubuntu, MacOS, and Arch Linux (team uses different OSes)
- `main.c` is a simple print-and-loop demonstrating UART direct register access

---

### 3. `UART_Implementation` — Communication Protocol Layer

**Owner**: Piyush Singh Bhati  
**Status**: ✅ Complete  
**SRS Coverage**: FR-021, FR-022, FR-025, SR-004

Implements the binary UART protocol defined in SRS Appendix F for model loading and inference result retrieval.

#### Protocol Format
```
[START_BYTE=0xAA] [LENGTH] [COMMAND] [PAYLOAD...] [CRC-8]
```

#### Implementation (`uart-protocol/`)
- **`uart_protocol.h`**: Protocol constants, command codes (0x01–0x06), function prototypes
- **`uart_protocol.c`**: 
  - `uart_pack_message()` — Serializes command + payload into wire format with CRC
  - `uart_unpack_message()` — Deserializes and validates START byte, CRC, bounds
  - `uart_calculate_crc()` — XOR-8 checksum over all frame bytes
- **`test_protocol.c`**: Unit tests for pack/unpack round-trip, CRC validation, edge cases

#### Supported Commands
| Code | Command | Purpose |
|------|---------|---------|
| 0x01 | `CMD_LOAD_MODEL` | Upload ONNX model binary |
| 0x02 | `CMD_SET_INPUT` | Set inference input tensor |
| 0x03 | `CMD_RUN_INFERENCE` | Trigger inference execution |
| 0x04 | `CMD_GET_RESULTS` | Retrieve inference output |
| 0x05 | `CMD_SYSTEM_STATUS` | Query system health |
| 0x06 | `CMD_CONFIG_UPDATE` | Update runtime configuration |

---

### 4. `feat/onnx` — ONNX Runtime & Graph Processing

**Owner**: Harshit Saini  
**Status**: 🔄 In Progress (core complete, Conv/Pool stubs)  
**SRS Coverage**: FR-006 through FR-015

This is the ML brain of the unikernel — parsing ONNX models, building computation graphs, scheduling operator execution, and running inference.

#### ONNX Type System (`include/onnx/onnx_types.h`)
- **Data types**: `ONNX_DataType` enum (FLOAT32, UINT8, INT8, INT16, INT32, INT64, FLOAT64)
- **Tensor**: Name, dtype, shape (up to 8 dims), data pointer, size, initializer flag
- **Operator types**: 19 operators defined (`ONNX_OperatorType` enum)
- **Node**: Name, op_type, inputs/outputs (up to 16 each), attributes (kernel_shape, strides, pads, dilations, axis, group), scheduling metadata (exec_order, priority, dependency tracking), performance counters (exec_time_us, exec_count)
- **Graph**: Up to 256 nodes, 512 tensors, execution schedule array, tensor memory pool with usage tracking
- **InferenceContext**: Graph pointer, workspace, execution statistics

#### Graph Management (`include/onnx/onnx_graph.h`, `src/onnx/onnx_graph.c`)
- **Lifecycle**: `ONNX_Graph_Init()`, `ONNX_Graph_Cleanup()`
- **Loading**: `ONNX_Graph_LoadFromMemory()`, `ONNX_Graph_LoadEmbedded()`
- **Tensor ops**: `ONNX_Graph_CreateTensor()`, `ONNX_Graph_FindTensor()`, `ONNX_Graph_AllocateTensor()`
- **Node ops**: `ONNX_Graph_AddNode()`, `ONNX_Node_AddInput()`, `ONNX_Node_AddOutput()`, `ONNX_Node_SetPriority()`
- **Scheduling**: `ONNX_Graph_BuildDependencies()`, `ONNX_Graph_GenerateSchedule()` (topological sort), `ONNX_Graph_GenerateCustomSchedule()` (priority-based), `ONNX_Graph_SetCustomSchedule()` (manual)
- **Introspection**: `ONNX_Graph_Print()`, `ONNX_Graph_PrintStats()`, `ONNX_Graph_Validate()`

#### Model Loading (`include/onnx/onnx_loader.h`, `src/onnx/onnx_loader.c`)
- **Formats**: Protobuf (`ONNX_LoadProtobuf`), Custom binary (`ONNX_LoadCustomBinary`), Embedded C array (`ONNX_LoadEmbedded`)
- **Custom binary format**: `ONNX_CustomHeader` with magic `0x4F4E4E58`, fast embedded-friendly parsing
- **Conversion tools**: `scripts/convert_onnx_to_binary.py`, `src/onnx/generate_onnx.py`

#### Inference Runtime (`include/onnx/onnx_runtime.h`, `src/onnx/onnx_runtime.c`)
- **Execution**: `ONNX_Runtime_Inference()` — full graph execution following schedule
  - Validates input count/sizes, copies input data, executes nodes in order, copies outputs
- **Partial execution**: `ONNX_Runtime_ExecuteUpTo()` — run graph until a named node
- **Node dispatch**: `ONNX_Runtime_ExecuteNode()` — dispatches to operator-specific implementations
- **Operator implementations** (currently implemented):
  - ✅ `ONNX_Execute_Arithmetic()` — Add, Sub, Mul, Div (element-wise float32)
  - ✅ `ONNX_Execute_MatMul()` — Matrix multiplication
  - ✅ `ONNX_Execute_ReLU()` — ReLU activation
  - ⏳ `ONNX_Execute_Conv()` — Stub (returns NOT_SUPPORTED)
  - ⏳ `ONNX_Execute_Pool()` — Stub (returns NOT_SUPPORTED)
- **Profiling**: `ONNX_Runtime_GetStats()`, `ONNX_Runtime_ResetStats()`, `ONNX_Runtime_PrintProfile()`

#### Embedded Test Model (`include/test_model.h`)
- Pre-compiled ONNX model as C byte array with detailed protobuf structure comments
- Used for testing without filesystem dependency

---

### 5. `net-protocol` — Reliable UDP Network Protocol (RUDP)

**Owner**: Aashma Yadav  
**Status**: ✅ Complete  
**SRS Coverage**: FR-021, FR-022, FR-023, FR-025, SR-004, DC-002

Implements a purpose-built network protocol for ML inference communication, replacing raw UDP/TCP with selective reliability.

#### Why RUDP?
- **Raw UDP**: No delivery guarantee — unacceptable for model/result frames
- **Full TCP**: 3-way handshake + congestion control — too complex for bare-metal unikernel
- **RUDP**: Selective reliability per command class — ML control is reliable, telemetry is best-effort

#### Protocol Design
```
┌─────────────────────────────────────────────────────────────┐
│  RELIABLE (ML control): DATA[seq=N, RELIABLE] → ACK[N]     │
│  BEST-EFFORT (status):  DATA[seq=N] → (no ACK)             │
│  FRAGMENTATION (models): FRAG[seq=N] → ACK → ... → END     │
└─────────────────────────────────────────────────────────────┘
```

#### Components
- **RUDP Core** (`src/net/rudp.c`, `include/net/rudp.h`):
  - Session management: `RUDP_OpenSession()`, `RUDP_CloseSession()`, `RUDP_Poll()`
  - Data transfer: `RUDP_Send()`, `RUDP_SendLarge()` (with fragmentation), `RUDP_Receive()`
  - Retransmit window with configurable timeout and max retries
  - All buffers statically allocated (DC-002 compliant)
- **Ethernet Driver** (`src/drivers/eth_driver.c`, `include/net/eth_driver.h`):
  - Abstracts QEMU virtio-net and SMSC LAN9118
  - API: `ETH_Init()`, `ETH_Send()`, `ETH_Recv()`, `ETH_TxReady()`, `ETH_RxAvailable()`
- **CRC-16** (`src/net/crc16.c`, `include/net/crc16.h`):
  - 16-bit CRC for frame integrity (stronger than UART CRC-8)
- **Network Types** (`include/net/net_types.h`):
  - Frame format, RUDP header, session state, statistics structures
  - Command codes shared with UART protocol
- **Python Test Client** (`scripts/rudp_client.py`): For development testing
- **Unit Tests** (`tests/test_rudp.c`): Session, send/recv, fragmentation tests

---

### 6. `unikernel` — Benchmark Suite (Unikraft vs Linux VM)

**Owner**: Harshit Saini  
**Status**: ✅ Complete  
**SRS Coverage**: PR-001, PR-002, PDR-001, BR-005

Comprehensive benchmarking infrastructure comparing ML inference performance between Unikraft unikernels and Ubuntu Linux VMs.

#### Benchmark Programs
- **`src/neural_network_benchmark.c`**: Simple 784→128→10 neural network (MNIST-like) with ReLU + Softmax
- **`src/unikraft_benchmark.c`**: Same network optimized for Unikraft with `rdtscp` timing
- **`src/multithreaded_benchmark.c`**: Multi-threaded variant for scalability testing
- **`src/optimized_parallel_benchmark.c`**: SIMD-aligned, loop-unrolled variant
- **`src/numpy_style_benchmark.c`**: Matrix operation benchmark in NumPy-like style

#### Cooperative Scheduler (`src/cooperative_scheduler.h`)
- Header-only implementation using POSIX `ucontext` (makecontext/swapcontext)
- `coop_sched_init()`, `coop_sched_spawn()`, `coop_sched_run()`, `coop_yield()`
- Up to 16 concurrent tasks, 256KB default stack, round-robin scheduling
- Sub-microsecond context switches, no kernel involvement

#### Infrastructure
- **Unikraft Config** (`unikraft/Kraftfile`, `unikraft/src/main.c`): Unikraft deployment
- **Ubuntu VM** (`ubuntu_vm/`): Cloud-init automated VM setup with benchmarks
- **Runners**: `run_unikraft_qemu.sh`, `run_ubuntu_qemu.sh`, `run_comparison.sh`
- **Analysis**: `generate_graphs.py` (1000+ lines), `generate_comparison_graphs.py` — produces latency P50/P99, throughput, scaling, heatmap graphs
- **Results**: JSON results for 1/2/4 vCPU configurations, pre-generated PNG graphs

---

### 7. `build` — Directory Restructure

**Owner**: Piyush Singh Bhati  
**Status**: ✅ Complete  
**SRS Coverage**: DC-001

Restructured the repository to match the SRS-proposed directory layout (`/src`, `/include`, `/build`, `/scripts`). Built on top of the `BootLoader_and_HAL` branch.

---

### 8. `SRS-and-Reports` — Documentation

**Owner**: Piyush Singh Bhati, Darpan Baviskar  
**Status**: ✅ Complete  
**SRS Coverage**: DOC-001

Contains the Software Requirements Specification (v2.0/v3.0) LaTeX source, progress reports, and UML diagrams (component, deployment, use case, activity, class, sequence, timing diagrams).

---

## Current Implementation Coverage vs SRS

| SRS Requirement Group | Status | Branch(es) |
|----------------------|--------|------------|
| **FR-001–005**: Hardware Management | ✅ Complete | `BootLoader_and_HAL` |
| **FR-006–010**: Graph Processing | 🔄 Core complete | `feat/onnx` |
| **FR-011–015**: Execution Engine | 🔄 Core complete (Conv/Pool stubs) | `feat/onnx` |
| **FR-016–020**: Memory Management | ⏳ Not started | — |
| **FR-021–025**: Communication | ✅ Complete (UART + RUDP) | `UART_Implementation`, `net-protocol` |
| **FR-026–030**: Performance Monitoring | ⏳ Partial (profiling in runtime) | `feat/onnx`, `unikernel` |
| **PR-001–006**: Performance | ✅ Benchmarked | `unikernel` |
| **PDR-001–005**: Predictability | 🔄 Partial | `unikernel` |
| **SR-001–005**: Security | 🔄 Partial (CRC, MMU) | `BootLoader_and_HAL`, `UART_Implementation` |

---

## What Needs to Be Built Next

### 🔴 Critical Missing Components

1. **Static Memory Allocator** (FR-016 through FR-020)
   - Tensor pool allocator with 64-byte cache-line alignment
   - Tensor lifetime analysis and memory reuse
   - Memory usage statistics and peak tracking

2. **Conv2D / Pooling Operators** (FR-013)
   - Currently stubs in `onnx_runtime.c`
   - Need NEON SIMD-optimized implementations

3. **Integration of All Branches**
   - Merge `BootLoader_and_HAL` + `feat/onnx` + `UART_Implementation` + `net-protocol` into a working system

### 🟡 High Priority

4. **Minimal Kernel API** (replacing system calls)
5. **Full Scheduler Integration** (cooperative scheduler from `unikernel` branch into MiniOS kernel)
6. **Performance Counters** (FR-026 through FR-030)

---

## Minimal Kernel API Design (Replacing System Calls)

Since MiniOS is a **unikernel with a single address space** (no user/kernel boundary), traditional system calls via `SVC`/`HVC` are unnecessary. Instead, the kernel exposes a **direct function-call API** that application-level code (the ML runtime) invokes as regular C function calls with zero overhead.

### Proposed Kernel API Layers

```c
/* ═══════════════════════════════════════════════════════════════════
 *  LAYER 1: Hardware Abstraction (hal/)
 *  Direct hardware access — already implemented
 * ═══════════════════════════════════════════════════════════════════ */

// Timer API (FR-005)
uint64_t    KAPI_Timer_GetTicks(void);          // Read ARM Generic Timer
uint64_t    KAPI_Timer_GetFrequency(void);      // Timer frequency (Hz)
uint64_t    KAPI_Timer_TicksToUs(uint64_t t);   // Convert to microseconds
void        KAPI_Timer_BusyWaitUs(uint64_t us); // Busy-wait delay

// Interrupt Control (FR-004)
void        KAPI_IRQ_Disable(void);             // DAIF mask
void        KAPI_IRQ_Enable(void);              // DAIF unmask
uint64_t    KAPI_IRQ_SaveAndDisable(void);      // Save flags + disable
void        KAPI_IRQ_Restore(uint64_t flags);   // Restore saved flags

// Cache Control (FR-003)
void        KAPI_Cache_CleanRange(void* addr, size_t len);
void        KAPI_Cache_InvalidateRange(void* addr, size_t len);
void        KAPI_Cache_FlushAll(void);

// UART I/O (FR-021, FR-022) — already implemented as HAL_UART_*
// Ethernet I/O (net-protocol) — already implemented as ETH_* / RUDP_*

/* ═══════════════════════════════════════════════════════════════════
 *  LAYER 2: Memory Management (mem/)
 *  Static allocation — no malloc/free, pool-based
 * ═══════════════════════════════════════════════════════════════════ */

// Pool Allocator (FR-016, FR-017)
Status      KAPI_Mem_InitPool(void* base, size_t size, size_t block_size);
void*       KAPI_Mem_AllocBlock(size_t size, size_t alignment);
void        KAPI_Mem_FreeBlock(void* ptr);
size_t      KAPI_Mem_GetFreeBytes(void);
size_t      KAPI_Mem_GetPeakUsage(void);

// Tensor-Specific Allocation (FR-018)
void*       KAPI_Mem_AllocTensor(size_t size);  // 64-byte aligned
void        KAPI_Mem_FreeTensor(void* ptr);

// Memory Utilities (no libc dependency)
void        KAPI_Mem_Copy(void* dst, const void* src, size_t n);
void        KAPI_Mem_Set(void* dst, uint8_t val, size_t n);
int         KAPI_Mem_Compare(const void* a, const void* b, size_t n);

/* ═══════════════════════════════════════════════════════════════════
 *  LAYER 3: Scheduling (sched/)
 *  Cooperative task scheduling — no preemption
 * ═══════════════════════════════════════════════════════════════════ */

// Task Management
typedef void (*TaskFunc)(void* arg);
int         KAPI_Task_Create(TaskFunc func, void* arg, size_t stack_size);
void        KAPI_Task_Yield(void);              // Cooperative yield
void        KAPI_Task_Exit(void);               // Mark task finished
int         KAPI_Task_GetId(void);              // Current task ID

// Scheduler Control
void        KAPI_Sched_Init(void);
void        KAPI_Sched_Run(void);               // Run until all done
void        KAPI_Sched_Stop(void);

/* ═══════════════════════════════════════════════════════════════════
 *  LAYER 4: Logging & Diagnostics (log/)
 *  Printf-like output via UART
 * ═══════════════════════════════════════════════════════════════════ */

void        KAPI_Log(const char* module, const char* msg);
void        KAPI_LogHex(const char* label, uint64_t value);
void        KAPI_Panic(const char* reason);     // Print + halt

/* ═══════════════════════════════════════════════════════════════════
 *  LAYER 5: Performance Counters (perf/)
 *  ARM PMU access for profiling (FR-026–030)
 * ═══════════════════════════════════════════════════════════════════ */

void        KAPI_Perf_Reset(void);
uint64_t    KAPI_Perf_GetCycles(void);
uint64_t    KAPI_Perf_GetCacheMisses(void);
uint64_t    KAPI_Perf_GetBranchMisses(void);
void        KAPI_Perf_StartRegion(const char* name);
void        KAPI_Perf_EndRegion(const char* name);
```

### Why This Works for a Unikernel
- **Zero overhead**: Direct function calls, no SVC trap, no context switch
- **Single address space**: All code shares the same page tables — no permission transitions
- **Compile-time binding**: The ML runtime and kernel are linked into one binary
- **Type safety**: Full C type checking (unlike `syscall(NR, ...)` variadic interface)

---

## Scheduler Design

### Recommended: Two-Level Cooperative Scheduler

MiniOS needs a scheduler that is:
1. **Cooperative** (not preemptive) — operators run to completion
2. **Graph-aware** — respects ONNX operator dependencies
3. **Predictable** — deterministic execution order for consistent timing
4. **Lightweight** — sub-microsecond context switch or zero-overhead inline

#### Level 1: Graph Scheduler (Static, Compile-Time)

This is the **primary scheduler** — it determines the order in which ONNX operators execute.

```
Algorithm: Topological Sort with Priority Weighting
──────────────────────────────────────────────────────

Input:  ONNX Graph G = (Nodes, Edges)
        Each node has priority P (based on estimated compute cost)
        
Step 1: Build adjacency list from tensor dependencies
Step 2: Compute in-degree for each node
Step 3: Initialize priority queue with zero-in-degree nodes
Step 4: While queue is not empty:
          - Dequeue node N with highest priority
          - Add N to execution schedule
          - For each successor S of N:
              - Decrement in-degree of S
              - If in-degree(S) == 0: enqueue S
Step 5: If schedule length ≠ node count → cycle detected (error)

Output: Linear execution order respecting dependencies
```

**Already partially implemented** in `ONNX_Graph_GenerateSchedule()` and `ONNX_Graph_GenerateCustomSchedule()`.

#### Level 2: Task Scheduler (Runtime, Cooperative)

For concurrent tasks **outside** the inference hot path (e.g., network polling, telemetry, UART I/O):

```
┌─────────────────────────────────────────────────────────┐
│     Main Cooperative Loop (kernel_main)                  │
│                                                          │
│  while (running) {                                       │
│      // 1. Check for incoming network/UART commands      │
│      RUDP_Poll(&session);                                │
│      UART_CheckInput();                                  │
│                                                          │
│      // 2. If inference requested, execute graph         │
│      if (inference_pending) {                            │
│          for (i = 0; i < schedule_length; i++) {        │
│              ONNX_Runtime_ExecuteNode(ctx, schedule[i]); │
│              // Yield point: check for urgent interrupts │
│              if (KAPI_IRQ_Pending()) handle_urgent();    │
│          }                                               │
│          send_results();                                  │
│      }                                                   │
│                                                          │
│      // 3. Periodic tasks                                │
│      if (timer_elapsed(TELEMETRY_INTERVAL))              │
│          send_telemetry();                                │
│                                                          │
│      // 4. Idle — enter low-power WFE                    │
│      if (no_work_pending) WFE();                         │
│  }                                                       │
└─────────────────────────────────────────────────────────┘
```

### Scheduling Algorithms Comparison for This Project

| Algorithm | Fit for MiniOS? | Rationale |
|-----------|----------------|-----------|
| **Topological Sort (Kahn's)** | ✅ **Best** for graph scheduling | Respects operator dependencies; O(V+E); deterministic order |
| **Cooperative Round-Robin** | ✅ **Best** for task scheduling | Simple, predictable, already implemented in `cooperative_scheduler.h` |
| **Priority-Based (Static)** | ✅ Good alternative | Can prioritize compute-heavy operators for cache warmth |
| **Rate-Monotonic (RMS)** | ❌ Not suitable | Designed for periodic preemptive tasks — MiniOS is aperiodic cooperative |
| **Earliest Deadline First (EDF)** | ❌ Not suitable | Dynamic priority — adds complexity incompatible with predictability goals |
| **Linux CFS** | ❌ Not suitable | General-purpose fair scheduling — irrelevant for single-application unikernel |
| **Work-Stealing** | ❌ Not suitable | Multi-core parallel — MiniOS is single-threaded by design |

### Recommended Algorithm
**Topological Sort with Static Priority** for the graph scheduler, combined with **Cooperative Round-Robin** for background tasks. This gives deterministic inference execution while allowing periodic network/telemetry tasks to run between inference requests.

---

## Memory Manager Design

### Requirements Analysis

From the SRS (FR-016 through FR-020, DC-002):
- All memory allocated **before** inference begins (no runtime malloc)
- Tensor memory reuse based on **lifetime analysis**
- 64-byte cache-line alignment (FR-018)
- Memory protection for critical regions via MMU (FR-019)
- Peak usage statistics and fragmentation tracking (FR-020)
- **No dynamic allocation at runtime** (DC-002)

### Proposed Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Memory Map (QEMU virt)                     │
├─────────────────────────────────────────────────────────────┤
│  0x40000000  ┌──────────────┐                               │
│              │  .text (code)│  ~64KB                        │
│              ├──────────────┤                               │
│              │  .rodata     │  ~16KB (strings, ONNX model)  │
│              ├──────────────┤                               │
│              │  .data       │  ~4KB  (initialized globals)  │
│              ├──────────────┤                               │
│              │  .bss        │  ~4KB  (zero-init globals)    │
│  0x40100000  ├──────────────┤                               │
│              │  Kernel Heap │  1MB   (page tables, etc.)    │
│  0x40200000  ├──────────────┤                               │
│              │  Tensor Pool │  64MB  (pre-allocated)        │
│              │  (64B align) │                               │
│  0x44200000  ├──────────────┤                               │
│              │  Workspace   │  16MB  (temporary compute)    │
│  0x45200000  ├──────────────┤                               │
│              │  RUDP Buffers│  256KB (static Tx/Rx frames)  │
│              ├──────────────┤                               │
│              │  Free        │                               │
│  0x40800000  ├──────────────┤                               │
│              │  Stack ↓     │  512KB (grows downward)       │
│              └──────────────┘                               │
└─────────────────────────────────────────────────────────────┘
```

### Three-Tier Allocator Design

#### Tier 1: Bump Allocator (Boot-Time Only)
```c
// Used only during initialization — never freed
typedef struct {
    uint8_t* base;
    uint8_t* current;
    uint8_t* end;
    size_t   peak_usage;
} BumpAllocator;

void*  Bump_Alloc(BumpAllocator* a, size_t size, size_t alignment);
size_t Bump_GetUsed(BumpAllocator* a);
void   Bump_Reset(BumpAllocator* a);  // For re-initialization only
```

#### Tier 2: Pool Allocator (Tensor Memory)
```c
// Fixed-block pool for tensor storage
// Blocks are 64-byte aligned for cache efficiency
typedef struct {
    uint8_t*  pool_base;
    size_t    pool_size;
    size_t    block_size;     // Configurable per pool
    uint32_t  num_blocks;
    uint32_t  free_count;
    uint32_t* free_list;      // Stack-based free list
    uint8_t*  usage_bitmap;   // For statistics
} PoolAllocator;

Status Pool_Init(PoolAllocator* p, void* base, size_t size, size_t block_size);
void*  Pool_Alloc(PoolAllocator* p);
void   Pool_Free(PoolAllocator* p, void* ptr);
```

#### Tier 3: Graph Memory Planner (Tensor Lifetime Analysis)
```c
// Pre-computes memory layout before inference starts
// Analyzes tensor lifetimes to maximize memory reuse
typedef struct {
    uint32_t tensor_id;
    uint32_t first_use;     // First node that reads/writes this tensor
    uint32_t last_use;      // Last node that reads this tensor
    size_t   size;          // Tensor size in bytes
    size_t   offset;        // Computed offset in tensor pool
} TensorAllocation;

typedef struct {
    TensorAllocation  allocations[ONNX_MAX_TENSORS];
    uint32_t          num_allocations;
    size_t            total_required;
    size_t            total_with_reuse;  // After lifetime optimization
    size_t            savings_bytes;     // total_required - total_with_reuse
} MemoryPlan;

// Compute optimal memory plan for a graph
Status MEM_PlanGraph(ONNX_Graph* graph, MemoryPlan* plan);

// Execute the plan — assign tensor data pointers
Status MEM_ExecutePlan(MemoryPlan* plan, void* tensor_pool, size_t pool_size);
```

### Tensor Lifetime Analysis Algorithm

```
Algorithm: Greedy Interval Graph Coloring for Memory Reuse
──────────────────────────────────────────────────────────

Input:  Execution schedule S = [N0, N1, ..., Nk]
        Tensors T = [T0, T1, ..., Tm] with sizes

Step 1: For each tensor Ti, compute:
          first_use = min node index where Ti appears as input/output
          last_use  = max node index where Ti appears as input

Step 2: Sort tensors by size (largest first — first-fit decreasing)

Step 3: For each tensor Ti (in sorted order):
          - Find the smallest existing memory slot that:
              a) Has size >= Ti.size
              b) Has no lifetime overlap with Ti
          - If found: assign Ti to that slot (memory reuse!)
          - If not: allocate new slot at next available offset

Step 4: Total memory = max(offset + size) across all tensors

Result: Each tensor has assigned offset — no two live tensors overlap
```

### Memory Protection Scheme (FR-019)

Using the existing MMU page tables, define protection regions:

| Region | Permission | Purpose |
|--------|-----------|---------|
| `.text` | RX (Read + Execute) | Code — no writes allowed |
| `.rodata` | RO (Read Only) | Constants, embedded ONNX model |
| `.data`, `.bss` | RW (Read + Write) | Globals, page tables |
| Stack | RW + NX (No Execute) | Stack — no code execution |
| Tensor Pool | RW + NX | Tensor data — no code execution |
| Device Memory | RW + NX + Device | UART/Ethernet MMIO |
| Guard Pages | No Access | Stack overflow detection |

This requires extending the current L1 block descriptors to L1→L2→L3 tables with 4KB granularity for fine-grained protection.

---

## Summary

The MiniOS project has made substantial progress across all critical components. The bootloader, HAL, UART protocol, RUDP networking, ONNX parser/runtime, and benchmark suite are all functional. The immediate next steps are:

1. **Implement the static memory allocator** (Tier 1–3 design above)
2. **Complete Conv2D and Pooling operators** with NEON SIMD
3. **Integrate all branches** into a single bootable unikernel
4. **Wire up the Kernel API** (KAPI layer) as the unified interface
5. **Add ARM PMU performance counters** for production profiling
