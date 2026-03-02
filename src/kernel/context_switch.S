/*
 * context_switch.S — ARM64 context switch for MiniOS scheduler
 *
 * void context_switch(uint64_t** old_sp, uint64_t* new_sp);
 *
 * Saves callee-saved registers (x19–x30) onto the current stack,
 * stores the current SP into *old_sp, loads SP from new_sp,
 * restores callee-saved registers, and returns (via restored x30).
 *
 * Register layout on stack (12 registers × 8 bytes = 96 bytes):
 *   SP + 0:   x19
 *   SP + 8:   x20
 *   SP + 16:  x21
 *   SP + 24:  x22
 *   SP + 32:  x23
 *   SP + 40:  x24
 *   SP + 48:  x25
 *   SP + 56:  x26
 *   SP + 64:  x27
 *   SP + 72:  x28
 *   SP + 80:  x29 (frame pointer)
 *   SP + 88:  x30 (link register)
 *
 * Per AAPCS64, x19–x28 are callee-saved. x29 (FP) and x30 (LR)
 * are also preserved. Caller-saved registers (x0–x18) do not
 * need saving because the compiler handles them at call sites.
 */

.section .text
.global context_switch

context_switch:
    /*
     * x0 = uint64_t** old_sp  (pointer to where we save current SP)
     * x1 = uint64_t*  new_sp  (SP to restore)
     */

    /* ---- Save current context onto current stack ---- */
    /* Allocate 96 bytes for 12 registers */
    sub     sp, sp, #96

    /* Store callee-saved registers */
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]

    /* Save current SP to *old_sp */
    mov     x2, sp
    str     x2, [x0]

    /* ---- Restore new context ---- */
    /* Load new SP */
    mov     sp, x1

    /* Restore callee-saved registers */
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]

    /* Deallocate the frame */
    add     sp, sp, #96

    /* Return — x30 was restored, so this jumps to the
     * correct return address (either back to SCHED_Yield/Run
     * or into the task trampoline for first-time dispatch) */
    ret
