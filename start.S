//
// Minimal ARM64 Boot Code for ML Inference Unikernel
// Fixed: .rept used instead of .irpc, semihosting trap uses hlt 0xf000
//
.section ".text.boot"
.global _start

// Semihosting definitions (for QEMU debug output)
.equ SYS_WRITE0,      0x04
.equ SYS_EXIT,        0x18

//
// Exception Vector Table
// Aligned to 2KB (0x800), 16 entries, each 0x80 bytes apart.
//
.align 11
vector_table:
    .rept 16
    .align 7
    b exception_handler
    .endr

//
// Entry point
//
_start:
    // Ensure we are in a high Exception Level (EL3 or EL2)
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ge    1f
    // If we are in EL1, print error and hang (should not happen on QEMU -kernel)
    adr     x0, el_error_msg
    bl      semihosting_puts
    b       halt_loop
1:
    // Setup stack pointer for EL1 (we will drop to EL1)
    adrp    x0, stack_top
    add     x0, x0, :lo12:stack_top
    mov     sp, x0

    // If we are at EL3, drop to EL2, then EL1
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #3
    b.ne    try_el2

    // Configure EL3
    // Set SCR_EL3: NS=1, RW=1 (EL2 is AArch64), HCE=1, FIQ/IRQ masked
    mov     x0, #0b101011
    msr     scr_el3, x0
    // Set SPSR_EL3: Mask DAIF, EL2h mode
    mov     x0, #0x3c9
    msr     spsr_el3, x0
    // Set return address to EL2 entry
    adr     x0, el2_entry
    msr     elr_el3, x0
    eret

try_el2:
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    setup_el1

el2_entry:
    // Configure EL2
    // Set HCR_EL2: RW=1 (EL1 is AArch64)
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    // Set SPSR_EL2: Mask DAIF, EL1h mode
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    // Set return address to EL1 entry
    adr     x0, setup_el1
    msr     elr_el2, x0
    eret

setup_el1:
    // --- We are now at EL1 ---
    // Install exception vector table
    adr     x0, vector_table
    msr     vbar_el1, x0

    // Enable FP/SIMD (NEON) - CRITICAL for ML!
    mov     x0, #(0b11 << 20)
    msr     cpacr_el1, x0

    // Setup MMU (Flat map / Identity map)
    bl      mmu_init

    // Jump to C code
    bl      main

    // If main returns, print message and exit via semihosting (QEMU)
    adr     x0, exit_msg
    bl      semihosting_puts
    mov     x0, SYS_EXIT
    mov     x1, 0
    hlt     0xf000

halt_loop:
    wfi
    b       halt_loop

//
// Helper: Print string via semihosting (for QEMU debugging)
//
semihosting_puts:
    mov     x1, x0
    mov     x0, SYS_WRITE0
    hlt     0xf000
    ret

exception_handler:
    // For now, just print error and halt
    adr     x0, excp_msg
    bl      semihosting_puts
    b       halt_loop

.section .rodata
el_error_msg:
    .asciz "Error: Started in EL1 or lower.\n"
exit_msg:
    .asciz "\nReturned from main. Exiting.\n"
excp_msg:
    .asciz "\nUnexpected exception!\n"

.section .bss
.align 4
stack:
    .skip 0x10000          // 64KB stack
stack_top: