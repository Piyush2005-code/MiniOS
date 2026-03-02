/*
 * context.S — ARM64 context switch routines for MiniOS
 *
 * Provides cooperative context switching between threads.
 * Only callee-saved registers are saved/restored because
 * the switch is always initiated by a C function call
 * (THREAD_Yield / THREAD_Sleep / THREAD_Exit), so caller-
 * saved registers are already preserved by the caller.
 *
 * cpu_context_t layout (must match thread.h):
 *   Offset  0: x19     Offset 48: x25
 *   Offset  8: x20     Offset 56: x26
 *   Offset 16: x21     Offset 64: x27
 *   Offset 24: x22     Offset 72: x28
 *   Offset 32: x23     Offset 80: fp (x29)
 *   Offset 40: x24     Offset 88: lr (x30)
 *                       Offset 96: sp
 *   Total: 104 bytes
 */

.section .text

/* ------------------------------------------------------------------ */
/*  cpu_context_switch(old_ctx, new_ctx)                              */
/*                                                                    */
/*  x0 = pointer to old thread's cpu_context_t (save current state)   */
/*  x1 = pointer to new thread's cpu_context_t (restore new state)    */
/*                                                                    */
/*  Saves callee-saved registers + SP to old_ctx,                     */
/*  restores them from new_ctx, and returns (via LR) into the         */
/*  new thread's execution flow.                                      */
/* ------------------------------------------------------------------ */

.global cpu_context_switch
cpu_context_switch:
    /* ---- Save current thread's context to old_ctx (x0) ---- */
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]     /* fp, lr */
    mov     x9, sp
    str     x9, [x0, #96]           /* sp */

    /* ---- Restore new thread's context from new_ctx (x1) ---- */
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]     /* fp, lr */
    ldr     x9, [x1, #96]
    mov     sp, x9                   /* sp */

    /*
     * Return via the new thread's LR (x30).
     *
     * For an already-running thread: returns to the instruction
     * after its previous cpu_context_switch call.
     *
     * For a brand-new thread: LR = _thread_entry_trampoline,
     * which sets up arguments and calls the thread function.
     */
    ret


/* ------------------------------------------------------------------ */
/*  _thread_entry_trampoline                                          */
/*                                                                    */
/*  Entry point for newly created threads on their first schedule.    */
/*  When cpu_context_switch restores the initial context and does     */
/*  'ret', execution arrives here with:                               */
/*    x19 = thread function pointer                                   */
/*    x20 = thread argument (void *arg)                               */
/*                                                                    */
/*  We enable IRQs (they were disabled during context switch),        */
/*  call the thread function, and clean up if it returns.             */
/* ------------------------------------------------------------------ */

.global _thread_entry_trampoline
_thread_entry_trampoline:
    /* Enable IRQs for the new thread */
    msr     daifclr, #2

    /* Call thread_func(arg) */
    mov     x0, x20                 /* x0 = arg */
    blr     x19                     /* call func(arg) */

    /* If the thread function returns, exit cleanly */
    bl      THREAD_Exit

    /* Should never reach here — THREAD_Exit does not return */
.Ltrampoline_hang:
    wfe
    b       .Ltrampoline_hang
