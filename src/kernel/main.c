/**
 * @file main.c
 * @brief MiniOS kernel entry point
 *
 * Initializes all hardware subsystems, memory management, scheduling,
 * and launches three threads: inference demo, ONNX demo, and monitor.
 */

#include "types.h"
#include "status.h"
#include "hal/uart.h"
#include "hal/mmu.h"
#include "hal/gic.h"
#include "hal/timer.h"
#include "kernel/kmem.h"
#include "kernel/thread.h"
#include "onnx/onnx_loader_demo.h"
#include "onnx/onnx_test.h"

/* ------------------------------------------------------------------ */
/*  External symbols                                                   */
/* ------------------------------------------------------------------ */
extern void _vector_table(void);

/* ------------------------------------------------------------------ */
/*  Arch helpers (provided by context.S / boot code)                  */
/* ------------------------------------------------------------------ */
static inline void install_vectors(void)
{
    uint64_t vbar = (uint64_t)(uintptr_t)&_vector_table;
    __asm__ volatile("msr vbar_el1, %0" :: "r"(vbar));
    __asm__ volatile("isb");
}

static inline uint32_t arch_get_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (uint32_t)((el >> 2) & 0x3);
}

static inline void arch_enable_irq(void)
{
    __asm__ volatile("msr daifclr, #2");
}

/* ------------------------------------------------------------------ */
/*  Exception handler names                                           */
/* ------------------------------------------------------------------ */
static const char* exception_names[] = {
    "EL1 SP0 Synchronous",     /*  0 */
    "EL1 SP0 IRQ",             /*  1 */
    "EL1 SP0 FIQ",             /*  2 */
    "EL1 SP0 SError",          /*  3 */
    "EL1 SPx Synchronous",     /*  4 */
    "EL1 SPx IRQ",             /*  5 */
    "EL1 SPx FIQ",             /*  6 */
    "EL1 SPx SError",          /*  7 */
    "EL0 AArch64 Synchronous", /*  8 */
    "EL0 AArch64 IRQ",         /*  9 */
    "EL0 AArch64 FIQ",         /* 10 */
    "EL0 AArch64 SError",      /* 11 */
    "EL0 AArch32 Synchronous", /* 12 */
    "EL0 AArch32 IRQ",         /* 13 */
    "EL0 AArch32 FIQ",         /* 14 */
    "EL0 AArch32 SError",      /* 15 */
};

/* ------------------------------------------------------------------ */
/*  Exception handler (called from vectors.S)                         */
/* ------------------------------------------------------------------ */
void HAL_Exception_Handler(uint64_t id, uint64_t esr,
                            uint64_t elr, uint64_t far)
{
    HAL_UART_PutString("\n!!! EXCEPTION: ");
    if (id < 16) {
        HAL_UART_PutString(exception_names[id]);
    } else {
        HAL_UART_PutString("Unknown (");
        HAL_UART_PutDec((uint32_t)id);
        HAL_UART_PutString(")");
    }
    HAL_UART_PutString("\n");
    HAL_UART_PutString("  ESR_EL1 : "); HAL_UART_PutHex(esr); HAL_UART_PutString("\n");
    HAL_UART_PutString("  ELR_EL1 : "); HAL_UART_PutHex(elr); HAL_UART_PutString("\n");
    HAL_UART_PutString("  FAR_EL1 : "); HAL_UART_PutHex(far); HAL_UART_PutString("\n");
    HAL_UART_PutString("  System halted.\n");
    while (1) { __asm__ volatile("wfe"); }
}

/* ------------------------------------------------------------------ */
/*  Status code to string conversion                                  */
/* ------------------------------------------------------------------ */
const char* STATUS_ToString(Status status)
{
    switch (status) {
        case STATUS_OK:                         return "OK";
        case STATUS_ERROR_INVALID_ARGUMENT:     return "INVALID_ARGUMENT";
        case STATUS_ERROR_NOT_SUPPORTED:        return "NOT_SUPPORTED";
        case STATUS_ERROR_NOT_INITIALIZED:      return "NOT_INITIALIZED";
        case STATUS_ERROR_OUT_OF_MEMORY:        return "OUT_OF_MEMORY";
        case STATUS_ERROR_MEMORY_ALIGNMENT:     return "MEMORY_ALIGNMENT";
        case STATUS_ERROR_MEMORY_PROTECTION:    return "MEMORY_PROTECTION";
        case STATUS_ERROR_HARDWARE_FAULT:       return "HARDWARE_FAULT";
        case STATUS_ERROR_TIMEOUT:              return "TIMEOUT";
        case STATUS_ERROR_EXECUTION_FAILED:     return "EXECUTION_FAILED";
        case STATUS_ERROR_EXECUTION_TIMEOUT:    return "EXECUTION_TIMEOUT";
        case STATUS_ERROR_INVALID_GRAPH:        return "INVALID_GRAPH";
        case STATUS_ERROR_UNSUPPORTED_OPERATOR: return "UNSUPPORTED_OPERATOR";
        case STATUS_ERROR_SHAPE_MISMATCH:       return "SHAPE_MISMATCH";
        case STATUS_ERROR_COMM_FAILURE:         return "COMM_FAILURE";
        case STATUS_ERROR_CRC_MISMATCH:         return "CRC_MISMATCH";
        case STATUS_ERROR_THREAD_LIMIT:         return "THREAD_LIMIT";
        case STATUS_ERROR_SCHEDULER_ACTIVE:     return "SCHEDULER_ACTIVE";
        case STATUS_ERROR_POOL_EXHAUSTED:       return "POOL_EXHAUSTED";
        default:                                return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/*  IRQ dispatcher (called from vectors.S _irq_handler_full)          */
/* ------------------------------------------------------------------ */
void HAL_IRQ_Handler(void)
{
    uint32_t iar = HAL_GIC_Acknowledge();
    uint32_t irq_id = iar & 0x3FF;

    if (irq_id == IRQ_TIMER_PHYS) {
        HAL_Timer_HandleIRQ();
        SCHED_TimerTick();
    } else if (irq_id < 1020) {
        HAL_UART_PutString("[IRQ] Unhandled INTID ");
        HAL_UART_PutDec(irq_id);
        HAL_UART_PutString("\n");
    }

    if (irq_id < 1020) {
        HAL_GIC_EndOfInterrupt(iar);
    }
}

/* ------------------------------------------------------------------ */
/*  Demo thread: simulated ML inference workload                      */
/* ------------------------------------------------------------------ */
static void inference_thread(void *arg)
{
    (void)arg;
    const uint32_t iterations = 5;

    HAL_UART_PutString("[INFER] Inference thread started\n");

    for (uint32_t i = 0; i < iterations; i++) {
        uint64_t start = HAL_Timer_GetTicks();

        HAL_UART_PutString("[INFER] Iteration ");
        HAL_UART_PutDec(i + 1);
        HAL_UART_PutString("/");
        HAL_UART_PutDec(iterations);

        HAL_Timer_DelayUs(50000);   /* Simulate ~50 ms operator work */

        uint64_t elapsed = HAL_Timer_GetElapsedUs(start);
        HAL_UART_PutString("  (");
        HAL_UART_PutDec((uint32_t)(elapsed / 1000));
        HAL_UART_PutString(" ms)\n");

        THREAD_Yield();
    }

    HAL_UART_PutString("[INFER] Inference complete\n");
}

/* ------------------------------------------------------------------ */
/*  Demo thread: ONNX loader demo                                     */
/* ------------------------------------------------------------------ */
static void onnx_thread(void *arg)
{
    (void)arg;

    HAL_UART_PutString("[ONNX ] ONNX demo thread started\n");

    /* Run loader demo */
    ONNX_LoaderDemo();

    HAL_UART_PutString("[ONNX ] ONNX demo thread complete\n");
}


/* ------------------------------------------------------------------ */
/*  Test thread: ONNX Unit/Integration/Component tests                */
/* ------------------------------------------------------------------ */
static void test_thread(void *arg)
{
    (void)arg;
    ONNX_RunAllTests();
}

/* ------------------------------------------------------------------ */
/*  Demo thread: memory & thread monitor                              */
/* ------------------------------------------------------------------ */
static void monitor_thread(void *arg)
{
    (void)arg;
    uint32_t tick = 0;

    HAL_UART_PutString("[MON  ] Monitor thread started\n");

    while (tick < 8) {
        kmem_stats_t stats;
        KMEM_GetStats(&stats);

        HAL_UART_PutString("[MON  ] tick=");
        HAL_UART_PutDec(tick);
        HAL_UART_PutString("  heap ");
        HAL_UART_PutDec((uint32_t)(stats.heap_used / 1024));
        HAL_UART_PutString("KB / ");
        HAL_UART_PutDec((uint32_t)(stats.heap_total / 1024));
        HAL_UART_PutString("KB  threads=");
        HAL_UART_PutDec(SCHED_GetThreadCount());
        HAL_UART_PutString("  uptime=");
        HAL_UART_PutDec((uint32_t)SCHED_GetUptime());
        HAL_UART_PutString("ms\n");

        tick++;
        THREAD_Sleep(100);   /* Sleep 100 ms */
    }

    HAL_UART_PutString("[MON  ] Monitor exiting\n");
}

/* ------------------------------------------------------------------ */
/*  Kernel entry point                                                */
/* ------------------------------------------------------------------ */
void kernel_main(void)
{
    Status status;

    /* ---- Step 1: Initialize UART ---- */
    status = HAL_UART_Init();

    HAL_UART_PutString("\n");
    HAL_UART_PutString("Hello, Piyush!!");
    HAL_UART_PutString("======================================\n");
    HAL_UART_PutString("  MiniOS v0.2 - ARM64 Unikernel\n");
    HAL_UART_PutString("  ML Inference with Multithreading\n");
    HAL_UART_PutString("======================================\n");
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] UART initialized: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    uint32_t el = arch_get_el();
    HAL_UART_PutString("[BOOT] Running at EL");
    HAL_UART_PutDec(el);
    HAL_UART_PutString("\n");

    /* ---- Step 2: Install exception vectors ---- */
    HAL_UART_PutString("[BOOT] Installing exception vectors...\n");
    install_vectors();
    HAL_UART_PutString("[BOOT] Exception vectors installed\n");

    /* ---- Step 3: Initialize MMU ---- */
    HAL_UART_PutString("[BOOT] Initializing MMU...\n");
    status = HAL_MMU_Init();
    HAL_UART_PutString("[BOOT] MMU status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* ---- Step 4: Initialize kernel memory manager ---- */
    HAL_UART_PutString("[BOOT] Initializing memory manager...\n");
    status = KMEM_Init();
    HAL_UART_PutString("[BOOT] KMEM status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] Heap free: ");
    HAL_UART_PutDec((uint32_t)(KMEM_GetFreeSpace() / 1024));
    HAL_UART_PutString(" KB\n");

    /* ---- Step 5: Initialize GIC ---- */
    HAL_UART_PutString("[BOOT] Initializing GIC...\n");
    status = HAL_GIC_Init();
    HAL_UART_PutString("[BOOT] GIC status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* ---- Step 6: Initialize Timer ---- */
    HAL_UART_PutString("[BOOT] Initializing timer...\n");
    status = HAL_Timer_Init();
    HAL_Timer_SetInterval(10000);   /* 10 ms tick */
    HAL_GIC_EnableIRQ(IRQ_TIMER_PHYS);
    HAL_GIC_SetPriority(IRQ_TIMER_PHYS, 0xA0);
    HAL_UART_PutString("[BOOT] Timer status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("  (10ms tick)\n");

    /* ---- Step 7: Initialize Scheduler ---- */
    HAL_UART_PutString("[BOOT] Initializing scheduler...\n");
    status = SCHED_Init();
    HAL_UART_PutString("[BOOT] Scheduler status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* ---- Step 8: Create application threads ---- */
    HAL_UART_PutString("[BOOT] Creating threads...\n");
    thread_t *t_infer = NULL;
    thread_t *t_onnx  = NULL;
    thread_t *t_mon   = NULL;

    status = THREAD_Create(&t_infer, "inference", inference_thread,
                           NULL, THREAD_PRIORITY_NORMAL, 0);
    HAL_UART_PutString("[BOOT]   inference: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    status = THREAD_Create(&t_onnx, "onnx", onnx_thread,
                           NULL, THREAD_PRIORITY_NORMAL, 0);
    HAL_UART_PutString("[BOOT]   onnx     : ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    status = THREAD_Create(&t_mon, "monitor", monitor_thread,
                           NULL, THREAD_PRIORITY_LOW, 0);
    HAL_UART_PutString("[BOOT]   monitor  : ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    thread_t *t_test = NULL;
    status = THREAD_Create(&t_test, "test", test_thread,
                           NULL, THREAD_PRIORITY_NORMAL, 0);
    HAL_UART_PutString("[BOOT]   test     : ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* ---- Step 9: Enable interrupts and start timer ---- */
    HAL_UART_PutString("[BOOT] Enabling interrupts...\n");
    HAL_Timer_Enable();
    arch_enable_irq();

    /* ---- Step 10: Start scheduler (does not return) ---- */
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] ==============================\n");
    HAL_UART_PutString("[BOOT]  Starting scheduler\n");
    HAL_UART_PutString("[BOOT]  Threads: ");
    HAL_UART_PutDec(SCHED_GetThreadCount());
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] ==============================\n");
    HAL_UART_PutString("\n");

    SCHED_Start();    /* Enters idle loop — never returns */
}
