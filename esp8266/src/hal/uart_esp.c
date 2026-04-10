/**
 * @file uart_esp.c
 * @brief UART HAL Implementation — ESP8266 UART0 (GPIO1=TX, GPIO3=RX)
 *
 * Replaces the ARM64 PL011 driver with direct ESP8266 UART0 register
 * access. Maintains the same public API surface so all higher-level
 * code (SFU, shell, ONNX debug) compiles without changes.
 *
 * ESP8266 UART0 base: 0x60000000
 * Reference: ESP8266 Technical Reference, Section 11 (UART)
 */

#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  ESP8266 UART0 Register Definitions                                */
/* ------------------------------------------------------------------ */

#define UART0_BASE          0x60000000UL

/* Register offsets */
#define UART_FIFO_REG       (UART0_BASE + 0x00)  /* TX/RX FIFO data */
#define UART_INT_RAW_REG    (UART0_BASE + 0x04)  /* Raw interrupt status */
#define UART_INT_ST_REG     (UART0_BASE + 0x08)  /* Masked interrupt status */
#define UART_INT_ENA_REG    (UART0_BASE + 0x0C)  /* Interrupt enable */
#define UART_INT_CLR_REG    (UART0_BASE + 0x10)  /* Interrupt clear */
#define UART_CLKDIV_REG     (UART0_BASE + 0x14)  /* Baud rate divisor */
#define UART_AUTOBAUD_REG   (UART0_BASE + 0x18)  /* Auto-baud control */
#define UART_STATUS_REG     (UART0_BASE + 0x1C)  /* Status: TXFIFO_CNT, RXFIFO_CNT */
#define UART_CONF0_REG      (UART0_BASE + 0x20)  /* Config: parity, data bits, stop bits */
#define UART_CONF1_REG      (UART0_BASE + 0x24)  /* FIFO thresholds */

/* Status register fields */
#define UART_TXFIFO_CNT_S   16
#define UART_TXFIFO_CNT_M   0xFF
#define UART_RXFIFO_CNT_S   0
#define UART_RXFIFO_CNT_M   0xFF

/* TX FIFO depth on ESP8266 */
#define UART_TXFIFO_MAX     128

/* CONF0 bits */
#define UART_PARITY_EN      (1 << 1)
#define UART_PARITY         (1 << 0)
#define UART_STOP_BIT_NUM_S 4           /* bits 4:5 — 01=1, 11=2 stop bits */
#define UART_BIT_NUM_S      2           /* bits 2:3 — 11=8 data bits */
#define UART_LOOPBACK       (1 << 14)

/* Clock speed for baud rate calculation */
#define APB_CLK_FREQ        80000000UL  /* 80 MHz APB clock */

static inline uint32_t uart_read_reg(uint32_t addr)
{
    return REG32(addr);
}

static inline void uart_write_reg(uint32_t addr, uint32_t val)
{
    REG32(addr) = val;
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_Init                                                     */
/* ------------------------------------------------------------------ */

Status HAL_UART_Init(void)
{
    /* Calculate baud rate divisor for 115200 baud */
    uint32_t clkdiv = APB_CLK_FREQ / 115200;
    uart_write_reg(UART_CLKDIV_REG, clkdiv & 0xFFFFF);

    /* Configure: 8 data bits, 1 stop bit, no parity */
    uint32_t conf0 = (3 << UART_BIT_NUM_S)   /* 8 data bits (11b) */
                   | (1 << UART_STOP_BIT_NUM_S); /* 1 stop bit (01b) */
    uart_write_reg(UART_CONF0_REG, conf0);

    /* Set FIFO thresholds: TX empty at 0, RX available at 1 */
    uart_write_reg(UART_CONF1_REG, (0 << 8) | (1 << 0));

    /* Clear all pending interrupts */
    uart_write_reg(UART_INT_CLR_REG, 0xFFFF);

    /* Disable all UART interrupts (polling mode) */
    uart_write_reg(UART_INT_ENA_REG, 0);

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_PutChar                                                  */
/* ------------------------------------------------------------------ */

void HAL_UART_PutChar(char c)
{
    /* Wait until TX FIFO has space (count < 128) */
    while (((uart_read_reg(UART_STATUS_REG) >> UART_TXFIFO_CNT_S)
             & UART_TXFIFO_CNT_M) >= UART_TXFIFO_MAX) {
        /* spin */
    }
    uart_write_reg(UART_FIFO_REG, (uint32_t)(uint8_t)c);
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_GetChar                                                  */
/* ------------------------------------------------------------------ */

char HAL_UART_GetChar(void)
{
    /* Wait until RX FIFO has at least one byte */
    while (((uart_read_reg(UART_STATUS_REG) >> UART_RXFIFO_CNT_S)
             & UART_RXFIFO_CNT_M) == 0) {
        /* spin */
    }
    return (char)(uart_read_reg(UART_FIFO_REG) & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_HasChar                                                  */
/* ------------------------------------------------------------------ */

int HAL_UART_HasChar(void)
{
    return ((uart_read_reg(UART_STATUS_REG) >> UART_RXFIFO_CNT_S)
             & UART_RXFIFO_CNT_M) > 0;
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_PutString                                                */
/* ------------------------------------------------------------------ */

void HAL_UART_PutString(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            HAL_UART_PutChar('\r');
        }
        HAL_UART_PutChar(*s++);
    }
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_PutHex                                                   */
/* ------------------------------------------------------------------ */

void HAL_UART_PutHex(uint32_t value)
{
    static const char hex[] = "0123456789abcdef";
    HAL_UART_PutChar('0');
    HAL_UART_PutChar('x');

    bool leading = true;
    for (int shift = 28; shift >= 0; shift -= 4) {
        int nibble = (int)((value >> (uint32_t)shift) & 0xF);
        if (nibble != 0) leading = false;
        if (!leading || shift == 0) {
            HAL_UART_PutChar(hex[nibble]);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  HAL_UART_PutDec                                                   */
/* ------------------------------------------------------------------ */

void HAL_UART_PutDec(uint32_t value)
{
    char buf[12];
    int  pos = 0;

    if (value == 0) {
        HAL_UART_PutChar('0');
        return;
    }

    while (value > 0) {
        buf[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }

    /* Print reversed */
    for (int i = pos - 1; i >= 0; i--) {
        HAL_UART_PutChar(buf[i]);
    }
}
