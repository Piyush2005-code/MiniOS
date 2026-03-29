/**
 * @file cmd.c
 * @brief MiniOS Command Registry and Built-in Commands
 *
 * Implements the static command table and the five built-in commands:
 *   help    — list all registered commands
 *   uptime  — show time since boot (ms and HH:MM:SS)
 *   memstat — show kernel heap usage
 *   ps      — show thread count
 *   clear   — scroll the terminal
 *
 * The command table is a flat array of cmd_entry_t (max CMD_MAX_COMMANDS).
 * Lookup is O(n) linear scan — perfectly adequate for ≤32 commands on
 * a system where commands are typed at human speed.
 *
 * @note Per SRS FR-023 (system status reporting), FR-020 (memory stats),
 *       FR-005 (timer services).
 */

#include "kernel/cmd.h"
#include "hal/timer.h"
#include "hal/uart.h"
#include "kernel/daemon.h"
#include "kernel/kmem.h"
#include "kernel/thread.h"
#include "fs/minifs.h"          
#include "net/net_model.h"      
/* ------------------------------------------------------------------ */
/*  Command table                                                     */
/* ------------------------------------------------------------------ */

static cmd_entry_t g_cmd_table[CMD_MAX_COMMANDS];
static uint32_t g_cmd_count = 0;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/** Case-insensitive strcmp (only ASCII letters) */
static int cmd_streq(const char *a, const char *b) {
  while (*a && *b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
    if (ca != cb)
      return 0;
    a++;
    b++;
  }
  return (*a == '\0' && *b == '\0');
}

/** Copy at most max-1 chars then null-terminate */
static void cmd_strncpy(char *dst, const char *src, uint32_t max) {
  uint32_t i = 0;
  while (i < max - 1 && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/** Check whether c is whitespace */
static int cmd_isspace(char c) {
  return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

/* ------------------------------------------------------------------ */
/*  Public API — registry                                             */
/* ------------------------------------------------------------------ */

Status CMD_Register(const char *name, const char *help, cmd_handler_t handler) {
  if (name == NULL || handler == NULL) {
    return STATUS_ERROR_INVALID_ARGUMENT;
  }
  if (g_cmd_count >= CMD_MAX_COMMANDS) {
    return STATUS_ERROR_POOL_EXHAUSTED;
  }

  cmd_entry_t *e = &g_cmd_table[g_cmd_count];
  cmd_strncpy(e->name, name, CMD_NAME_MAX);
  cmd_strncpy(e->help, (help != NULL) ? help : "", CMD_HELP_MAX);
  e->handler = handler;

  g_cmd_count++;
  return STATUS_OK;
}

void CMD_Dispatch(char *line) {
  if (line == NULL)
    return;

  /* Skip leading whitespace */
  while (*line && cmd_isspace(*line))
    line++;
  if (*line == '\0')
    return; /* blank line — do nothing */

  /* Tokenise into argv[], splitting on whitespace */
  char *argv[CMD_MAX_ARGS];
  int argc = 0;

  char *p = line;
  while (*p != '\0' && argc < CMD_MAX_ARGS) {
    /* Skip whitespace */
    while (*p && cmd_isspace(*p)) {
      *p = '\0';
      p++;
    }
    if (*p == '\0')
      break;

    argv[argc++] = p;

    /* Advance to end of token */
    while (*p && !cmd_isspace(*p))
      p++;
  }

  if (argc == 0)
    return;

  /* Look up argv[0] in the command table */
  for (uint32_t i = 0; i < g_cmd_count; i++) {
    if (cmd_streq(g_cmd_table[i].name, argv[0])) {
      g_cmd_table[i].handler(argc, argv);
      return;
    }
  }

  /* Not found */
  HAL_UART_PutString("Unknown command: '");
  HAL_UART_PutString(argv[0]);
  HAL_UART_PutString("'  (type 'help' for a list)\n");
}

const cmd_entry_t *CMD_GetTable(uint32_t *count) {
  if (count)
    *count = g_cmd_count;
  return g_cmd_table;
}

/* ------------------------------------------------------------------ */
/*  Helper: print HH:MM:SS                                           */
/* ------------------------------------------------------------------ */
static void print_wall_time(uint64_t secs) {
  uint32_t h = (uint32_t)(secs / 3600U);
  uint32_t m = (uint32_t)((secs % 3600U) / 60U);
  uint32_t s = (uint32_t)(secs % 60U);

  if (h < 10) {
    HAL_UART_PutString("0");
  }
  HAL_UART_PutDec(h);
  HAL_UART_PutString(":");
  if (m < 10) {
    HAL_UART_PutString("0");
  }
  HAL_UART_PutDec(m);
  HAL_UART_PutString(":");
  if (s < 10) {
    HAL_UART_PutString("0");
  }
  HAL_UART_PutDec(s);
}

/* ================================================================== */
/*  Built-in command handlers                                         */
/* ================================================================== */

/* ---- help --------------------------------------------------------- */
/**
 * Usage: help
 * Lists every registered command with its one-line description.
 */
static void cmd_help(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  HAL_UART_PutString("\n  MiniOS built-in commands\n");
  HAL_UART_PutString("  ========================\n");

  for (uint32_t i = 0; i < g_cmd_count; i++) {
    HAL_UART_PutString("  ");
    HAL_UART_PutString(g_cmd_table[i].name);

    /* Pad name to 10 chars for alignment */
    uint32_t len = 0;
    const char *n = g_cmd_table[i].name;
    while (n[len])
      len++;
    for (uint32_t sp = len; sp < 10; sp++)
      HAL_UART_PutString(" ");

    HAL_UART_PutString("  ");
    HAL_UART_PutString(g_cmd_table[i].help);
    HAL_UART_PutString("\n");
  }
  HAL_UART_PutString("\n");
}

/* ---- uptime ------------------------------------------------------- */
/**
 * Usage: uptime
 * Prints milliseconds since boot and human-readable HH:MM:SS.
 */
static void cmd_uptime(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  uint64_t ms = SCHED_GetUptime();
  uint64_t secs = DAEMON_GetWallSeconds();

  HAL_UART_PutString("Uptime : ");
  HAL_UART_PutDec((uint32_t)ms);
  HAL_UART_PutString(" ms  (");
  print_wall_time(secs);
  HAL_UART_PutString(")\n");
}

/* ---- memstat ------------------------------------------------------ */
/**
 * Usage: memstat
 * Prints kernel heap used, total, free, and usage percentage.
 */
static void cmd_memstat(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  kmem_stats_t st;
  KMEM_GetStats(&st);

  uint32_t pct = (st.heap_total > 0)
                     ? (uint32_t)((st.heap_used * 100ULL) / st.heap_total)
                     : 0;

  HAL_UART_PutString("Heap   : ");
  HAL_UART_PutDec((uint32_t)(st.heap_used / 1024U));
  HAL_UART_PutString(" KB used\n");
  HAL_UART_PutString("         ");
  HAL_UART_PutDec((uint32_t)(st.heap_total / 1024U));
  HAL_UART_PutString(" KB total\n");
  HAL_UART_PutString("         ");
  HAL_UART_PutDec((uint32_t)((st.heap_total - st.heap_used) / 1024U));
  HAL_UART_PutString(" KB free (");
  HAL_UART_PutDec(100 - pct);
  HAL_UART_PutString("%)\n");
}

/* ---- ps ----------------------------------------------------------- */
/**
 * Usage: ps
 * Prints the total number of threads created (including idle + daemons).
 */
static void cmd_ps(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  uint32_t n = SCHED_GetThreadCount();
  HAL_UART_PutString("Threads: ");
  HAL_UART_PutDec(n);
  HAL_UART_PutString(" active\n");
  HAL_UART_PutString("  (idle + inference + daemons)\n");
}

/* ---- clear -------------------------------------------------------- */
/**
 * Usage: clear
 * Sends ANSI escape to clear the terminal; falls back to blank lines
 * if the terminal doesn't support ANSI.
 */
static void cmd_clear(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  /* ANSI: clear screen and move cursor home */
  HAL_UART_PutString("\033[2J\033[H");
}

static void cmd_echo(int argc, char *argv[]) {
  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
      HAL_UART_PutString(argv[i]);
      HAL_UART_PutString(" ");
    }
  } else {
    HAL_UART_PutString("usage: echo <text to be echoed>\n");
  }
  HAL_UART_PutString("\n");
}

/* ---- ls ----------------------------------------------------------- */
/**
 * Usage: ls           → lists / (root)
 * Usage: ls exec      → lists /exec
 * Usage: ls tmp       → lists /tmp
 */
static void cmd_ls(int argc, char *argv[]) {
    if (argc < 2) {
        /* No argument: show root */
        MFS_ListRoot();
    } else {
        /* Argument: show that directory */
        MFS_ListDir(argv[1]);
    }
}

/* ---- recv --------------------------------------------------------- */
/**
 * Usage: recv
 * Blocks until a model file is received over UART from the host.
 * Stores it in /exec/model.bin
 */
static void cmd_recv(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    NET_ReceiveModel();
}

/* ---- run ---------------------------------------------------------- */
/**
 * Usage: run exec/model.bin
 * Loads the file from MiniFS and runs ONNX inference on it.
 */
static void cmd_run(int argc, char *argv[]) {
    if (argc < 2) {
        HAL_UART_PutString("Usage: run <path>  (e.g. run exec/model.bin)\n");
        return;
    }
    NET_RunModel(argv[1]);
}
/* ------------------------------------------------------------------ */
/*  Register built-ins                                                */
/* ------------------------------------------------------------------ */
void CMD_RegisterBuiltins(void) {
  CMD_Register("help",    "List all available commands",    cmd_help);
  CMD_Register("uptime",  "Show time since boot",           cmd_uptime);
  CMD_Register("memstat", "Show kernel heap usage",         cmd_memstat);
  CMD_Register("ps",      "Show active thread count",       cmd_ps);
  CMD_Register("clear",   "Clear the terminal screen",      cmd_clear);
  CMD_Register("echo",    "Echo arguments to terminal",     cmd_echo);
  CMD_Register("ls",      "ls [dir]  List filesystem",      cmd_ls);
  CMD_Register("recv",    "recv  Receive model over UART",  cmd_recv);
  CMD_Register("run",     "run <path>  Run ONNX model",     cmd_run);
}
