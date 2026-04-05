/**
 * @file shell.c
 * @brief MiniOS interactive UART shell daemon
 *
 * Runs as a cooperative low-priority thread. Displays a prompt, reads
 * characters from the PL011 UART one at a time (HAL_UART_GetChar()),
 * echoes them back, handles backspace, and dispatches complete lines
 * to CMD_Dispatch().
 *
 * Terminal interaction example:
 *
 *   MiniOS v0.2 | type 'help' for commands
 *   miniOS> uptime
 *   Uptime : 4230 ms  (00:00:04)
 *   miniOS> _
 *
 * Design notes:
 *  - HAL_UART_GetChar() is BLOCKING. The shell thread holds the CPU
 *    while waiting for input. This is intentional: the shell runs at
 *    THREAD_PRIORITY_LOW so all other work (inference, daemons) runs
 *    first. When the user is not typing, the shell sleeps inside
 *    GetChar() waiting for a byte, and other threads run via the
 *    timer interrupt waking them.
 *  - Cooperative: after each dispatched command the shell calls
 *    THREAD_Yield() so the runtime daemon has a chance to print its
 *    2-second stats before the next prompt appears.
 *
 * @note Per SRS FR-021 (UART interface), FR-023 (status reporting).
 */

#include "kernel/shell.h"
#include "kernel/cmd.h"
#include "kernel/thread.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Shell prompt                                                      */
/* ------------------------------------------------------------------ */
#define SHELL_PROMPT    "\nminiOS> "

/* ------------------------------------------------------------------ */
/*  Shell daemon thread                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Main loop of the interactive shell.
 *
 * Reads a line from UART, echoes characters, handles backspace,
 * and passes complete lines to CMD_Dispatch().
 */
static void shell_daemon(void *arg)
{
    (void)arg;

    /* Working buffer — writable copy for CMD_Dispatch tokenisation */
    char line[CMD_LINE_MAX];
    uint32_t pos = 0;

    HAL_UART_PutString("\n");
    HAL_UART_PutString("  ========================================\n");
    HAL_UART_PutString("   MiniOS v0.2  |  type 'help' for cmds  \n");
    HAL_UART_PutString("  ========================================\n");

    while (1) {
        /* Print prompt */
        HAL_UART_PutString(SHELL_PROMPT);

        pos = 0;

        /* Read one line */
        while (1) {
            char c = HAL_UART_GetChar();

            if (c == '\r' || c == '\n') {
                /* Enter — terminate and dispatch */
                line[pos] = '\0';
                HAL_UART_PutString("\n");
                break;

            } else if ((c == '\b' || c == 0x7F) && pos > 0) {
                /* Backspace — erase last char */
                pos--;
                HAL_UART_PutString("\b \b");  /* erase on VT100/picocom */

            } else if (c >= 0x20 && c < 0x7F && pos < CMD_LINE_MAX - 1) {
                /* Printable character — echo and store */
                line[pos++] = c;
                HAL_UART_PutChar(c);
            }
            /* Ignore: control chars, overflow */
        }

        /* Dispatch the line (may be empty — CMD_Dispatch handles it) */
        CMD_Dispatch(line);

        /* Cooperative yield: let daemons/inference run before next prompt */
        THREAD_Yield();
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status SHELL_RegisterDaemon(void)
{
    thread_t *t = NULL;
    Status s = THREAD_Create(&t, "shell", shell_daemon, NULL,
                             THREAD_PRIORITY_LOW, 0 /* default 8 KB stack */);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[SHELL] ERROR: failed to create shell daemon\n");
    } else {
        HAL_UART_PutString("[SHELL] Shell daemon registered\n");
    }
    return s;
}
