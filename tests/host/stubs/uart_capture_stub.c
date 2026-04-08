#include <stddef.h>
#include <stdint.h>

#include "status.h"

#define UART_CAPTURE_MAX 8192

static char g_uart_buf[UART_CAPTURE_MAX];
static size_t g_uart_len = 0;

static void uart_capture_append_char(char c) {
    if (g_uart_len + 1 < UART_CAPTURE_MAX) {
        g_uart_buf[g_uart_len++] = c;
        g_uart_buf[g_uart_len] = '\0';
    }
}

void uart_capture_reset(void) {
    g_uart_len = 0;
    g_uart_buf[0] = '\0';
}

const char *uart_capture_get(void) {
    return g_uart_buf;
}

Status HAL_UART_Init(void) { return STATUS_OK; }

void HAL_UART_PutChar(char c) { uart_capture_append_char(c); }

char HAL_UART_GetChar(void) { return '\0'; }

void HAL_UART_PutString(const char *s) {
    if (s == NULL) {
        return;
    }
    while (*s) {
        uart_capture_append_char(*s++);
    }
}

void HAL_UART_PutHex(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    HAL_UART_PutString("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (uint8_t)((v >> i) & 0xFU);
        uart_capture_append_char(hex[nibble]);
    }
}

void HAL_UART_PutDec(uint64_t v) {
    char tmp[24];
    int idx = 0;

    if (v == 0) {
        uart_capture_append_char('0');
        return;
    }

    while (v > 0 && idx < (int)sizeof(tmp)) {
        tmp[idx++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (idx > 0) {
        uart_capture_append_char(tmp[--idx]);
    }
}
