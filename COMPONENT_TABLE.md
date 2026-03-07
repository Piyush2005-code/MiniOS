# MiniOS — Component Traceability & Progress Table

> **Project:** MiniOS — ARM64 Bare-Metal Unikernel for ML Inference  
> **Repository:** https://github.com/Piyush2005-code/MiniOS  
> **Report Generated:** 03/02/2026  
> **Team:** Piyush Singh Bhati · Harshit Saini · Aashma Yadav · Darpan Baviskar

---

## Column Key

| Abbreviation | Meaning |
|---|---|
| FR | Functional Requirement |
| NFR | Non-Functional Requirement |
| BR | Business Requirement |
| DC | Design Constraint |
| PR | Performance Requirement |
| DOC | Documentation Requirement |

---

## Full Component Table

| Requirements | Component | Class / Module | Functions / Interfaces | Start Date | End Date | Update Date | Update Made | Reason for Update | Team Member(s) Designing & Updating | Team Member(s) Testing | Status | Ticket ID | Date of Ticket Generation | Date of Ticket Resolution |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOC-001 | Project Scaffold — Initial Repository | `README.md` | N/A | 01/26/2026 | 01/26/2026 | N/A | N/A | N/A | Piyush Singh Bhati | N/A | Complete | TICKET-001 | 01/26/2026 | 01/26/2026 |
| FR-001, DC-001 | Bare-Metal Boot — kernel entry & platform loop | `boot_loop`, `Makefile` (Ubuntu / macOS / Arch) | `main()` bare-metal loop, platform init | 02/11/2026 | 02/12/2026 | 02/12/2026 | Added platform-specific Makefiles for Ubuntu, macOS, Arch Linux | Three team members use different host OSes; single Makefile was insufficient | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-002 | 02/11/2026 | 02/12/2026 |
| FR-002 | HAL — UART Serial Driver | `uart.c` / `uart.h` | `UART_Init()`, `UART_PutChar()`, `UART_GetChar()`, `UART_PutStr()` | 02/15/2026 | 02/15/2026 | N/A | N/A | N/A | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-003 | 02/15/2026 | 02/15/2026 |
| FR-003, NFR-001 | Bootloader / HAL — Boot & MMU | `boot.S`, `vectors.S`, `mmu.c`, `mmu.h` | `_start()`, `vector_table`, `MMU_Init()`, `MMU_Enable()` | 02/20/2026 | 03/02/2026 | 03/02/2026 | Updated project documentation to include HAL design rationale | Bootloader testing completed; documentation was lagging | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-004 | 02/20/2026 | 03/02/2026 |
| DOC-002 | Kernel API Documentation | `ProjectDocumentation.md` | N/A | 02/23/2026 | 02/23/2026 | 03/02/2026 | Added GIC and Timer integration notes; updated subsystem table | New HAL subsystems (GIC, Timer) added during Sprint 1 | Piyush Singh Bhati | N/A | Complete | TICKET-005 | 02/23/2026 | 03/02/2026 |
| FR-004, NFR-002 | HAL — ARM64 Architecture Helpers | `arch.h` (inline, header-only) | `arch_enable_irq()`, `arch_disable_irq()`, `arch_irq_save()`, `arch_irq_restore()`, `arch_dsb()`, `arch_isb()`, `arch_get_el()` | 02/23/2026 | 02/23/2026 | N/A | N/A | N/A | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-006 | 02/23/2026 | 02/23/2026 |
| FR-005 | Lib — Freestanding String Utilities | `string.h` / `string.c` | `memset()`, `memcpy()`, `strlen()` | 02/24/2026 | 02/24/2026 | N/A | N/A | N/A | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-007 | 02/24/2026 | 02/24/2026 |
| FR-006, NFR-003 | Kernel — Bump Allocator (Memory Manager) | `kmem.h` / `kmem.c` | `KMEM_Init()`, `KMEM_Alloc()`, `KMEM_GetFreeSpace()`, `KMEM_GetStats()` | 02/24/2026 | 02/25/2026 | 02/25/2026 | Fixed pointer alignment bug: `ptr + alignment` replaced with `(ptr + mask) & ~mask` | Incorrect alignment produced misaligned allocations on AArch64 hardware | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-008 | 02/24/2026 | 02/25/2026 |
| FR-007, NFR-004 | HAL — GICv2 Interrupt Controller Driver | `gic.h` / `gic.c` | `HAL_GIC_Init()`, `HAL_GIC_EnableIRQ()`, `HAL_GIC_DisableIRQ()`, `HAL_GIC_Acknowledge()`, `HAL_GIC_EndOfInterrupt()` | 02/26/2026 | 02/26/2026 | 02/26/2026 | Fixed GIC CPU interface init: added `GICC_PMR = 0xFF` and `GICC_BPR = 0` | Incomplete initialization left IRQs masked; no interrupts were delivered | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-009 | 02/26/2026 | 02/26/2026 |
| FR-008, PR-001 | HAL — ARM Generic Timer Driver | `timer.h` / `timer.c` | `HAL_Timer_Init()`, `HAL_Timer_Enable()`, `HAL_Timer_Disable()`, `HAL_Timer_GetTicks()`, `HAL_Timer_GetElapsedUs()`, `HAL_Timer_DelayUs()`, `HAL_Timer_Reload()` | 02/27/2026 | 02/27/2026 | 02/27/2026 | Fixed tick-calculation overflow; added `HAL_Timer_GetElapsedUs()` helper | `uint32_t` multiply overflowed on high-frequency counters; cast to `uint64_t` | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-010 | 02/27/2026 | 02/27/2026 |
| FR-006, FR-007, FR-008, DC-002 | Kernel — System Integration & Linker Script | `main.c` / `linker.ld` | `kernel_main()`, subsystem init sequence: `UART → vectors → MMU → KMEM → GIC → Timer → WFE` | 03/01/2026 | 03/02/2026 | N/A | N/A | N/A | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-011 | 03/01/2026 | 03/02/2026 |
| FR-009, NFR-005 | Kernel — Full Multithreaded API (Sprint 2) | `thread.h`, `context.S`, `kmem.h` (arena/pool), `gic.h`, `timer.h` | Thread create/yield/join/exit, arena allocator, pool allocator, full GIC + Timer APIs, cooperative scheduler, `STATUS_ToString()` | 02/20/2026 | 03/02/2026 | 03/02/2026 | Full kernel API delivered on `Kernel_API` branch; extends Sprint 1 bare-minimum with threading | Sprint 2 scope expanded to multithreading and advanced memory management | Piyush Singh Bhati | Piyush Singh Bhati | Complete | TICKET-012 | 02/20/2026 | 03/02/2026 |
| FR-010, PR-002 | Unikernel — ML Inference Platform (Unikraft) | `unikernel config`, `benchmark_runner`, automation scripts | `benchmark_run()`, `project_setup()`, automated test harness, benchmark report generation | 01/27/2026 | 02/04/2026 | N/A | N/A | N/A | Harshit Saini | Harshit Saini | Complete | TICKET-013 | 01/27/2026 | 02/04/2026 |
| DOC-003, DOC-004 | Documentation — SRS & Progress Reports | `SRS.md`, `README.md`, `images/`, `images_v1/` | N/A | 01/26/2026 | 02/26/2026 | 02/26/2026 | Added `images_v1/` folder with architecture diagrams; fixed README typos and formatting | Visual documentation lagging behind implementation; formatting inconsistencies found in review | Piyush Singh Bhati, Darpan Baviskar | N/A | Complete | TICKET-014 | 01/26/2026 | 02/26/2026 |
| FR-011, NFR-006 | ML Inference — ONNX Runtime (Freestanding) | `onnx_loader.c`, `onnx_model.h`, traceability matrix | `onnx_load_model()`, `onnx_run_inference()`, ONNX protobuf parser, embedded model byte array | 02/20/2026 | 03/02/2026 | 03/02/2026 | Added project overview section and traceability matrix; refactored embedded model protobuf with detailed byte-level comments | Documentation requirement for academic submission; protobuf structure needed clarification | Harshit Saini | Harshit Saini | Complete | TICKET-015 | 02/20/2026 | 03/02/2026 |
| FR-012, NFR-007 | Network — Reliable UDP (RUDP) Protocol | `rudp.c`, `rudp.h`, `README.md` | `RUDP_Init()`, `RUDP_Send()`, `RUDP_Receive()`, `RUDP_Ack()`, handshake sequence | 02/26/2026 | 03/01/2026 | 03/01/2026 | Updated README.md twice (documentation corrections and clarifications) | Protocol specification details were unclear; README needed alignment with implementation | Aashma Yadav | Aashma Yadav | Complete | TICKET-016 | 02/26/2026 | 03/01/2026 |
| FR-013, PR-003, NFR-008 | Kernel — Scheduler Policies & Benchmark Suite | `scheduler.c`, `benchmark.c`, `workloads.c`, benchmark report | `FCFS()`, `SJF()`, `RoundRobin()`, `HRRN()`, `Priority()`, `MLQ()`, `Lottery()`, `benchmark_run_all_policies()`, `stats_track()`, ML operator workload tasks, cooperative scheduler with memory + timer | 02/24/2026 | 03/02/2026 | 03/02/2026 | Final commit implemented cooperative scheduler integrating memory management and timer services; added quantitative benchmark report with matplotlib charts | Scheduler required timer and memory integration for accurate benchmarking; charts needed for report | Harshit Saini, Darpan Baviskar | Harshit Saini, Darpan Baviskar | Complete | TICKET-017 | 02/24/2026 | 03/02/2026 |

---

## Branch-to-Component Mapping

| Branch | Ticket IDs | Primary Author(s) | Date Range | Status |
|---|---|---|---|---|
| `main` | TICKET-001 | Piyush Singh Bhati | 01/26/2026 | Complete |
| `baremetalOS` | TICKET-002 | Piyush Singh Bhati | 02/11/2026 – 02/12/2026 | Complete |
| `UART_Implementation` | TICKET-003 | Piyush Singh Bhati | 02/15/2026 | Complete |
| `BootLoader_and_HAL` | TICKET-004 | Piyush Singh Bhati | 02/20/2026 – 03/02/2026 | Complete |
| `build/kernel_api` | TICKET-005 – TICKET-011 | Piyush Singh Bhati | 02/23/2026 – 03/02/2026 | Complete |
| `Kernel_API` | TICKET-012 | Piyush Singh Bhati | 02/20/2026 – 03/02/2026 | Complete |
| `unikernel` | TICKET-013 | Harshit Saini | 01/27/2026 – 02/04/2026 | Complete |
| `SRS-and-Reports` | TICKET-014 | Piyush Singh Bhati, Darpan Baviskar | 01/26/2026 – 02/26/2026 | Complete |
| `feat/onnx` | TICKET-015 | Harshit Saini | 02/20/2026 – 03/02/2026 | Complete |
| `net-protocol` | TICKET-016 | Aashma Yadav | 02/26/2026 – 03/01/2026 | Complete |
| `scheduler_benchmark` | TICKET-017 | Harshit Saini, Darpan Baviskar | 02/24/2026 – 03/02/2026 | Complete |
