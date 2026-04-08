# MiniOS — Requirements Traceability Matrix

> Generated from Git repository analysis across all branches.  
> Repository: https://github.com/Piyush2005-code/MiniOS.git  
> Date: 03/02/2026

---

## Branch: `BootLoader_and_HAL`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| FR-001 | ARM64 processor core initialization (SCTLR, TCR, MAIR) | HAL / Bootloader | boot.S | `_start`, `_Lprimary_core`, `_Lfrom_el3`, `_Lfrom_el2`, `_Lat_el1` | 02/20/2026 | 02/20/2026 | — | — | — |
| FR-002 | MMU configuration with 4KB granularity page tables | HAL / MMU | mmu.c / mmu.h | `HAL_MMU_Init`, `mmu_build_page_tables`, `write_mair_el1`, `write_tcr_el1`, `write_ttbr0_el1` | 02/20/2026 | 02/20/2026 | — | — | — |
| FR-003 | Data and instruction cache enable with write-back policy | HAL / MMU | mmu.c | `HAL_MMU_Init` (SCTLR bits C, I), `HAL_MMU_CleanInvalidateDCache`, `HAL_MMU_InvalidateTLB` | 02/20/2026 | 02/20/2026 | — | — | — |
| FR-004 | Hardware interrupt handling with configurable priorities | HAL / Exception Handling | vectors.S / main.c | `_vector_table`, `_exception_handler`, `HAL_Exception_Handler`, `install_vectors` | 02/20/2026 | 02/20/2026 | — | — | — |
| FR-005 | Timer services for execution timing (ARM Generic Timer) | HAL / Timer | boot.S | `_Lfrom_el2` (cnthctl_el2, cntvoff_el2 timer enable) | 02/20/2026 | 02/20/2026 | — | — | — |
| FR-021 | UART serial interface (PL011 at 0x09000000) | HAL / UART | uart.c / uart.h | `HAL_UART_Init`, `HAL_UART_PutChar`, `HAL_UART_GetChar`, `HAL_UART_PutString`, `HAL_UART_PutHex`, `HAL_UART_PutDec` | 02/20/2026 | 02/20/2026 | — | — | — |
| DC-001 | C11 and ARM64 assembly implementation | Build System | Makefile / linker.ld | Makefile (cross-compiler aarch64-linux-gnu-gcc), linker.ld (memory layout ENTRY=_start) | 02/20/2026 | 02/20/2026 | — | — | — |
| SR-003 | No dynamic code loading or self-modifying code | HAL / Kernel | main.c | `kernel_main` (static initialization, idle loop with WFE) | 02/20/2026 | 02/20/2026 | — | — | — |
| PR-006 | Boot time < 200ms power-on to inference-ready | HAL / Bootloader | boot.S / main.c | Full boot sequence: `_start` → EL transition → BSS zero → `kernel_main` | 02/20/2026 | 02/20/2026 | — | — | — |

**Commit:** `d8b7372` — Testing for the bootloader  
**Author:** Piyush2005-code  

---

## Branch: `baremetalOS`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| FR-001 | ARM64 processor initialization (EL3→EL2→EL1 transition) | Bootloader | start.S | `_start`, `try_el2`, `el2_entry`, `setup_el1`, `semihosting_puts`, `exception_handler` | 02/11/2026 | 02/12/2026 | 02/12/2026 | Multi-OS Makefile support | Added Makefiles for Ubuntu, MacOS, Arch |
| FR-002 | MMU flat/identity-mapped page table setup | Bootloader / MMU | mmu.c | `mmu_init` (L1/L2 tables, 2MB blocks, TT_TYPE_BLOCK, MAIR, TCR) | 02/11/2026 | 02/12/2026 | — | — | — |
| FR-021 | Basic UART output via PL011 MMIO | Bootloader | main.c | `uart_putc`, `print` (direct UART_DR/UART_FR register access) | 02/11/2026 | 02/12/2026 | — | — | — |
| DC-001 | C and ARM64 assembly baremetal build | Build System | Makefile / linker.ld | Cross-compilation with aarch64-linux-gnu-*, linker script at 0x40000000 | 02/11/2026 | 02/12/2026 | 02/12/2026 | Three OS Makefiles | Team uses Ubuntu, MacOS, Arch |

**Commits:**
- `ce670a1` (02/11/2026) — Added basic baremetal OS running loop — *Piyush2005-code*
- `9d6fae2` (02/11/2026) — Added makefiles for all team OS variants — *Piyush2005-code*
- `41cf625` (02/12/2026) — Corrected code — *Piyush2005-code*
- `f66d605` (02/12/2026) — Three Makefiles for Ubuntu/MacOS/Arch — *Piyush2005-code*

---

## Branch: `UART_Implementation`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| FR-021 | UART binary protocol (START/LEN/CMD/PAYLOAD/CRC) per Appendix F | Communication Interface | uart_protocol.h / uart_protocol.c | `uart_pack_message`, `uart_unpack_message`, `uart_calculate_crc` | 02/15/2026 | 02/15/2026 | — | — | — |
| FR-022 | Inference result output via UART protocol | Communication Interface | uart_protocol.h | `uart_pack_message` (CMD_GET_RESULTS = 0x04) | 02/15/2026 | 02/15/2026 | — | — | — |
| FR-025 | Input validation for correctness and safety | Communication Interface | uart_protocol.c | `uart_unpack_message` (START_BYTE check, CRC validation, bounds checks) | 02/15/2026 | 02/15/2026 | — | — | — |
| SR-004 | CRC integrity checking for data validation | Communication Interface | uart_protocol.c | `uart_calculate_crc` (XOR-8 checksum), CRC verification in `uart_unpack_message` | 02/15/2026 | 02/15/2026 | — | — | — |

**Protocol Commands Implemented:**
- `CMD_LOAD_MODEL` (0x01), `CMD_SET_INPUT` (0x02), `CMD_RUN_INFERENCE` (0x03)
- `CMD_GET_RESULTS` (0x04), `CMD_SYSTEM_STATUS` (0x05), `CMD_CONFIG_UPDATE` (0x06)

**Commit:** `2d283ce` — New Implementation of UART Protocol  
**Author:** Piyush2005-code  

---

## Branch: `feat/onnx`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| FR-006 | ONNX computation graph ingestion (binary protobuf, v1.8+) | Graph Processing / ONNX Loader | onnx_loader.h / onnx_loader.c | `ONNX_LoadProtobuf`, `ONNX_LoadEmbedded`, `ONNX_LoadCustomBinary`, `ONNX_ExportCustomBinary`, `ONNX_PrintModelInfo` | 02/27/2026 | 03/02/2026 | 03/02/2026 | Refactored embedded test model with protobuf structure | Improved byte-level comments in test model |
| FR-007 | Graph validation for integrity and operator compatibility | Graph Processing / Graph Manager | onnx_graph.h / onnx_graph.c | `ONNX_Graph_Validate`, `ONNX_Graph_Init`, `ONNX_Graph_Cleanup` | 02/27/2026 | 03/02/2026 | — | — | — |
| FR-008 | Operator dependency analysis (topological sorting on DAG) | Graph Processing / Scheduler | onnx_graph.h / onnx_graph.c | `ONNX_Graph_BuildDependencies`, `ONNX_Graph_GenerateSchedule`, `ONNX_Graph_GenerateCustomSchedule` | 02/27/2026 | 03/02/2026 | — | — | — |
| FR-009 | Static execution schedule with optional operator fusion | Graph Processing / Scheduler | onnx_graph.h | `ONNX_Graph_SetCustomSchedule`, `ONNX_Graph_GetSchedule`, `ONNX_Node_SetPriority` | 02/27/2026 | 03/02/2026 | — | — | — |
| FR-011 | Execute operators in dependency order (cooperative) | Execution Engine / Runtime | onnx_runtime.h / onnx_runtime.c | `ONNX_Runtime_Inference`, `ONNX_Runtime_ExecuteNode`, `ONNX_Runtime_ExecuteUpTo` | 02/27/2026 | 03/02/2026 | — | — | — |
| FR-012 | Execution timing measurement per operator | Execution Engine / Profiling | onnx_runtime.h / onnx_runtime.c | `ONNX_Runtime_GetStats`, `ONNX_Runtime_ResetStats`, `ONNX_Runtime_PrintProfile` | 02/27/2026 | 03/02/2026 | — | — | — |
| FR-013 | NEON SIMD operator implementations (Add, MatMul, ReLU) | Execution Engine / Operators | onnx_runtime.c | `ONNX_Execute_Arithmetic` (Add/Sub/Mul/Div), `ONNX_Execute_MatMul`, `ONNX_Execute_ReLU` | 02/27/2026 | 03/02/2026 | — | — | — |
| FR-015 | Error handling via predefined error codes | Execution Engine | onnx_runtime.c | Status-based dispatch in `ONNX_Runtime_ExecuteNode`, per-operator error propagation | 02/27/2026 | 03/02/2026 | — | — | — |

**ONNX Data Types Defined:** `ONNX_DataType` (FLOAT32, UINT8, INT8, INT16, INT32, INT64, FLOAT64)  
**Operators Supported:** Add, Sub, Mul, Div, MatMul, ReLU, Sigmoid, Tanh, Softmax, Conv (stub), MaxPool (stub), AvgPool (stub), Reshape, Transpose, Flatten, BatchNorm, GEMM, Concat

**Commits:**
- `d7e6e1c` (02/27/2026) — Implement ONNX model loading and inference runtime — *Priyanshu Saini*
- `161a93e` (03/02/2026) — Refactor embedded ONNX test model with protobuf structure — *Harshit Saini*

---

## Branch: `net-protocol`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| FR-021 | Network interface for graph/result transfer (Ethernet) | Communication Interface / RUDP | rudp.h / rudp.c | `RUDP_Init`, `RUDP_Shutdown`, `RUDP_Send`, `RUDP_SendLarge`, `RUDP_Receive` | 03/01/2026 | 03/01/2026 | 03/01/2026 | README updates | Documentation improvements |
| FR-022 | Inference result output via network protocol | Communication Interface / RUDP | rudp.h / rudp.c | `RUDP_Send` (NET_CMD_GET_RESULTS), response frame building | 03/01/2026 | 03/01/2026 | — | — | — |
| FR-023 | System status and health reporting over network | Communication Interface / RUDP | rudp.h / net_types.h | `NET_CMD_SYSTEM_STATUS`, `NET_CMD_KEEPALIVE` commands | 03/01/2026 | 03/01/2026 | — | — | — |
| FR-025 | Input validation with CRC-16 integrity checking | Communication Interface / CRC | crc16.h / crc16.c | `CRC16_Compute`, frame validation in `RUDP_Receive` | 03/01/2026 | 03/01/2026 | — | — | — |
| SR-004 | CRC-16 data integrity (stronger than UART CRC-8) | Communication Interface / CRC | crc16.c | `CRC16_Compute` over full frame including headers | 03/01/2026 | 03/01/2026 | — | — | — |
| NFR (reliability) | Reliable delivery with ACK/retry for ML control frames | Communication Interface / RUDP | rudp.c | `RUDP_Send` (RELIABLE flag), `RUDP_Poll` (retransmit timer), `store_in_window`, `build_frame` | 03/01/2026 | 03/01/2026 | — | — | — |
| NFR (network) | Session management (open/close/keepalive) | Communication Interface / RUDP | rudp.c | `RUDP_OpenSession`, `RUDP_CloseSession`, `RUDP_Poll` (keepalive/dead session detect) | 03/01/2026 | 03/01/2026 | — | — | — |
| NFR (network) | Fragmentation for large ONNX models (>1452 bytes) | Communication Interface / RUDP | rudp.h / rudp.c | `RUDP_SendLarge` (FRAG/FRAG_END flags, per-fragment ACK) | 03/01/2026 | 03/01/2026 | — | — | — |
| DC-002 | No dynamic allocation (all static buffers) | Communication Interface | rudp.c / eth_driver.h | `s_tx_frame`, `s_rx_frame` (static buffers), `s_local_mac`, `s_stats` | 03/01/2026 | 03/01/2026 | — | — | — |

**Ethernet Driver:** Abstracts QEMU virtio-net and SMSC LAN9118 via `ETH_Init`, `ETH_Send`, `ETH_Recv`, `ETH_TxReady`, `ETH_RxAvailable`  
**RUDP Protocol:** Selective reliability (ML control = reliable, telemetry = best-effort), CRC-16, fragmentation, session keepalive

**Commits:**
- `f1c131b` (03/01/2026) — Initial RUDP network protocol implementation — *Aashma Yadav*
- `1c0be9b` (03/01/2026) — Update README.md — *Aashma Yadav*
- `e3b7210` (03/01/2026) — Update README.md — *Aashma Yadav*

---

## Branch: `unikernel`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| PR-001 | Inference throughput within 20% of bare-metal | Benchmarking Suite | neural_network_benchmark.c / unikraft_benchmark.c | `forward`, `init_model`, `softmax`, `main` (benchmark loop) | 02/16/2026 | 02/25/2026 | 02/25/2026 | Added Ubuntu VM benchmark script | Comparison across environments |
| PR-002 | System overhead measurement (< 10% of inference time) | Benchmarking Suite | multithreaded_benchmark.c | `BenchmarkResult` struct, timing via `rdtscp_time`, throughput/latency calculation | 02/16/2026 | 02/25/2026 | — | — | — |
| PDR-001 | Execution time variation < 15% across runs | Benchmarking Suite | generate_graphs.py / analyze_results.py | Latency P50/P99 measurement, statistical analysis, comparison graphs | 02/16/2026 | 02/25/2026 | — | — | — |
| BR-005 | Cooperative scheduler for unikernel workloads | Scheduling / Cooperative Scheduler | cooperative_scheduler.h | `coop_sched_init`, `coop_sched_spawn`, `coop_sched_run`, `coop_yield`, `CoopTask`, `CoopScheduler` | 02/16/2026 | 02/25/2026 | — | — | — |

**Benchmark Models:** Small (784→128→10) and Large neural network forward passes  
**Environments Tested:** Unikraft unikernel, Ubuntu VM (QEMU), multithreaded, optimized parallel  
**Scheduler:** Header-only cooperative scheduler using `ucontext` (makecontext/swapcontext), round-robin, sub-μs context switches

**Commits:**
- `3fe53a2` (02/16/2026) — Unikernel vs Linux VM: ML Inference Benchmark Suite — *Harshit Saini*
- `0783e43` (02/25/2026) — Add benchmark script for Ubuntu VM environment — *Harshit Saini*

---

## Branch: `build`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| DC-001 | Project directory structure per SRS specification | Build System | Directory structure | N/A — restructured `/src`, `/include`, `/build`, `/scripts` per SRS coding standards | 02/27/2026 | 02/27/2026 | — | code_changes / directory restructure | Match proposed SRS directory structure |

**Commit:** `1cb4d60` (02/27/2026) — Changed directory structure to match proposed structure — *Piyush2005-code*

---

## Branch: `SRS-and-Reports`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| DOC-001 | SRS documentation and progress reports | Documentation | README.md / SRS LaTeX files | N/A — SRS v2.0/v3.0 documents, progress reports, UML diagrams | 02/16/2026 | 02/26/2026 | 02/26/2026 | Added images_v1 folder, fixed typos | Documentation improvements |

**Commits:**
- `5779a43` (02/16/2026) — Initial SRS and Progress Report — *Piyush Singh Bhati*
- `326aea3` (02/17/2026) — Updated README — *Piyush Singh Bhati*
- `587f63f` (02/17/2026) — Fix typos and formatting — *Piyush Singh Bhati*
- `b21ee7b` (02/17/2026) — Added images — *Piyush2005-code*
- `c0d6885` (02/26/2026) — Added images_v1 folder — *Darpan Baviskar*

---

## Branch: `main`

| Attribute | Requirements | Component | Class | Functions | Start Date | End Date | Update Date | Update Made | Reason for Update |
|-----------|-------------|-----------|-------|-----------|------------|----------|-------------|-------------|-------------------|
| DOC-001 | Repository landing page and branch guide | Documentation | README.md | N/A | 01/26/2026 | 02/26/2026 | 02/26/2026 | Updated README with project description and branch guide | Provide navigation for module branches |

**Commits:**
- `3150494` (01/26/2026) — First commit (initial README) — *Piyush2005-code*
- `16a52a6` (02/26/2026) — Updated README for main branch — *Piyush Singh Bhati*
