/**
 * @file main.c
 * @brief MiniOS-ESP8266 Entry Point
 *
 * The ESP8266 NonOS SDK calls user_init() after the RF subsystem and
 * internal OS structures have been set up. This replaces the ARM64
 * boot.S + kernel_main() chain with a single C function that:
 *
 *   1. Initializes UART for debug output
 *   2. Initializes the memory manager (24 KB bump allocator)
 *   3. Starts Wi-Fi in station mode and waits up to 10s for an IP
 *   4. Initializes the UDP socket layer (port 9000)
 *   5. Initializes the SFU protocol layer (self-tests CRC16)
 *   6. Loads the default ONNX model (tiny_mlp from flash)
 *   7. Starts the UART shell (polled every 20ms via os_timer)
 *   8. Starts the SFU tick timer (retransmissions every 100ms)
 *   9. Prints the IP address and "Ready" banner
 *
 * After user_init() returns, the NonOS SDK event loop takes over.
 * All packet reception is interrupt/callback-driven (espconn).
 * The shell and SFU tick run as os_timer callbacks.
 *
 * No threads, no MMU, no GIC — just callbacks and timers.
 */

#include "hal/uart.h"
#include "hal/timer.h"
#include "hal/wifi.h"
#include "kernel/kmem.h"
#include "kernel/shell.h"
#include "net/udp.h"
#include "net/sfu.h"
#include "net/infer_server.h"
#include "types.h"
#include "status.h"
#include "user_config.h"

/* ESP8266 NonOS SDK */
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"

/* ------------------------------------------------------------------ */
/*  SFU tick timer                                                    */
/* ------------------------------------------------------------------ */

static os_timer_t g_sfu_tick_timer;

static void ICACHE_FLASH_ATTR sfu_tick_cb(void *arg)
{
    (void)arg;
    SFU_Tick();
}

static void ICACHE_FLASH_ATTR sfu_tick_timer_init(void)
{
    os_timer_disarm(&g_sfu_tick_timer);
    os_timer_setfn(&g_sfu_tick_timer, (os_timer_func_t *)sfu_tick_cb, NULL);
    os_timer_arm(&g_sfu_tick_timer, SFU_TICK_INTERVAL_MS, 1);
}

/* ------------------------------------------------------------------ */
/*  Print boot banner                                                 */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR print_banner(void)
{
    HAL_UART_PutString("\n");
    HAL_UART_PutString("========================================\n");
    HAL_UART_PutString("  MiniOS-ESP8266 ML Inference Unikernel\n");
    HAL_UART_PutString("  ESP-12E | Xtensa LX106 | 80 MHz\n");
    HAL_UART_PutString("  SFU Protocol on UDP:9000\n");
    HAL_UART_PutString("========================================\n");
}

/* ------------------------------------------------------------------ */
/*  print_ip_ready — called after Wi-Fi connects                      */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR print_ip_ready(void)
{
    char ip[16];
    HAL_WiFi_GetIPString(ip);
    HAL_UART_PutString("[BOOT] IP address: ");
    HAL_UART_PutString(ip);
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] SFU server ready on udp://");
    HAL_UART_PutString(ip);
    HAL_UART_PutString(":9000\n");
    HAL_UART_PutString("[BOOT] Connect with: python3 sfu_client.py\n\n");
}

/* ------------------------------------------------------------------ */
/*  user_init — ESP8266 NonOS SDK entry point                         */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR user_init(void)
{
    /* 1. UART — first thing, so we can see all subsequent logs */
    HAL_UART_Init();
    print_banner();

    /* 2. Memory manager — 24 KB bump allocator from dRAM */
    Status s = KMEM_Init();
    if (s != STATUS_OK) {
        HAL_UART_PutString("[BOOT] KMEM init FAILED\n");
        return;
    }

    /* 3. Timer subsystem */
    HAL_Timer_Init();
    HAL_Timer_SetInterval(10000); /* 10ms tick period */

    /* 4. Wi-Fi — asynchronous; we wait up to 10s for IP */
    s = HAL_WiFi_Init();
    if (s != STATUS_OK) {
        HAL_UART_PutString("[BOOT] WiFi init FAILED\n");
    }

    /* Block here (poll loop) until IP obtained or 10s timeout */
    s = HAL_WiFi_WaitForIP(10000);
    if (s == STATUS_OK) {
        print_ip_ready();
    } else {
        HAL_UART_PutString("[BOOT] WARNING: no IP — SFU will fail until connected\n");
    }

    /* 5. UDP socket layer */
    UDP_Init();

    /* 6. SFU protocol layer — binds UDP port 9000 */
    SFU_Init();
    SFU_SelfTest();

    /* 7. ONNX runtime + default model (tiny_mlp from flash) */
    INFER_Init();

    /* 8. UART shell (20ms poll timer) */
    SHELL_Init();

    /* 9. SFU retransmission tick timer (100ms) */
    sfu_tick_timer_init();

    /* 10. Enable the HAL timer for system_ticks */
    HAL_Timer_Enable();

    HAL_UART_PutString("[BOOT] boot complete — handing off to event loop\n");
    /* NonOS event loop runs automatically after user_init() returns */
}

/* ------------------------------------------------------------------ */
/*  Required by NonOS SDK                                             */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR user_rf_pre_init(void) { }
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    /*
     * Return the flash sector used for RF calibration data.
     * For a 4MB flash (ESP-12E default): sector = 0x3FB (4MB - 5*4KB)
     */
    return 0x3FB;
}
