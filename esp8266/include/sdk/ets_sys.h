/**
 * @file ets_sys.h
 * @brief ESP8266 NonOS SDK ETS (Xtensa interrupt system) stubs.
 *
 * The ETS is the ROM-level interrupt/task dispatcher on the LX106.
 * We only need the few macros/prototypes our code actually touches.
 */

#ifndef _ETS_SYS_H_
#define _ETS_SYS_H_

#include "c_types.h"

/* ------------------------------------------------------------------ */
/*  Section placement attributes                                       */
/* ------------------------------------------------------------------ */

/** Code placed in SPI Flash (slow, cacheable via iCache). */
#ifndef ICACHE_FLASH_ATTR
#  define ICACHE_FLASH_ATTR  __attribute__((section(".irom0.text")))
#endif

/** Code placed in internal iRAM (fast, always accessible). */
#ifndef IRAM_ATTR
#  define IRAM_ATTR          __attribute__((section(".iram0.text")))
#endif

/* ------------------------------------------------------------------ */
/*  Interrupt enable/disable                                           */
/* ------------------------------------------------------------------ */

#define ETS_INTR_LOCK()     do { } while (0)
#define ETS_INTR_UNLOCK()   do { } while (0)

/* ------------------------------------------------------------------ */
/*  ETS task priorities (used by system_os_task)                      */
/* ------------------------------------------------------------------ */

#define ESP_TASK_PRIO_0   0U    /**< Lowest user OS-task priority  */
#define ESP_TASK_PRIO_1   1U    /**< Middle user OS-task priority  */
#define ESP_TASK_PRIO_2   2U    /**< Highest user OS-task priority */

/* ------------------------------------------------------------------ */
/*  ROM printf (routed to UART0 by the SDK)                           */
/* ------------------------------------------------------------------ */

int  ets_printf(const char *fmt, ...);
void ets_uart_printf(const char *fmt, ...);

#endif /* _ETS_SYS_H_ */
