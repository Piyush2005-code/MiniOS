/**
 * @file context_stub.c
 * @brief Stub context switch functions for host-side tests.
 *
 * context.S defines two symbols that thread.c calls:
 *   cpu_context_switch(cpu_context_t *old, cpu_context_t *new)
 *   _thread_entry_trampoline (function pointer / jump target)
 *
 * For host-side scheduler unit tests, tasks never actually run,
 * so these stubs just record the most recent saved/restored contexts.
 */

#include <stdint.h>
#include "kernel/thread.h"  /* cpu_context_t */

/* Last contexts observed by cpu_context_switch (for test inspection) */
cpu_context_t *ctx_stub_last_old = (void*)0;
cpu_context_t *ctx_stub_last_new = (void*)0;

void cpu_context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx)
{
    ctx_stub_last_old = old_ctx;
    ctx_stub_last_new = new_ctx;
    /* On the host we cannot actually switch; this is a no-op. */
}

/**
 * _thread_entry_trampoline is used as the initial LR for new threads.
 * On the host it is never called; we just need the symbol to exist.
 */
void _thread_entry_trampoline(void)
{
    /* Never reached on host */
}
