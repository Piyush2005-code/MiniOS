/*
 * vectors.S — ARM64 exception vector table for MiniOS
 *
 * ARMv8-A exception vector table layout:
 *   - 16 entries, each 128 bytes (32 instructions max)
 *   - 4 groups of 4 vectors:
 *     Group 0: Current EL with SP0
 *     Group 1: Current EL with SPx (our normal mode)
 *     Group 2: Lower EL using AArch64
 *     Group 3: Lower EL using AArch32
 *   - Each group has: Synchronous, IRQ, FIQ, SError
 *
 * The table must be 2KB (0x800) aligned.
 *
 * Initial implementation: all vectors print the exception type
 * and halt. Real handlers will be added as subsystems are built.
 */

.section .text

/* ------------------------------------------------------------------ */
/*  Helper macro: each vector entry is 128 bytes (0x80)               */
/* ------------------------------------------------------------------ */
.macro VECTOR_ENTRY label
.balign 128
\label:
.endm

/* ------------------------------------------------------------------ */
/*  Exception handler stub: save state, print, halt                   */
/*  We use a simple approach: branch to a common handler              */
/*  with the exception ID in x0.                                      */
/* ------------------------------------------------------------------ */
.macro EXCEPTION_STUB id
    mov     x0, #\id
    b       _exception_handler
.endm

/* ------------------------------------------------------------------ */
/*  Vector Table                                                      */
/* ------------------------------------------------------------------ */

.balign 2048                        /* 2KB alignment required */
.global _vector_table
_vector_table:

    /* ---- Current EL with SP0 ---- */
    VECTOR_ENTRY _vec_el1_sp0_sync
        EXCEPTION_STUB 0

    VECTOR_ENTRY _vec_el1_sp0_irq
        EXCEPTION_STUB 1

    VECTOR_ENTRY _vec_el1_sp0_fiq
        EXCEPTION_STUB 2

    VECTOR_ENTRY _vec_el1_sp0_serror
        EXCEPTION_STUB 3

    /* ---- Current EL with SPx (our normal mode) ---- */
    VECTOR_ENTRY _vec_el1_spx_sync
        EXCEPTION_STUB 4

    VECTOR_ENTRY _vec_el1_spx_irq
        b       _irq_handler_full

    VECTOR_ENTRY _vec_el1_spx_fiq
        EXCEPTION_STUB 6

    VECTOR_ENTRY _vec_el1_spx_serror
        EXCEPTION_STUB 7

    /* ---- Lower EL using AArch64 ---- */
    VECTOR_ENTRY _vec_el0_64_sync
        EXCEPTION_STUB 8

    VECTOR_ENTRY _vec_el0_64_irq
        EXCEPTION_STUB 9

    VECTOR_ENTRY _vec_el0_64_fiq
        EXCEPTION_STUB 10

    VECTOR_ENTRY _vec_el0_64_serror
        EXCEPTION_STUB 11

    /* ---- Lower EL using AArch32 ---- */
    VECTOR_ENTRY _vec_el0_32_sync
        EXCEPTION_STUB 12

    VECTOR_ENTRY _vec_el0_32_irq
        EXCEPTION_STUB 13

    VECTOR_ENTRY _vec_el0_32_fiq
        EXCEPTION_STUB 14

    VECTOR_ENTRY _vec_el0_32_serror
        EXCEPTION_STUB 15

/* ------------------------------------------------------------------ */
/*  Common exception handler                                          */
/*  x0 = exception ID                                                 */
/*  Prints exception info via UART and halts                          */
/* ------------------------------------------------------------------ */
.global _exception_handler
_exception_handler:
    /* Save the exception ID */
    mov     x19, x0

    /* Read useful exception state registers */
    mrs     x20, esr_el1            /* Exception Syndrome Register */
    mrs     x21, elr_el1            /* Exception Link Register (return address) */
    mrs     x22, far_el1            /* Fault Address Register */

    /*
     * Call the C-level exception handler:
     *   void HAL_Exception_Handler(uint64_t id, uint64_t esr,
     *                              uint64_t elr, uint64_t far);
     */
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    mov     x3, x22
    bl      HAL_Exception_Handler

    /* Halt — never return */
.Lexc_halt:
    wfe
    b       .Lexc_halt


/* ------------------------------------------------------------------ */
/*  Full IRQ handler for EL1 SPx                                      */
/*                                                                    */
/*  Saves all general-purpose registers, ELR_EL1, and SPSR_EL1       */
/*  to the current stack, calls the C-level IRQ dispatcher, then      */
/*  restores everything and returns with eret.                        */
/*                                                                    */
/*  This is a non-preemptive handler: the interrupted thread's        */
/*  execution resumes after the handler returns. Thread switching     */
/*  only happens at cooperative yield points.                         */
/*                                                                    */
/*  Stack frame layout (272 bytes, 16-byte aligned):                  */
/*    [sp, #0]   - [sp, #240]: x0-x30 (31 regs, 248 bytes)          */
/*    [sp, #248] - [sp, #263]: ELR_EL1, SPSR_EL1                     */
/*    [sp, #264] - [sp, #271]: padding                               */
/* ------------------------------------------------------------------ */

.global _irq_handler_full
_irq_handler_full:
    sub     sp, sp, #272

    /* Save x0, x1 first (we need them as scratch for ELR/SPSR) */
    stp     x0, x1, [sp, #0]

    /* Read and save ELR/SPSR before clobbering any more regs */
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #248]

    /* Save remaining general-purpose registers */
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]

    /* Call C-level IRQ dispatcher (defined in main.c) */
    bl      HAL_IRQ_Handler

    /* Restore ELR and SPSR */
    ldp     x0, x1, [sp, #248]
    msr     elr_el1, x0
    msr     spsr_el1, x1

    /* Restore all general-purpose registers */
    ldp     x0, x1,   [sp, #0]
    ldp     x2, x3,   [sp, #16]
    ldp     x4, x5,   [sp, #32]
    ldp     x6, x7,   [sp, #48]
    ldp     x8, x9,   [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30,      [sp, #240]

    add     sp, sp, #272
    eret
