/**
 * @file net_cmds.c
 * @brief Network monitoring shell commands for MiniOS
 *
 * Commands:
 *   netstat            — snapshot of SFU traffic counters
 *   netlog [on|off]    — toggle per-packet UART logging
 *   netlive            — real-time 500 ms rolling delta display;
 *                        exit by pressing Enter or 'q'
 */

#include "net/net_cmds.h"
#include "net/sfu.h"
#include "kernel/cmd.h"
#include "kernel/thread.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Module state                                                      */
/* ------------------------------------------------------------------ */

static uint8_t g_netlog_enabled = 0;

/* Called by sfu.c if we hook into a logging callback — we expose a
 * getter so infer_server / future code can check this flag without a
 * circular include. */
uint8_t NET_IsLogEnabled(void) { return g_netlog_enabled; }

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static void nc_puts(const char *s) { HAL_UART_PutString(s); }
static void nc_putu(uint32_t v)    { HAL_UART_PutDec(v); }

/** Print a fixed-width right-aligned decimal */
static void nc_putu_w(uint32_t v, uint32_t width)
{
    char buf[12];
    int n = 0;
    uint32_t tmp = v;
    if (tmp == 0) { buf[n++] = '0'; }
    while (tmp > 0) { buf[n++] = '0' + (char)(tmp % 10); tmp /= 10; }
    /* Pad */
    for (uint32_t p = (uint32_t)n; p < width; p++) HAL_UART_PutChar(' ');
    /* Reverse print */
    for (int i = n - 1; i >= 0; i--) HAL_UART_PutChar(buf[i]);
}

/* ------------------------------------------------------------------ */
/*  netstat                                                           */
/* ------------------------------------------------------------------ */

static void cmd_netstat(int argc, char *argv[])
{
    (void)argc; (void)argv;

    sfu_stats_t s;
    SFU_GetStats(&s);

    nc_puts("\n  SFU Network Statistics\n");
    nc_puts("  ─────────────────────────────────────\n");

    nc_puts("  RX packets  : "); nc_putu(s.rx_packets);   nc_puts("\n");
    nc_puts("  RX bytes    : "); nc_putu(s.rx_bytes);     nc_puts("\n");
    nc_puts("  TX packets  : "); nc_putu(s.tx_packets);   nc_puts("\n");
    nc_puts("  TX bytes    : "); nc_putu(s.tx_bytes);     nc_puts("\n");
    nc_puts("  ─────────────────────────\n");
    nc_puts("  PINGs rx    : "); nc_putu(s.ping_count);   nc_puts("\n");
    nc_puts("  PONGs tx    : "); nc_putu(s.pong_count);   nc_puts("\n");
    nc_puts("  Infer reqs  : "); nc_putu(s.infer_requests);  nc_puts("\n");
    nc_puts("  Infer resps : "); nc_putu(s.infer_responses); nc_puts("\n");
    nc_puts("  CMD reqs    : "); nc_putu(s.cmd_count);    nc_puts("\n");
    nc_puts("  ─────────────────────────\n");
    nc_puts("  CRC errors  : "); nc_putu(s.checksum_errors); nc_puts("\n");
    nc_puts("  Bad magic   : "); nc_putu(s.bad_magic);    nc_puts("\n");
    nc_puts("  netlog      : ");
    nc_puts(g_netlog_enabled ? "ON" : "OFF");
    nc_puts("\n");
}

/* ------------------------------------------------------------------ */
/*  netlog                                                            */
/* ------------------------------------------------------------------ */

static void cmd_netlog(int argc, char *argv[])
{
    if (argc < 2) {
        nc_puts("netlog: ");
        nc_puts(g_netlog_enabled ? "ON" : "OFF");
        nc_puts("  (usage: netlog on|off)\n");
        return;
    }

    const char *arg = argv[1];
    int is_on  = (arg[0]=='o'||arg[0]=='O') && (arg[1]=='n'||arg[1]=='N') && arg[2]=='\0';
    int is_off = (arg[0]=='o'||arg[0]=='O') && (arg[1]=='f'||arg[1]=='F') && arg[2]!='\0';

    if (is_on) {
        g_netlog_enabled = 1;
        nc_puts("netlog: enabled\n");
    } else if (is_off) {
        g_netlog_enabled = 0;
        nc_puts("netlog: disabled\n");
    } else {
        nc_puts("netlog: usage: netlog on|off\n");
    }
}

/* ------------------------------------------------------------------ */
/*  netlive                                                           */
/* ------------------------------------------------------------------ */

#define NETLIVE_PERIOD_TICKS  50u    /* 50 × 10 ms = 500 ms */

static void cmd_netlive(int argc, char *argv[])
{
    (void)argc; (void)argv;

    nc_puts("\n  [netlive] Real-time SFU stats  (press Enter to exit)\n");
    nc_puts("  ─────────────────────────────────────────────────────\n");
    nc_puts("  Time(s)  RX-pkt  TX-pkt  RX-B    TX-B   PING INFER\n");
    nc_puts("  ─────────────────────────────────────────────────────\n");

    sfu_stats_t prev;
    SFU_GetStats(&prev);
    uint64_t start_ticks = HAL_Timer_GetSystemTicks();
    uint64_t next_tick   = start_ticks + NETLIVE_PERIOD_TICKS;

    while (1) {
        /* Non-blocking keypress check — exit on Enter or 'q' */
        char ch = 0;
        if (HAL_UART_TryGetChar(&ch)) {
            if (ch == '\r' || ch == '\n' || ch == 'q' || ch == 'Q') {
                nc_puts("\n  [netlive] exited\n");
                return;
            }
        }

        uint64_t now = HAL_Timer_GetSystemTicks();
        if (now < next_tick) {
            THREAD_Yield();
            continue;
        }
        next_tick = now + NETLIVE_PERIOD_TICKS;

        sfu_stats_t cur;
        SFU_GetStats(&cur);

        uint64_t elapsed_ms = (now - start_ticks) *
                              (uint64_t)HAL_Timer_GetTickPeriodMs();
        uint32_t elapsed_s  = (uint32_t)(elapsed_ms / 1000u);

        nc_puts("  ");
        nc_putu_w(elapsed_s, 6);
        nc_puts("   ");
        nc_putu_w(cur.rx_packets - prev.rx_packets, 6);
        nc_puts("  ");
        nc_putu_w(cur.tx_packets - prev.tx_packets, 6);
        nc_puts("  ");
        nc_putu_w(cur.rx_bytes - prev.rx_bytes, 6);
        nc_puts("  ");
        nc_putu_w(cur.tx_bytes - prev.tx_bytes, 6);
        nc_puts("  ");
        nc_putu_w(cur.ping_count - prev.ping_count, 4);
        nc_puts(" ");
        nc_putu_w(cur.infer_requests - prev.infer_requests, 5);
        nc_puts("\n");

        prev = cur;
        THREAD_Yield();
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status NET_RegisterCommands(void)
{
    Status s;
    s = CMD_Register("netstat",
                     "Show SFU network statistics",
                     cmd_netstat);
    if (s != STATUS_OK) return s;

    s = CMD_Register("netlog",
                     "Enable/disable per-packet log [on|off]",
                     cmd_netlog);
    if (s != STATUS_OK) return s;

    s = CMD_Register("netlive",
                     "Real-time network stats (Enter to exit)",
                     cmd_netlive);
    if (s != STATUS_OK) return s;

    return STATUS_OK;
}
