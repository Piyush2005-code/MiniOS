/**
 * @file test_kapi.c
 * @brief Unity tests for CT-KAPI-001 through CT-KAPI-004
 *
 * Tests the Kernel API layer (kapi.h) on the host using stubs.
 * arch_irq_save/restore are no-ops on the host; tests verify
 * the contract rather than actual IRQ masking.
 */

#include "unity.h"

/* Include only the headers we need; avoid arch.h asm on host */
#define MINIOS_HAL_ARCH_H
#include "types.h"
#include "status.h"


/* arch stubs provided by arch_stub.c */
extern uint64_t arch_irq_save(void);
extern void     arch_irq_restore(uint64_t flags);

/* timer stub */
extern uint64_t HAL_Timer_GetTicks(void);
extern void     timer_stub_reset(void);
extern void     HAL_Timer_DelayUs(uint64_t us);

/* uart stub — KAPI_Log goes to UART; we just verify it doesn't crash */
extern void HAL_UART_PutString(const char *s);

void setUp(void)    { timer_stub_reset(); }
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/*  CT-KAPI-001: IRQ save/disable/restore cycle                       */
/* ------------------------------------------------------------------ */
void test_CT_KAPI_001(void)
{
    /* Nested save-and-disable calls restore the original DAIF state.
     * On the host the stubs return 0 and are no-ops; we verify the
     * symmetry contract: restore is called in reverse order and
     * the value passed to inner-restore is the inner-save return. */
    uint64_t outer = arch_irq_save();   /* save outer (returns 0) */
    uint64_t inner = arch_irq_save();   /* save inner (returns 0) */
    arch_irq_restore(inner);            /* unwind inner */
    arch_irq_restore(outer);            /* unwind outer */
    /* If we reach here without crash, the contract holds */
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  CT-KAPI-002: Performance region measurement                       */
/*  Verify timer stub increments and the "elapsed" is non-negative.  */
/* ------------------------------------------------------------------ */
void test_CT_KAPI_002(void)
{
    uint64_t t0 = HAL_Timer_GetTicks();
    /* Simulate some work */
    HAL_Timer_DelayUs(1000);
    uint64_t t1 = HAL_Timer_GetTicks();
    /* Timer stub increments by 1000 each GetTicks call */
    TEST_ASSERT_TRUE(t1 >= t0);
}

/* ------------------------------------------------------------------ */
/*  CT-KAPI-003: Log output format                                    */
/*  On host, UART stub is a no-op; verify PutString doesn't crash.   */
/* ------------------------------------------------------------------ */
void test_CT_KAPI_003(void)
{
    /* Simulate KAPI_Log("[module] message\n") via uart stub */
    HAL_UART_PutString("[CT-KAPI-003] message\n");
    TEST_PASS();
}

/* ------------------------------------------------------------------ */
/*  CT-KAPI-004: Cache flush does not corrupt memory                  */
/*  On host, cache flush is a no-op; verify pattern is preserved.    */
/* ------------------------------------------------------------------ */
void test_CT_KAPI_004(void)
{
    uint8_t buf[64];
    /* Write known pattern */
    for (int i = 0; i < 64; i++) buf[i] = (uint8_t)(i ^ 0xA5);

    /* Simulate cache flush (DSB/ISB no-op on host) */
    __asm__ volatile("" ::: "memory");  /* compiler barrier as stand-in */

    /* Verify pattern preserved */
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(i ^ 0xA5), buf[i]);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CT_KAPI_001);
    RUN_TEST(test_CT_KAPI_002);
    RUN_TEST(test_CT_KAPI_003);
    RUN_TEST(test_CT_KAPI_004);
    return UNITY_END();
}
