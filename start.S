/*
 * start.S - ARM64 Boot Code for ML Inference Unikernel
 * 
 * This file handles:
 * - Exception level transitions (EL3 -> EL2 -> EL1)
 * - Stack initialization
 * - Exception vector table installation
 * - FP/SIMD (NEON) enablement
 * - MMU initialization and jump to C code
 */

.section ".text.boot"

.global _start

/*
 * Entry point - QEMU -kernel starts at EL2
 * Raspberry Pi firmware may start at EL3 or EL2
 */
_start:
    /* Determine current exception level */
    mrs     x0, CurrentEL
    and     x0, x0, #0xC        /* Extract EL field (bits 3:2) */
    cmp     x0, #0xC            /* Are we at EL3? */
    b.eq    from_el3
    cmp     x0, #0x8            /* Are we at EL2? */
    b.eq    from_el2
    cmp     x0, #0x4            /* Are we at EL1? */
    b.eq    from_el1
    /* Unknown EL - hang */
    b       .

from_el3:
    /*
     * Configure EL3 to drop to EL2
     */
    /* Set up SCR_EL3 - Secure Configuration Register */
    mov     x0, #0x531          /* RW=1 (EL2 is AArch64), NS=1, SMD=1, HCE=1, SIF=1 */
    msr     scr_el3, x0
    
    /* Set up SPSR_EL3 for exception return to EL2h */
    mov     x0, #0x3c9          /* EL2h (SPSel=1), mask debug exceptions, IRQ, FIQ */
    msr     spsr_el3, x0
    
    /* Set ELR_EL3 to from_el2 label */
    adr     x0, from_el2
    msr     elr_el3, x0
    
    eret                        /* Exception return to EL2 */

from_el2:
    /*
     * Configure EL2 to drop to EL1
     */
    /* Set up HCR_EL2 - Hypervisor Configuration Register */
    mov     x0, #(1 << 31)      /* RW=1: EL1 is AArch64 */
    msr     hcr_el2, x0
    
    /* Set up SPSR_EL2 for exception return to EL1h */
    mov     x0, #0x3c5          /* EL1h (SPSel=1, M=0b0101), mask debug, IRQ, FIQ */
    msr     spsr_el2, x0
    
    /* Set ELR_EL2 to from_el1 label */
    adr     x0, from_el1
    msr     elr_el2, x0
    
    eret                        /* Exception return to EL1 */

from_el1:
    /*
     * Now running at EL1 - set up the execution environment
     */
    
    /* Zero the BSS section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
zero_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       zero_bss
bss_done:

    /* Set up stack pointer - 64 KB stack in BSS */
    ldr     x0, =stack_top
    mov     sp, x0
    
    /* Install exception vector table */
    ldr     x0, =vector_table
    msr     vbar_el1, x0
    
    /*
     * Enable FP/SIMD (NEON) - required for ML inference
     * Set CPACR_EL1.FPEN to 0b11 (no trapping at EL1 and EL0)
     */
    mov     x0, #(0x3 << 20)    /* FPEN = 0b11 */
    msr     cpacr_el1, x0
    isb
    
    /*
     * Initialize MMU (implemented in C)
     */
    bl      mmu_init
    
    /*
     * Jump to C entry point
     */
    bl      main
    
    /* If main returns, hang */
hang:
    wfi
    b       hang

/*
 * Exception Vector Table
 * ARMv8-A requires 16 entries, each aligned to 128 bytes
 * 4 exception types × 4 exception levels = 16 entries
 */
.align 11                       /* 2^11 = 2048 byte alignment */
vector_table:
    /* Current EL with SP0 */
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    
    /* Current EL with SPx */
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    
    /* Lower EL using AArch64 */
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    
    /* Lower EL using AArch32 */
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler
    .align 7
    b       exception_handler

/*
 * Simple exception handler - prints error message and hangs
 */
exception_handler:
    /* Save x0, x1 for UART output */
    stp     x0, x1, [sp, #-16]!
    
    /* Print exception message */
    adr     x0, exception_msg
    bl      print
    
    /* Restore registers */
    ldp     x0, x1, [sp], #16
    
    /* Hang forever */
1:  wfi
    b       1b

/*
 * Data Section
 */
.section ".rodata"
exception_msg:
    .asciz  "\n*** EXCEPTION OCCURRED - SYSTEM HALTED ***\n"

/*
 * BSS Section - uninitialized data
 */
.section ".bss"
.align 12                       /* Page align for MMU */

/* 64 KB stack */
.align 16
stack_bottom:
    .skip   65536
stack_top:
