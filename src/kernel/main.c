/**
 * @file main.c
 * @brief MiniOS kernel entry point
 *
 * This is the first C function called after boot.S sets up
 * the stack and zeroes BSS. It initializes all hardware
 * subsystems and enters the idle loop.
 *
 * After this function completes initialization, the system
 * is ready for the ML runtime components (memory allocator,
 * graph parser, execution engine) to be built on top.
 */

#include "types.h"
#include "status.h"
#include "hal/uart.h"
#include "hal/mmu.h"
#include "onnx/onnx_demo.h"
#include "onnx/onnx_loader_demo.h"

/* ------------------------------------------------------------------ */
/*  External symbols from vectors.S                                   */
/* ------------------------------------------------------------------ */
extern void _vector_table(void);

/* ------------------------------------------------------------------ */
/*  Exception handler names for pretty-printing                       */
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
/*  Read Current Exception Level                                      */
/* ------------------------------------------------------------------ */
static inline uint32_t get_current_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (uint32_t)((el >> 2) & 0x3);
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
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  ESR_EL1 : ");
    HAL_UART_PutHex(esr);
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  ELR_EL1 : ");
    HAL_UART_PutHex(elr);
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  FAR_EL1 : ");
    HAL_UART_PutHex(far);
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  System halted.\n");

    /* Halt forever */
    while (1) {
        __asm__ volatile("wfe");
    }
}

/* ------------------------------------------------------------------ */
/*  Status code to string conversion                                 */
/* ------------------------------------------------------------------ */
const char* STATUS_ToString(Status status)
{
    switch (status) {
        case STATUS_OK:                      return "OK";
        case STATUS_ERROR_INVALID_ARGUMENT:  return "INVALID_ARGUMENT";
        case STATUS_ERROR_NOT_SUPPORTED:     return "NOT_SUPPORTED";
        case STATUS_ERROR_NOT_INITIALIZED:   return "NOT_INITIALIZED";
        case STATUS_ERROR_OUT_OF_MEMORY:     return "OUT_OF_MEMORY";
        case STATUS_ERROR_MEMORY_ALIGNMENT:  return "MEMORY_ALIGNMENT";
        case STATUS_ERROR_MEMORY_PROTECTION: return "MEMORY_PROTECTION";
        case STATUS_ERROR_HARDWARE_FAULT:    return "HARDWARE_FAULT";
        case STATUS_ERROR_TIMEOUT:           return "TIMEOUT";
        case STATUS_ERROR_EXECUTION_FAILED:  return "EXECUTION_FAILED";
        case STATUS_ERROR_EXECUTION_TIMEOUT: return "EXECUTION_TIMEOUT";
        case STATUS_ERROR_INVALID_GRAPH:     return "INVALID_GRAPH";
        case STATUS_ERROR_UNSUPPORTED_OPERATOR: return "UNSUPPORTED_OPERATOR";
        case STATUS_ERROR_SHAPE_MISMATCH:    return "SHAPE_MISMATCH";
        case STATUS_ERROR_COMM_FAILURE:      return "COMM_FAILURE";
        case STATUS_ERROR_CRC_MISMATCH:      return "CRC_MISMATCH";
        default:                             return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/*  Kernel entry point                                                */
/* ------------------------------------------------------------------ */
void kernel_main(void)
{
    Status status;

    /* ---- Step 1: Initialize UART for serial output ---- */
    status = HAL_UART_Init();

    /* Print boot banner */
    HAL_UART_PutString("\n");
    HAL_UART_PutString("======================================\n");
    HAL_UART_PutString("  MiniOS v0.1 - ARM64 Unikernel\n");
    HAL_UART_PutString("  ML Inference for ARM64 Devices\n");
    HAL_UART_PutString("======================================\n");
    HAL_UART_PutString("\n");

    /* Report UART status */
    HAL_UART_PutString("[BOOT] UART initialized: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* Report current exception level */
    uint32_t el = get_current_el();
    HAL_UART_PutString("[BOOT] Running at EL");
    HAL_UART_PutDec(el);
    HAL_UART_PutString("\n");

    /* ---- Step 2: Install exception vector table ---- */
    HAL_UART_PutString("[BOOT] Installing exception vectors...\n");
    install_vectors();
    HAL_UART_PutString("[BOOT] Exception vectors installed\n");

    /* ---- Step 3: Initialize MMU and caches ---- */
    HAL_UART_PutString("[BOOT] Initializing MMU...\n");
    status = HAL_MMU_Init();
    HAL_UART_PutString("[BOOT] MMU status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");

    /* ---- Boot complete ---- */
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] =============================\n");
    HAL_UART_PutString("[BOOT]  All subsystems initialized\n");
    HAL_UART_PutString("[BOOT]  Ready for C development!\n");
    HAL_UART_PutString("[BOOT] =============================\n");
    HAL_UART_PutString("\n");
    /* ---- Run ONNX Runtime Demos ---- */
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] Starting ONNX runtime demos...\n");
    HAL_UART_PutString("\n");
    
    /* Run model loading demo */
    ONNX_LoaderDemo();
    
    /* Run manual graph building demo (commented to save output space) */
    /* ONNX_RunDemos(); */
    
    /* ---- Idle loop ---- */
    HAL_UART_PutString("\n[BOOT] Entering idle loop...\n");
    HAL_UART_PutString("       (Ctrl+A then X to exit QEMU)\n");
    
    while (1) {
        __asm__ volatile("wfe");
    }
}
