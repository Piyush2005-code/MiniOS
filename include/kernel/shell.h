/**
 * @file shell.h
 * @brief MiniOS interactive shell daemon
 *
 * The shell daemon runs at THREAD_PRIORITY_LOW and provides an
 * interactive UART command prompt. It reads lines of text from the
 * PL011 UART, echoes characters back to the terminal, handles
 * backspace, and dispatches complete lines to CMD_Dispatch().
 *
 * Prompt appearance:
 *   miniOS> _
 *
 * Usage:
 *   Call SHELL_RegisterDaemon() after CMD_RegisterBuiltins() and
 *   before SCHED_Start().
 */

#ifndef MINIOS_KERNEL_SHELL_H
#define MINIOS_KERNEL_SHELL_H

#include "types.h"
#include "status.h"

/**
 * @brief Create the shell daemon thread.
 *
 * The shell daemon thread (name "shell", THREAD_PRIORITY_LOW) is
 * created and added to the scheduler ready queue. It will start
 * running once SCHED_Start() is called.
 *
 * Must be called AFTER CMD_RegisterBuiltins() and AFTER SCHED_Init().
 *
 * @return STATUS_OK on success
 *         STATUS_ERROR_THREAD_LIMIT if thread table is full
 *         STATUS_ERROR_OUT_OF_MEMORY if stack allocation fails
 */
Status SHELL_RegisterDaemon(void);

#endif /* MINIOS_KERNEL_SHELL_H */
