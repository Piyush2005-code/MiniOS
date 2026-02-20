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
        EXCEPTION_STUB 5

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
