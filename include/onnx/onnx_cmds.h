/**
 * @file onnx_cmds.h
 * @brief ONNX Shell Command Implementations
 */

#ifndef ONNX_CMDS_H
#define ONNX_CMDS_H

#include "status.h"

/**
 * @brief Register ONNX shell commands.
 * 
 * Registers: onnx_run, onnx_info
 */
Status ONNX_RegisterCommands(void);

#endif /* ONNX_CMDS_H */
