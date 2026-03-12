/**
 * @file test_runner_main.c
 * @brief QEMU test runner — replaces kernel_main when TEST=1
 *
 * Runs all QEMU test modules in sequence.
 * Outputs "[TEST] <id> PASS/FAIL" per test.
 * Outputs "[SUITE] X PASS, Y FAIL" at the end.
 * Exits QEMU via semihosting SYS_EXIT after the summary line.
 *
 * To build: make TEST=1
 * To run:   make TEST=1 run
 */

#include "types.h"
#include "status.h"
#include "hal/uart.h"
#include "hal/mmu.h"
#include "hal/timer.h"
#include "hal/arch.h"
#include "hal/gic.h"
#include "kernel/kmem.h"
#include "kernel/thread.h"

extern void _vector_table(void);

/* ------------------------------------------------------------------ */
/*  Module runner declarations                                         */
/* ------------------------------------------------------------------ */
extern void run_uart_tests(int *pass, int *fail);
extern void run_timer_tests(int *pass, int *fail);
extern void run_mmu_tests(int *pass, int *fail);
extern void run_ctx_tests(int *pass, int *fail);
extern void run_exception_tests(int *pass, int *fail);
extern void run_system_tests(int *pass, int *fail);

/* ------------------------------------------------------------------ */
/*  Status string (normally in main.c; provide here for test build)   */
/* ------------------------------------------------------------------ */
const char *STATUS_ToString(Status s)
{
    switch (s) {
        case STATUS_OK:                        return "OK";
        case STATUS_ERROR_INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
        case STATUS_ERROR_NOT_SUPPORTED:       return "NOT_SUPPORTED";
        case STATUS_ERROR_NOT_INITIALIZED:     return "NOT_INITIALIZED";
        case STATUS_ERROR_OUT_OF_MEMORY:       return "OUT_OF_MEMORY";
        case STATUS_ERROR_MEMORY_ALIGNMENT:    return "MEMORY_ALIGNMENT";
        case STATUS_ERROR_MEMORY_PROTECTION:   return "MEMORY_PROTECTION";
        case STATUS_ERROR_HARDWARE_FAULT:      return "HARDWARE_FAULT";
        case STATUS_ERROR_TIMEOUT:             return "TIMEOUT";
        case STATUS_ERROR_EXECUTION_FAILED:    return "EXECUTION_FAILED";
        case STATUS_ERROR_EXECUTION_TIMEOUT:   return "EXECUTION_TIMEOUT";
        case STATUS_ERROR_INVALID_GRAPH:       return "INVALID_GRAPH";
        case STATUS_ERROR_UNSUPPORTED_OPERATOR:return "UNSUPPORTED_OPERATOR";
        case STATUS_ERROR_SHAPE_MISMATCH:      return "SHAPE_MISMATCH";
        case STATUS_ERROR_COMM_FAILURE:        return "COMM_FAILURE";
        case STATUS_ERROR_CRC_MISMATCH:        return "CRC_MISMATCH";
        case STATUS_ERROR_THREAD_LIMIT:        return "THREAD_LIMIT";
        case STATUS_ERROR_SCHEDULER_ACTIVE:    return "SCHEDULER_ACTIVE";
        case STATUS_ERROR_POOL_EXHAUSTED:      return "POOL_EXHAUSTED";
        default:                               return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/*  HAL_Exception_Handler stub for test build                         */
/* ------------------------------------------------------------------ */
void HAL_Exception_Handler(uint64_t id, uint64_t esr,
                           uint64_t elr, uint64_t far)
{
    HAL_UART_PutString("\n!!! TEST EXCEPTION id=");
    HAL_UART_PutDec(id);
    HAL_UART_PutString(" esr=");  HAL_UART_PutHex(esr);
    HAL_UART_PutString(" elr=");  HAL_UART_PutHex(elr);
    HAL_UART_PutString(" far=");  HAL_UART_PutHex(far);
    HAL_UART_PutString("\n");
    while (1) arch_wfe();
}

/* ------------------------------------------------------------------ */
/*  HAL_IRQ_Handler stub for test build                               */
/* ------------------------------------------------------------------ */
void HAL_IRQ_Handler(void)
{
    /* Acknowledge and ignore all IRQs during test runs */
}

/* ------------------------------------------------------------------ */
/*  QEMU semihosting exit                                              */
/*  SYS_EXIT = 0x18, reason ADP_Stopped_ApplicationExit = (0x20026)   */
/* ------------------------------------------------------------------ */
static void qemu_exit(int code)
{
    /* AArch64 semihosting via HLT #0xF000 */
    register uint64_t x0 asm("x0") = 0x18;        /* SYS_EXIT */
    register uint64_t x1 asm("x1") = (uint64_t)(code == 0 ? 0x20026UL : 1UL);
    __asm__ volatile(
        "hlt #0xF000\n"
        :: "r"(x0), "r"(x1)
    );
    /* Fallback: halt */
    while (1) arch_wfe();
}

/* ------------------------------------------------------------------ */
/*  kernel_main — replaced by this file in TEST=1 builds              */
/* ------------------------------------------------------------------ */
void kernel_main(void)
{
    int total_pass = 0, total_fail = 0;

    /* ---- Hardware init ---- */
    HAL_UART_Init();

    HAL_UART_PutString("\n");
    HAL_UART_PutString("========================================\n");
    HAL_UART_PutString("  MiniOS Test Suite — QEMU Runner\n");
    HAL_UART_PutString("========================================\n\n");

    /* Install exception vectors */
    uint64_t vbar = (uint64_t)(uintptr_t)&_vector_table;
    __asm__ volatile("msr vbar_el1, %0" :: "r"(vbar));
    __asm__ volatile("isb");

    /* MMU */
    HAL_MMU_Init();

    /* Memory */
    KMEM_Init();

    /* Timer */
    HAL_Timer_Init();

    /* Scheduler */
    SCHED_Init();

    /* ---- Run test modules ---- */

    HAL_UART_PutString("\n--- UT-UART ---\n");
    run_uart_tests(&total_pass, &total_fail);

    HAL_UART_PutString("\n--- UT-TIMER ---\n");
    run_timer_tests(&total_pass, &total_fail);

    HAL_UART_PutString("\n--- UT-MMU ---\n");
    run_mmu_tests(&total_pass, &total_fail);

    HAL_UART_PutString("\n--- UT-CTX ---\n");
    run_ctx_tests(&total_pass, &total_fail);

    HAL_UART_PutString("\n--- CT-EXC ---\n");
    run_exception_tests(&total_pass, &total_fail);

    HAL_UART_PutString("\n--- ST-SYSTEM ---\n");
    run_system_tests(&total_pass, &total_fail);

    /* ---- Summary ---- */
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[SUITE] ");
    HAL_UART_PutDec((uint64_t)total_pass);
    HAL_UART_PutString(" PASS, ");
    HAL_UART_PutDec((uint64_t)total_fail);
    HAL_UART_PutString(" FAIL\n");

    /* ---- Exit QEMU ---- */
    qemu_exit(total_fail == 0 ? 0 : 1);
}
