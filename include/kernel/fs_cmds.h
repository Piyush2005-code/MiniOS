/**
 * @file fs_cmds.h
 * @brief ULFS Shell Command Registration
 *
 * Registers all file-system related commands into the MiniOS command
 * framework. Call FS_RegisterCommands() before SCHED_Start().
 *
 * Commands registered:
 *   ls      — list directory contents
 *   cd      — change working directory
 *   pwd     — print working directory
 *   mkdir   — create a directory
 *   touch   — create an empty file
 *   cat     — print file contents to UART
 *   write   — write text to a file
 *   rm      — remove a file or empty directory
 *   stat    — show inode information
 *   df      — show file system disk usage
 */

#ifndef MINIOS_KERNEL_FS_CMDS_H
#define MINIOS_KERNEL_FS_CMDS_H

#include "status.h"

/**
 * @brief Register all ULFS file-system commands.
 *
 * Must be called after ULFS_Init() and CMD_RegisterBuiltins(),
 * before SCHED_Start().
 *
 * @return STATUS_OK on success; STATUS_ERROR_POOL_EXHAUSTED if the
 *         command table is full.
 */
Status FS_RegisterCommands(void);

#endif /* MINIOS_KERNEL_FS_CMDS_H */
