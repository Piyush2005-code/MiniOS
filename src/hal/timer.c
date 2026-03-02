/**
 * @file timer.c
 * @brief ARM Generic Timer implementation for MiniOS
 *
 * Uses the AArch64 physical timer (EL1):
 *   CNTPCT_EL0   — Physical counter (read-only, monotonic)
 *   CNTFRQ_EL0   — Counter frequency (set by firmware/QEMU)
 *   CNTP_TVAL_EL0 — Timer countdown value
 *   CNTP_CTL_EL0  — Timer control (enable, mask, status)
 *
 * On QEMU virt, the timer frequency is typically 62.5 MHz
 * (62500000 Hz), giving ~16 ns per tick.
 *
 * @note Per SRS FR-005
 * @complexity Time: O(1) for all operations
 */

#include "hal/timer.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Cached frequency (read once at init)                              */
/* ------------------------------------------------------------------ */
static uint64_t s_timer_freq_hz = 0;

/* ------------------------------------------------------------------ */
/*  Inline assembly for timer system registers                        */
/* ------------------------------------------------------------------ */

static inline uint64_t read_cntpct_el0(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
  return val;
}

static inline uint64_t read_cntfrq_el0(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
  return val;
}

static inline void write_cntp_tval_el0(uint64_t val) {
  __asm__ volatile("msr cntp_tval_el0, %0" ::"r"(val));
}

static inline uint64_t read_cntp_ctl_el0(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
  return val;
}

static inline void write_cntp_ctl_el0(uint64_t val) {
  __asm__ volatile("msr cntp_ctl_el0, %0" ::"r"(val));
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_Timer_Init(void) {
  /* Read and cache the timer frequency */
  s_timer_freq_hz = read_cntfrq_el0();

  if (s_timer_freq_hz == 0) {
    HAL_UART_PutString("[TMR ] ERROR: Timer frequency is 0!\n");
    return STATUS_ERROR_HARDWARE_FAULT;
  }

  /*
   * Disable the timer and mask its interrupt initially.
   * Bit 0 (ENABLE) = 0  → timer disabled
   * Bit 1 (IMASK)  = 1  → interrupt masked
   */
  write_cntp_ctl_el0(0x2);

  HAL_UART_PutString("[TMR ] Timer frequency: ");
  HAL_UART_PutDec(s_timer_freq_hz);
  HAL_UART_PutString(" Hz\n");

  return STATUS_OK;
}

uint64_t HAL_Timer_GetTicks(void) { return read_cntpct_el0(); }

uint64_t HAL_Timer_GetFreqHz(void) { return s_timer_freq_hz; }

uint64_t HAL_Timer_TicksToUs(uint64_t ticks) {
  if (s_timer_freq_hz == 0)
    return 0;
  /* ticks * 1000000 / freq — careful of overflow for large values */
  return (ticks / (s_timer_freq_hz / 1000000UL));
}

uint64_t HAL_Timer_UsToTicks(uint64_t us) {
  if (s_timer_freq_hz == 0)
    return 0;
  return us * (s_timer_freq_hz / 1000000UL);
}

void HAL_Timer_BusyWaitUs(uint64_t us) {
  uint64_t start = read_cntpct_el0();
  uint64_t target = HAL_Timer_UsToTicks(us);

  while ((read_cntpct_el0() - start) < target) {
    /* spin */
  }
}

void HAL_Timer_SetDeadline(uint64_t ticks) {
  /* Set countdown value */
  write_cntp_tval_el0(ticks);

  /* Enable timer, unmask interrupt: ENABLE=1, IMASK=0 */
  write_cntp_ctl_el0(0x1);
}

void HAL_Timer_ClearIRQ(void) {
  /*
   * The timer IRQ is level-triggered. Writing a new TVAL
   * de-asserts the interrupt line and re-arms the timer.
   * If we don't want to re-arm, we mask the interrupt.
   */
  uint64_t ctl = read_cntp_ctl_el0();
  /* Mask the interrupt (bit 1) to acknowledge */
  write_cntp_ctl_el0(ctl | 0x2);
}

void HAL_Timer_DisableIRQ(void) {
  uint64_t ctl = read_cntp_ctl_el0();
  /* Set IMASK (bit 1) */
  write_cntp_ctl_el0(ctl | 0x2);
}

void HAL_Timer_EnableIRQ(void) {
  uint64_t ctl = read_cntp_ctl_el0();
  /* Clear IMASK (bit 1), set ENABLE (bit 0) */
  write_cntp_ctl_el0((ctl & ~0x2UL) | 0x1);
}
