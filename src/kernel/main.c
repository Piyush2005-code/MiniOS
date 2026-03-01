/**
 * @file main.c
 * @brief MiniOS kernel entry point
 *
 * Initialises hardware subsystems in order, then enters the idle WFE loop.
 * Memory manager (KMEM) is now integrated.  GIC and Timer will be added
 * in the following commit once the HAL drivers are verified.
 */

#include "types.h"
#include "status.h"
#include "hal/uart.h"
#include "hal/mmu.h"
#include "hal/arch.h"
#include "kernel/kmem.h"

/* External symbol from vectors.S */
extern void _vector_table(void);

/* ------------------------------------------------------------------ */
/*  Exception handler name table                                      */
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
/*  Install exception vector table                                    */
/* ------------------------------------------------------------------ */
static inline void install_vectors(void)
{
    uint64_t vbar = (uint64_t)(uintptr_t)&_vector_table;
    __asm__ volatile("msr vbar_el1, %0" :: "r"(vbar));
    __asm__ volatile("isb");
}

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
        HAL_UART_PutDec(id);
        HAL_UART_PutString(")");
    }
    HAL_UART_PutString("\n  ESR_EL1 : ");
    HAL_UART_PutHex(esr);
    HAL_UART_PutString("\n  ELR_EL1 : ");
    HAL_UART_PutHex(elr);
    HAL_UART_PutString("\n  FAR_EL1 : ");
    HAL_UART_PutHex(far);
    HAL_UART_PutString("\n  System halted.\n");
    while (1) { __asm__ volatile("wfe"); }
}

/* ------------------------------------------------------------------ */
/*  Status code to string                                             */
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
        case STATUS_ERROR_POOL_EXHAUSTED:       return "POOL_EXHAUSTED";
        default:                                return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/*  Kernel entry point                                                */
/* ------------------------------------------------------------------ */
void kernel_main(void)
{
    Status status;

    /* 1. UART */
    status = HAL_UART_Init();
    HAL_UART_PutString("\n======================================\n");
    HAL_UART_PutString("  MiniOS v0.2 - ARM64 Unikernel\n");
    HAL_UART_PutString("  Kernel API Sprint 1\n");
    HAL_UART_PutString("======================================\n\n");
    HAL_UART_PutString("[BOOT] UART: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* Report EL */
    HAL_UART_PutString("[BOOT] EL: ");
    HAL_UART_PutDec(arch_get_el());
    HAL_UART_PutString("\n");

    /* 2. Exception vectors */
    install_vectors();
    HAL_UART_PutString("[BOOT] Exception vectors installed\n");

    /* 3. MMU */
    status = HAL_MMU_Init();
    HAL_UART_PutString("[BOOT] MMU: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* 4. Kernel memory manager */
    status = KMEM_Init();
    HAL_UART_PutString("[BOOT] KMEM: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("  (free: ");
    HAL_UART_PutDec((uint32_t)(KMEM_GetFreeSpace() / 1024));
    HAL_UART_PutString(" KB)\n");

    /* GIC and Timer will be integrated in the next commit */

    HAL_UART_PutString("\n[BOOT] Kernel API layer ready\n");
    HAL_UART_PutString("[BOOT] Entering idle loop (WFE)...\n");
    HAL_UART_PutString("       (Ctrl+A then X to exit QEMU)\n");

    while (1) {
        __asm__ volatile("wfe");
    }
}
