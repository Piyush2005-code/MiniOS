# MiniOS Command Developer Guide

> **Branch:** `Kernel/commands`  
> **Location:** `docs/COMMANDS.md`

This guide explains how to add new shell commands to MiniOS. After reading it you will be able to write, register, and test a new command in about 10 minutes.

---

## 1. How the Command System Works

```
User types at serial terminal
        │
        ▼  (HAL_UART_GetChar, blocking)
   shell_daemon (THREAD_PRIORITY_LOW)
        │  reads chars, builds a line
        ▼  on Enter:
   CMD_Dispatch(line)
        │  tokenises → argv[]
        │  walks g_cmd_table[]
        ▼  on match:
   your_handler(argc, argv)
        │
        ▼  output via HAL_UART_PutString()
   miniOS> _
```

**Command table:** a static array of `cmd_entry_t` in `src/kernel/cmd.c`.  
**Limit:** `CMD_MAX_COMMANDS` (32) commands, `CMD_MAX_ARGS` (8) arguments.  
**Line limit:** `CMD_LINE_MAX` (80) characters per input line.

---

## 2. Quick-Start: Adding "Hello World"

### Step 1 — Write your handler

Open (or create) the file that owns your command, for example `src/kernel/cmd_custom.c`:

```c
#include "kernel/cmd.h"
#include "hal/uart.h"

void cmd_hello(int argc, char *argv[])
{
    if (argc > 1) {
        HAL_UART_PutString("Hello, ");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("!\n");
    } else {
        HAL_UART_PutString("Hello, world!\n");
    }
}
```

Handler signature is always:
```c
void my_handler(int argc, char *argv[]);
```
`argv[0]` is always the command name itself (like C's `main`).

### Step 2 — Register it

In `src/kernel/cmd.c`, inside `CMD_RegisterBuiltins()` (or in your own `CMD_RegisterCustom()` function):

```c
CMD_Register("hello", "Say hello (usage: hello [name])", cmd_hello);
```

Or call `CMD_Register()` from anywhere before `SCHED_Start()`.

### Step 3 — Add your file to the Makefile

In `Makefile`, in the `C_SRCS` block:

```makefile
$(SRC_DIR)/kernel/cmd_custom.c \
```

### Step 4 — Build and run

```bash
make clean && make
make run
# At the prompt:
# miniOS> hello
# Hello, world!
# miniOS> hello Piyush
# Hello, Piyush!
```

---

## 3. Built-in Commands Reference

| Command | Usage | Output |
|---|---|---|
| `help` | `help` | Lists all registered commands |
| `uptime` | `uptime` | Milliseconds since boot + HH:MM:SS |
| `memstat` | `memstat` | Heap used / total / free |
| `ps` | `ps` | Number of active threads |
| `clear` | `clear` | ANSI clear screen |

---

## 4. Handler Design Rules

| Rule | Rationale |
|---|---|
| **Never call `KMEM_Alloc()` inside a handler** | Handlers run in the shell daemon; heap allocations for intermediate data should be done at init time |
| **Keep output to ≤ 10 lines** | Long output blocks the shell daemon from yielding |
| **Call `THREAD_Yield()` for multi-step work** | Allows inference and other daemons to run mid-command |
| **Do not `while(1)` in a handler** | The shell will never return; spawn a daemon thread instead |
| **Validate `argc` before accessing `argv[n]`** | Safety against user typos |
| **Return void** | Communicate results only via UART output |

---

## 5. Argument Parsing Pattern

```c
void cmd_example(int argc, char *argv[])
{
    /* argv[0] = "example", argv[1..] = user args */
    if (argc < 2) {
        HAL_UART_PutString("Usage: example <value>\n");
        return;
    }

    const char *value = argv[1];
    HAL_UART_PutString("Got: ");
    HAL_UART_PutString(value);
    HAL_UART_PutString("\n");
}
```

There is no `atoi()` in the minimal libc, so if you need a number argument, parse it manually:

```c
/* Simple unsigned decimal parser (no stdlib needed) */
static uint32_t parse_uint(const char *s)
{
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return n;
}
```

---

## 6. Subcommand Pattern (for complex commands)

Group related actions under one top-level name:

```c
void cmd_onnx(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("Usage: onnx <load|run|info>\n");
        return;
    }

    if (argv[1][0] == 'l') {
        /* onnx load */
    } else if (argv[1][0] == 'r') {
        /* onnx run  */
    } else if (argv[1][0] == 'i') {
        /* onnx info */
    } else {
        HAL_UART_PutString("Unknown subcommand\n");
    }
}
// Register: CMD_Register("onnx", "Manage ONNX runtime (load/run/info)", cmd_onnx);
```

---

## 7. Daemon-Backed Commands (data from a background thread)

For commands that show live data collected by a daemon:

```c
/* In daemon.c — daemon writes this every 500 ms */
static volatile uint32_t g_latest_temp_c = 0;

/* Public getter (cmd.c reads this) */
uint32_t DAEMON_GetTemperature(void) { return g_latest_temp_c; }
```

```c
/* In cmd.c */
#include "kernel/daemon.h"

void cmd_temp(int argc, char *argv[])
{
    HAL_UART_PutString("Temp : ");
    HAL_UART_PutDec(DAEMON_GetTemperature());
    HAL_UART_PutString(" C\n");
}
```

The command just reads a `volatile` variable the daemon keeps up to date — zero blocking.

---

## 8. File Organisation

```
src/
  kernel/
    cmd.c          ← command table + built-ins (edit to add more built-ins)
    cmd_custom.c   ← your own commands (create this file)
    shell.c        ← UART reader + prompt (usually no need to edit)
include/
  kernel/
    cmd.h          ← CMD_Register(), CMD_Dispatch(), cmd_entry_t
    shell.h        ← SHELL_RegisterDaemon()
docs/
  COMMANDS.md      ← this file
```

---

## 9. Daemon Priority Reference

| # | Daemon | Period | Why first |
|---|---|---|---|
| 1 | `clock_daemon` | 1 000 ms | Wall time used by all others |
| 2 | `memwatch_daemon` | 500 ms | Catch OOM before it crashes the kernel |
| 3 | `shell_daemon` | blocking | Developer interface (this sprint) |
| 4 | `runtime_daemon` | 2 000 ms | Stats — nice to have, non-critical |
| 5 | `perf_daemon` | 5 000 ms | PMU sampling (future, post-ONNX integration) |

---

## 10. Testing Your Command on QEMU

```bash
# Build
make clean && make

# Start QEMU (Ctrl+A then X to exit)
make run

# Expected session:
#   MiniOS v0.2  |  type 'help' for commands
#   miniOS> help
#     help        List all available commands
#     uptime      Show time since boot
#     memstat     Show kernel heap usage
#     ps          Show active thread count
#     clear       Clear the terminal screen
#     hello       Say hello (usage: hello [name])
#
#   miniOS> hello Piyush
#   Hello, Piyush!
#
#   miniOS> uptime
#   Uptime : 3400 ms  (00:00:03)
```

---

*MiniOS — ARM64 ML Inference Unikernel — Sprint 6 (Mar 26 – Apr 10)*
