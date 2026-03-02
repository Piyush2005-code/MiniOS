/**
 * @file kapi.h
 * @brief Top-level Kernel API for MiniOS
 *
 * Master include header that aggregates all kernel subsystem
 * APIs: memory management, threading/scheduling, and HAL services.
 *
 * Usage:
 *   #include "kernel/kapi.h"    // includes everything
 *
 * Provides KERNEL_Init() to initialize all subsystems in the
 * correct dependency order after the HAL is ready.
 */

#ifndef MINIOS_KERNEL_KAPI_H
#define MINIOS_KERNEL_KAPI_H

/* ---- Subsystem headers ---- */
#include "types.h"
#include "status.h"
#include "hal/arch.h"
#include "hal/gic.h"
#include "hal/timer.h"
#include "kernel/kmem.h"
#include "kernel/thread.h"

/* ------------------------------------------------------------------ */
/*  Kernel Lifecycle API                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize all kernel subsystems
 *
 * Calls subsystem initializers in correct dependency order:
 *   1. KMEM_Init()     — Memory manager (uses linker heap region)
 *   2. HAL_GIC_Init()  — Interrupt controller
 *   3. HAL_Timer_Init() — System timer
 *   4. SCHED_Init()    — Scheduler (creates idle thread)
 *
 * Must be called AFTER HAL initialization (UART, MMU, vectors).
 *
 * @return STATUS_OK if all subsystems initialized successfully
 */
Status KERNEL_Init(void);

/**
 * @brief Start the kernel
 *
 * Enables timer interrupts, unmasks IRQs, and enters the
 * scheduler's idle loop. This function does NOT return.
 *
 * Must be called AFTER KERNEL_Init() and thread creation.
 */
void KERNEL_Start(void);

#endif /* MINIOS_KERNEL_KAPI_H */
