/**
 * @file uart_stub.c
 * @brief Stub HAL UART functions for host-side unit tests.
 * All functions are no-ops on the host.
 */

#include <stdint.h>
#include "status.h"

Status HAL_UART_Init(void)        { return STATUS_OK; }
void   HAL_UART_PutChar(char c)   { (void)c; }
char   HAL_UART_GetChar(void)     { return '\0'; }
void   HAL_UART_PutString(const char *s) { (void)s; }
void   HAL_UART_PutHex(uint64_t v)  { (void)v; }
void   HAL_UART_PutDec(uint64_t v)  { (void)v; }
