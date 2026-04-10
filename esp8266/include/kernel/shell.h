/**
 * @file shell.h
 * @brief Shell API for MiniOS-ESP8266
 */

#ifndef MINIOS_ESP8266_KERNEL_SHELL_H
#define MINIOS_ESP8266_KERNEL_SHELL_H

#include "types.h"
#include "onnx/onnx_runtime.h"

/**
 * @brief Initialize and start the UART shell.
 *        Registers a 20ms polling os_timer for command input.
 */
void SHELL_Init(void);

/* Exposed for cmd_infer — shares inference context with main */
extern ONNX_InferenceContext g_ctx;

#endif /* MINIOS_ESP8266_KERNEL_SHELL_H */
