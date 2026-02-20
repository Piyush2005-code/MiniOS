/*
 * boot.S — ARM64 boot entry point for MiniOS
 *
 * This is the very first code executed after QEMU loads kernel.elf.
 * QEMU virt loads the kernel at 0x40000000 and jumps here.
 *
 * Boot sequence:
 *   1. Park secondary CPU cores (only core 0 proceeds)
 *   2. Detect current Exception Level
 *   3. If at EL2, drop to EL1
 *   4. Set up the stack pointer
 *   5. Zero the .bss section
 *   6. Branch to kernel_main() in C
 *
 * Register conventions on entry (QEMU virt):
 *   x0 = DTB address (ignored for now)
 *   x1-x3 = 0
 */

.section .text.boot
.global _start

_start:
    /* ------------------------------------------------------------ */
    /* 1. Park secondary cores: only core 0 proceeds                */
    /* ------------------------------------------------------------ */
    mrs     x0, mpidr_el1           /* Read Multiprocessor Affinity Register */
    and     x0, x0, #0xFF           /* Extract Aff0 (core ID) */
    cbz     x0, .Lprimary_core      /* If core 0, continue */

.Lpark:
    wfe                             /* Wait For Event (low-power halt) */
    b       .Lpark                  /* Loop forever */

.Lprimary_core:
    /* ------------------------------------------------------------ */
    /* 2. Detect current Exception Level                            */
    /* ------------------------------------------------------------ */
    mrs     x0, CurrentEL           /* Read current EL */
    lsr     x0, x0, #2             /* Shift to get EL number */
    cmp     x0, #3
    b.eq    .Lfrom_el3
    cmp     x0, #2
    b.eq    .Lfrom_el2
    b       .Lat_el1                /* Already at EL1 */

    /* ------------------------------------------------------------ */
    /* 3a. Drop from EL3 to EL1 (unlikely on QEMU virt)            */
    /* ------------------------------------------------------------ */
.Lfrom_el3:
    /* Configure EL2 to be AArch64 */
    mrs     x0, scr_el3
    orr     x0, x0, #(1 << 10)     /* RW bit: EL2 is AArch64 */
    orr     x0, x0, #(1 << 0)      /* NS bit: Non-secure */
    msr     scr_el3, x0

    /* Set return address and state for EL2 */
    adr     x0, .Lfrom_el2
    msr     elr_el3, x0
    mov     x0, #0x3C9              /* SPSR: D/A/I/F masked, EL2h */
    msr     spsr_el3, x0
    eret

    /* ------------------------------------------------------------ */
    /* 3b. Drop from EL2 to EL1                                     */
    /* ------------------------------------------------------------ */
.Lfrom_el2:
    /* Configure EL1 to be AArch64 */
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)     /* RW bit: EL1 is AArch64 */
    msr     hcr_el2, x0

    /* Disable EL2 trapping of FP/SIMD and system registers */
    mov     x0, #0x33FF
    bic     x0, x0, #(1 << 10)     /* Clear TFP bit: don't trap FP/SIMD */
    msr     cptr_el2, x0
    msr     hstr_el2, xzr          /* Don't trap system registers */

    /* Enable timer access from EL1 */
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3             /* EL1PCEN + EL1PCTEN */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr       /* Virtual offset = 0 */

    /* Initialize SCTLR_EL1 to a known state */
    mov     x0, #0x0C00            /* RES1 bits */
    movk    x0, #0x0800, lsl #16   /* RES1 bits */
    msr     sctlr_el1, x0          /* MMU off, caches off, alignment check off */

    /* Set SPSR for EL1: mask D/A/I/F, use EL1h (SP_EL1) */
    mov     x0, #0x3C5             /* DAIF masked, M[3:0] = 0b0101 = EL1h */
    msr     spsr_el2, x0

    /* Set return address to EL1 entry */
    adr     x0, .Lat_el1
    msr     elr_el2, x0

    eret                            /* Drop to EL1 */

    /* ------------------------------------------------------------ */
    /* 4. At EL1: Set up stack and BSS                              */
    /* ------------------------------------------------------------ */
.Lat_el1:
    /* Enable FP/SIMD access at EL1 (CPACR_EL1.FPEN = 0b11) */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* Set up the stack pointer (SP_EL1) */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* ------------------------------------------------------------ */
    /* 5. Zero the .bss section                                     */
    /* ------------------------------------------------------------ */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end

.Lbss_loop:
    cmp     x0, x1
    b.ge    .Lbss_done
    str     xzr, [x0], #8          /* Store zero, advance by 8 bytes */
    b       .Lbss_loop

.Lbss_done:
    /* ------------------------------------------------------------ */
    /* 6. Branch to C code                                          */
    /* ------------------------------------------------------------ */
    bl      kernel_main

    /* If kernel_main returns, halt */
.Lhalt:
    wfe
    b       .Lhalt
