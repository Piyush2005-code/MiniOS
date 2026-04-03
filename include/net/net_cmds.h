/**
 * @file net_cmds.h
 * @brief Network monitoring shell commands for MiniOS
 *
 * Registers the following commands into the CMD table:
 *   netstat  — snapshot of SFU counters
 *   netlog   — enable/disable per-packet UART logging
 *   netlive  — real-time rolling 500 ms stats display
 */

#ifndef MINIOS_NET_NET_CMDS_H
#define MINIOS_NET_NET_CMDS_H

#include "status.h"

/**
 * @brief Register all network monitoring commands.
 *
 * Call after CMD_RegisterBuiltins() and before SCHED_Start().
 *
 * @return STATUS_OK on success, or STATUS_ERROR_POOL_EXHAUSTED if the
 *         command table is full.
 */
Status NET_RegisterCommands(void);

#endif /* MINIOS_NET_NET_CMDS_H */
