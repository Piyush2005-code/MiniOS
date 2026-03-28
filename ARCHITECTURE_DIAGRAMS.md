%% =============================================================================
%% MiniOS — ARM64 ML Inference Unikernel
%% Architecture Diagrams (extracted from PROJECT_DOCUMENTATION.md)
%% Branch: Kernel_API  |  HEAD: c370cb9  |  Date: 2026-03-18
%%
%% Diagram list:
%%   1. High-Level Architecture
%%   2. Boot Sequence
%%   3. Interrupt Handling Flow
%%   4. Context Switch Flow
%%   5. Memory Layout
%%   6. Kernel Memory Manager (KMEM) Allocator Map
%%   7. Thread State Machine
%%   8. Priority Queue Architecture
%%   9. ML Inference Thread Execution Model
%%  10. Kernel Subsystem Dependencies
%% =============================================================================


%% ---------------------------------------------------------------------------
%% Diagram 1: High-Level Architecture
%% Section 2.1 — Overall kernel image structure and hardware relationship
%% ---------------------------------------------------------------------------

graph TB
    subgraph "Physical Hardware / QEMU virt"
        HW[ARM64 Cortex-A53 CPU]
        MMIO[Memory-Mapped I/O\nUART · GIC · Timer]
        RAM[512 MB DRAM\n0x40000000-0x5FFFFFFF]
    end

    subgraph "MiniOS Kernel Image"
        BOOT[boot.S\n_start entry point]
        subgraph HAL[Hardware Abstraction Layer]
            UART[uart.c\nPL011 Serial Driver]
            MMU[mmu.c\nMMU + Cache Setup]
            GIC[gic.c\nGICv2 Interrupt Controller]
            TMR[timer.c\nARM Generic Timer]
            ARCH[arch.h\nInline ARM64 Primitives]
        end
        subgraph KERNEL[Kernel Core]
            MAIN[main.c\nkernel_main()  IRQ Dispatch]
            KMEM[kmem.c\nBump · Arena · Pool Allocators]
            THREAD[thread.c\nCooperative Scheduler + TCBs]
            CTX[context.S\nCPU Context Switch]
        end
        subgraph LIB[Freestanding Libraries]
            STR[string.c\nmemset · memcpy · strlen]
        end
        subgraph APP[Application Threads]
            INF[inference_thread\nSimulated ML Workload]
            MON[monitor_thread\nMemory + Uptime Monitor]
            IDLE[idle thread\nWFE low-power loop]
        end
    end

    BOOT --> HAL
    BOOT --> KERNEL
    KERNEL --> APP
    HAL --> MMIO
    MMIO --> HW
    RAM --> KERNEL


%% ---------------------------------------------------------------------------
%% Diagram 2: Boot Sequence
%% Section 2.2 — Step-by-step kernel initialization from reset to scheduler
%% ---------------------------------------------------------------------------

sequenceDiagram
    participant HW as ARM64 CPU
    participant BOOT as boot.S
    participant MAIN as kernel_main()
    participant HAL as HAL Drivers
    participant KMEM as Memory Manager
    participant SCHED as Scheduler

    HW->>BOOT: Reset / QEMU load at 0x40000000
    BOOT->>BOOT: Park secondary cores (MPIDR check)
    BOOT->>BOOT: Detect EL3/EL2/EL1, drop to EL1
    BOOT->>BOOT: Enable FP/SIMD (CPACR_EL1)
    BOOT->>BOOT: Set SP_EL1 = _stack_top
    BOOT->>BOOT: Zero BSS section
    BOOT->>MAIN: bl kernel_main

    MAIN->>HAL: HAL_UART_Init() — serial console
    MAIN->>MAIN: install_vectors() — VBAR_EL1
    MAIN->>HAL: HAL_MMU_Init() — page tables + caches
    MAIN->>KMEM: KMEM_Init() — heap from linker symbols
    MAIN->>HAL: HAL_GIC_Init() — interrupt controller
    MAIN->>HAL: HAL_Timer_Init() + HAL_Timer_SetInterval(10ms)
    MAIN->>HAL: HAL_GIC_EnableIRQ(IRQ=30)
    MAIN->>SCHED: SCHED_Init() — idle thread from boot context
    MAIN->>SCHED: THREAD_Create("inference", ...)
    MAIN->>SCHED: THREAD_Create("monitor", ...)
    MAIN->>HAL: HAL_Timer_Enable() + arch_enable_irq()
    MAIN->>SCHED: SCHED_Start() — enters idle loop (never returns)


%% ---------------------------------------------------------------------------
%% Diagram 3: Interrupt Handling Flow
%% Section 2.3 — Timer PPI #30 path from hardware to SCHED_TimerTick
%% ---------------------------------------------------------------------------

sequenceDiagram
    participant HW as Timer Hardware
    participant VEC as vectors.S
    participant DISP as HAL_IRQ_Handler()
    participant TMR as HAL_Timer_HandleIRQ()
    participant SCHED as SCHED_TimerTick()

    HW->>VEC: Physical Timer PPI #30 fires
    VEC->>VEC: _irq_handler_full: save x0-x30, ELR, SPSR to stack (272 bytes)
    VEC->>DISP: bl HAL_IRQ_Handler()
    DISP->>DISP: HAL_GIC_Acknowledge() -> irq_id
    DISP->>TMR: HAL_Timer_HandleIRQ()
    TMR->>TMR: system_ticks++, reload CNTP_TVAL
    TMR->>TMR: call timer_callback() if registered
    TMR-->>DISP: return
    DISP->>SCHED: SCHED_TimerTick()
    SCHED->>SCHED: wake sleeping threads whose wake_tick <= now
    DISP->>DISP: HAL_GIC_EndOfInterrupt(irq_id)
    DISP-->>VEC: return
    VEC->>VEC: restore x0-x30, ELR, SPSR
    VEC->>HW: eret — resume interrupted thread


%% ---------------------------------------------------------------------------
%% Diagram 4: Context Switch Flow
%% Section 2.4 — Cooperative thread switching via cpu_context_switch()
%% ---------------------------------------------------------------------------

sequenceDiagram
    participant T1 as Thread A (inference)
    participant YIELD as THREAD_Yield()
    participant SCHED as schedule()
    participant CTX as cpu_context_switch()
    participant T2 as Thread B (monitor)

    T1->>YIELD: THREAD_Yield() — cooperative yield between operators
    YIELD->>YIELD: arch_irq_save() — disable IRQs for safe switch
    YIELD->>YIELD: enqueue_ready(old) — put A back in READY queue
    YIELD->>SCHED: schedule() — pick highest-priority ready thread
    SCHED->>SCHED: dequeue_ready() -> Thread B selected
    SCHED->>CTX: cpu_context_switch(&A.context, &B.context)
    CTX->>CTX: Save x19-x30, SP to A.context (104 bytes)
    CTX->>CTX: Restore x19-x30, SP from B.context
    CTX->>T2: ret -> jump to B's saved LR
    Note over T2: Thread B now executing
    T2->>YIELD: THREAD_Yield() (later)
    YIELD->>CTX: cpu_context_switch(&B.context, &A.context)
    CTX->>T1: ret -> Thread A resumes after its cpu_context_switch call


%% ---------------------------------------------------------------------------
%% Diagram 5: Memory Layout
%% Section 2.5 — Virtual address space and RAM section layout
%% ---------------------------------------------------------------------------

graph LR
    subgraph "Virtual Address Space (Identity Mapped)"
        D["0x00000000 - 0x3FFFFFFF\nDevice Memory - nGnRnE\nUART 0x09000000\nGIC  0x08000000/0x08010000"]
        N["0x40000000 - 0x5FFFFFFF\nNormal WB Cacheable RAM\n512 MB"]
    end

    subgraph "RAM Layout"
        TEXT[".text code\n_start at 0x40000000"]
        RODATA[.rodata]
        DATA[.data]
        BSS[.bss]
        HEAP["Heap: _heap_start to _heap_end\n~498 MB available\nBump/Arena/Pool allocations"]
        GUARD[256KB guard zone]
        STACK["Kernel stack 64KB\ngrows downward"]
    end

    TEXT --> RODATA --> DATA --> BSS --> HEAP --> GUARD --> STACK


%% ---------------------------------------------------------------------------
%% Diagram 6: Kernel Memory Manager (KMEM) Allocator Map
%% Section 6 — Three-tier allocator strategy over the heap region
%% ---------------------------------------------------------------------------

graph TB
    subgraph "Heap Region (_heap_start to _heap_end)"
        B["Bump Allocator\nPermanent kernel allocations\nThread stacks, arena/pool headers"]
        A["Arena Region\nReserved by bump allocator\nResettable per-inference-cycle"]
        P["Pool Region\nReserved by bump allocator\nFixed-size object recycling"]
    end

    APP1[Thread Stacks] -->|KMEM_Alloc| B
    APP2[ML Tensors] -->|KMEM_TensorAlloc| A
    APP3[Operator Descriptors] -->|KMEM_PoolAlloc| P

    style A fill:#e8f5e9
    style P fill:#e3f2fd
    style B fill:#fff8e1


%% ---------------------------------------------------------------------------
%% Diagram 7: Thread State Machine
%% Section 7.1 — Full thread lifecycle from creation to termination
%% ---------------------------------------------------------------------------

stateDiagram-v2
    [*] --> INVALID : TCB slot unused
    INVALID --> READY : THREAD_Create()
    READY --> RUNNING : schedule() / cpu_context_switch()
    RUNNING --> READY : THREAD_Yield()
    RUNNING --> SLEEPING : THREAD_Sleep(ms)
    SLEEPING --> READY : SCHED_TimerTick() (wake_tick elapsed)
    RUNNING --> TERMINATED : THREAD_Exit()
    TERMINATED --> [*]


%% ---------------------------------------------------------------------------
%% Diagram 8: Priority Queue Architecture
%% Section 7.2 — Four-level FIFO ready queues feeding the scheduler
%% ---------------------------------------------------------------------------

graph LR
    subgraph "Ready Queues (4 priority levels)"
        H0["HIGH prio=0\nhead->tail FIFO"]
        H1["NORMAL prio=1\nhead->tail FIFO"]
        H2["LOW prio=2\nhead->tail FIFO"]
        H3["IDLE prio=3\nhead->tail FIFO"]
    end
    SCH["schedule()\ndequeue_ready()\npicks lowest prio# first"]
    H0 --> SCH
    H1 --> SCH
    H2 --> SCH
    H3 --> SCH


%% ---------------------------------------------------------------------------
%% Diagram 9: ML Inference Thread Execution Model
%% Section 12.1 — End-to-end execution flow of an inference workload thread
%% ---------------------------------------------------------------------------

graph TD
    START["Thread Created\nTHREAD_Create"] --> READY["READY State\nIn priority queue"]
    READY --> RUN["RUNNING\ncpu_context_switch"]
    RUN --> OP1["Execute Operator 1\nTimer measures start/end"]
    OP1 --> YIELD1["THREAD_Yield\nCooperative hand-off"]
    YIELD1 -->|context switch| MON[Monitor thread runs briefly]
    MON --> RUN2["Back to Inference\ncpu_context_switch"]
    RUN2 --> OP2[Execute Operator 2]
    OP2 --> SLEEP["THREAD_Sleep 50ms\nTimer wakes at tick"]
    SLEEP -->|SCHED_TimerTick| READY2[READY again]
    READY2 --> RUN3["RUNNING\ncpu_context_switch"]
    RUN3 --> DONE["Inference Complete\nTHREAD_Exit"]


%% ---------------------------------------------------------------------------
%% Diagram 10: Kernel Subsystem Dependencies
%% Section 12.2 — Build-time and runtime dependency graph between modules
%% ---------------------------------------------------------------------------

graph BT
    UART["uart.c\nPL011 Serial"] --> MAIN
    MMU["mmu.c\nMMU Tables"] --> MAIN
    GIC["gic.c\nInterrupt Ctrl"] --> MAIN
    TMR["timer.c\nARM Timer"] --> MAIN
    KMEM["kmem.c\nMemory Manager"] --> MAIN
    THREAD["thread.c\nScheduler"] --> MAIN
    KMEM --> THREAD
    TMR --> THREAD
    GIC --> MAIN
    ARCH["arch.h\nInline Primitives"] --> GIC
    ARCH --> THREAD
    ARCH --> TMR
    STR[string.c] --> KMEM
    MAIN["main.c\nkernel_main"]
