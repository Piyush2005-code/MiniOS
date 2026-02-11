/*
 * main.c - Main Entry Point and UART Driver
 * 
 * This file contains:
 * - PL011 UART driver for QEMU virt machine
 * - System initialization
 * - Main entry point for user ML inference code
 */

#include <stdint.h>

/*
 * PL011 UART Register Definitions
 * Base address: 0x09000000 (QEMU virt machine)
 */
#define UART_BASE       0x09000000UL

#define UART_DR         (*(volatile uint32_t *)(UART_BASE + 0x00))  /* Data Register */
#define UART_FR         (*(volatile uint32_t *)(UART_BASE + 0x18))  /* Flag Register */
#define UART_IBRD       (*(volatile uint32_t *)(UART_BASE + 0x24))  /* Integer Baud Rate Divisor */
#define UART_FBRD       (*(volatile uint32_t *)(UART_BASE + 0x28))  /* Fractional Baud Rate Divisor */
#define UART_LCRH       (*(volatile uint32_t *)(UART_BASE + 0x2C))  /* Line Control Register */
#define UART_CR         (*(volatile uint32_t *)(UART_BASE + 0x30))  /* Control Register */
#define UART_IMSC       (*(volatile uint32_t *)(UART_BASE + 0x38))  /* Interrupt Mask Set/Clear */
#define UART_ICR        (*(volatile uint32_t *)(UART_BASE + 0x44))  /* Interrupt Clear Register */

/* UART_FR flag register bits */
#define UART_FR_TXFF    (1 << 5)        /* Transmit FIFO Full */
#define UART_FR_RXFE    (1 << 4)        /* Receive FIFO Empty */

/* UART_LCRH line control register bits */
#define UART_LCRH_WLEN_8    (3 << 5)    /* 8 bits word length */
#define UART_LCRH_FEN       (1 << 4)    /* Enable FIFOs */

/* UART_CR control register bits */
#define UART_CR_UARTEN  (1 << 0)        /* UART Enable */
#define UART_CR_TXE     (1 << 8)        /* Transmit Enable */
#define UART_CR_RXE     (1 << 9)        /* Receive Enable */

/*
 * External functions (implemented in assembly)
 */
extern void mmu_init(void);

/*
 * Forward declarations
 */
static void uart_init(void);
static void uart_putc(char c);
void print(const char *str);

/*
 * UART Initialization
 * 
 * Configures PL011 for 115200 baud, 8N1, with FIFOs enabled
 * Reference clock: 24 MHz (QEMU default)
 * 
 * Baud rate divisor = UARTCLK / (16 * baud_rate)
 *                   = 24000000 / (16 * 115200)
 *                   = 13.0208...
 * Integer part: 13
 * Fractional part: 0.0208 * 64 = 1.33 ≈ 1
 */
static void uart_init(void)
{
    /* Disable UART before configuration */
    UART_CR = 0;
    
    /* Clear all interrupts */
    UART_ICR = 0x7FF;
    
    /* Set baud rate to 115200 */
    UART_IBRD = 13;         /* Integer part */
    UART_FBRD = 1;          /* Fractional part */
    
    /* Configure line control: 8 bits, no parity, 1 stop bit, FIFOs enabled */
    UART_LCRH = UART_LCRH_WLEN_8 | UART_LCRH_FEN;
    
    /* Disable all interrupts (we're polling) */
    UART_IMSC = 0;
    
    /* Enable UART, transmit, and receive */
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

/*
 * Send a single character to UART
 * 
 * Waits while transmit FIFO is full, then writes to data register
 */
static void uart_putc(char c)
{
    /* Wait while transmit FIFO is full */
    while (UART_FR & UART_FR_TXFF)
        ;
    
    /* Write character to data register */
    UART_DR = (uint32_t)c;
}

/*
 * Print a null-terminated string to UART
 * 
 * Public function - can be called from assembly exception handlers
 */
void print(const char *str)
{
    while (*str) {
        /* Convert LF to CRLF for proper terminal display */
        if (*str == '\n')
            uart_putc('\r');
        uart_putc(*str++);
    }
}

/*
 * Main Entry Point
 * 
 * Called from start.S after MMU initialization
 * This is where you implement your ML inference logic
 */
void main(void)
{
    /* Initialize UART for console output */
    uart_init();
    
    /* Print startup banner (as per SRS specification) */
    print("\n");
    print("==============================================\n");
    print("  ML Inference Unikernel - ARM64 Bare Metal\n");
    print("==============================================\n");
    print("\n");
    print("System Information:\n");
    print("  Architecture: ARMv8-A (AArch64)\n");
    print("  Exception Level: EL1\n");
    print("  MMU: Enabled (4KB granule, 3-level tables)\n");
    print("  FP/SIMD: Enabled (NEON)\n");
    print("  UART: PL011 @ 0x09000000 (115200 8N1)\n");
    print("\n");
    print("Status: Ready for ML inference workload\n");
    print("\n");
    print("----------------------------------------------\n");
    print("Add your ML inference code in main.c:main()\n");
    print("----------------------------------------------\n");
    print("\n");
    
    /*
     * ============================================
     * TODO: Implement your ML inference code here
     * ============================================
     * 
     * At this point:
     * - MMU is configured and enabled
     * - FP/SIMD (NEON) is available
     * - UART is ready for debug output via print()
     * - All memory is statically allocated
     * 
     * Next steps:
     * 1. Add your neural network model data (weights, biases)
     * 2. Implement inference functions using NEON intrinsics
     * 3. Load input data (from memory or UART)
     * 4. Run inference
     * 5. Output results via print()
     * 
     * Example skeleton:
     * 
     *   print("Loading model...\n");
     *   // Load your model weights
     *   
     *   print("Running inference...\n");
     *   // Call your inference function
     *   // float32_t result = ml_inference(input_data);
     *   
     *   print("Inference complete!\n");
     *   // Format and print results
     */
    
    /* Idle loop - wait for interrupts */
    print("Entering idle state (WFI loop)...\n\n");
    
    while (1) {
        __asm__ volatile("wfi");  /* Wait for interrupt */
    }
}
