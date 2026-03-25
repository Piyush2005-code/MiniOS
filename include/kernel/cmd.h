/**
 * @file cmd.h
 * @brief MiniOS Command Registry API
 *
 * Provides a static command table that maps command name strings to
 * handler functions. The shell daemon calls CMD_Dispatch() with the
 * raw input line; this module tokenises it and calls the right handler.
 *
 * Usage (adding a new command):
 *   1. Write a handler: void my_cmd(int argc, char *argv[])
 *   2. Call CMD_Register("myname", "short help text", my_cmd)
 *      before SCHED_Start() (e.g. in a CMD_RegisterCustom() fn).
 *
 * @note Commands run inside the shell_daemon thread — do NOT block
 *       for more than a few ms. Use THREAD_Yield() if you need to
 *       do longer work, or spawn a separate thread.
 */

#ifndef MINIOS_KERNEL_CMD_H
#define MINIOS_KERNEL_CMD_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Limits                                                            */
/* ------------------------------------------------------------------ */

/** Maximum number of simultaneously registered commands */
#define CMD_MAX_COMMANDS    32

/** Maximum characters in a single input line (including null) */
#define CMD_LINE_MAX        80

/** Maximum number of whitespace-separated tokens on one line */
#define CMD_MAX_ARGS        8

/** Maximum characters in a command name (including null) */
#define CMD_NAME_MAX        16

/** Maximum characters in a command help string (including null) */
#define CMD_HELP_MAX        72

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Command handler function signature.
 *
 * @param[in] argc  Number of tokens (argv[0] = command name itself)
 * @param[in] argv  Array of null-terminated token strings
 */
typedef void (*cmd_handler_t)(int argc, char *argv[]);

/**
 * @brief One entry in the command table.
 */
typedef struct {
    char          name[CMD_NAME_MAX];   /**< Command name, e.g. "uptime" */
    char          help[CMD_HELP_MAX];   /**< One-line description for help */
    cmd_handler_t handler;              /**< Handler function pointer       */
} cmd_entry_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Register a command into the global command table.
 *
 * Must be called before SCHED_Start() (from main.c or daemon init).
 *
 * @param[in] name    Command name string (max CMD_NAME_MAX-1 chars)
 * @param[in] help    Short description shown by 'help' (max CMD_HELP_MAX-1)
 * @param[in] handler Function to call when this command is typed
 *
 * @return STATUS_OK              on success
 *         STATUS_ERROR_POOL_EXHAUSTED  if CMD_MAX_COMMANDS already registered
 *         STATUS_ERROR_INVALID_ARGUMENT if name or handler is NULL
 */
Status CMD_Register(const char *name, const char *help,
                    cmd_handler_t handler);

/**
 * @brief Tokenise and dispatch a raw input line.
 *
 * Splits @p line on whitespace, looks up argv[0] in the command table,
 * and calls the matching handler. Prints an error if not found.
 *
 * Safe to call from the shell daemon thread.
 *
 * @param[in] line  Null-terminated raw input line (will be modified
 *                  in-place during tokenisation — pass a writable copy)
 */
void CMD_Dispatch(char *line);

/**
 * @brief Register all built-in kernel commands.
 *
 * Registers: help, uptime, memstat, ps, clear.
 * Called automatically by DAEMON_RegisterAll().
 */
void CMD_RegisterBuiltins(void);

/**
 * @brief Return pointer to the command table (for iteration).
 *
 * Useful if you want to generate dynamic listings.
 *
 * @param[out] count  Set to the number of registered commands
 * @return Pointer to the first cmd_entry_t in the static table
 */
const cmd_entry_t *CMD_GetTable(uint32_t *count);

#endif /* MINIOS_KERNEL_CMD_H */
