/**
 * @file main.c
 * @brief MiniOS-ESP8266 Boot Entry Point  (Checkpoint 1)
 *
 * -----------------------------------------------------------------------
 * ARCHITECTURE NOTE — why this file is different from src/kernel/main.c
 * -----------------------------------------------------------------------
 * The ARM64 entry chain is:
 *   boot.S (EL1 reset vector) → kernel_main() → SCHED_Start() [blocks]
 *
 * The ESP8266 NonOS SDK entry chain is:
 *   ROM bootloader → SDK RF init → user_init() → return → SDK event loop
 *
 * The critical difference: user_init() MUST RETURN promptly.  Any code
 * guarded by #ifdef ARCH_ARM64 in the shared tree is skipped here:
 *   - MMU initialisation  (no virtual memory on Xtensa LX106)
 *   - GIC initialisation  (no ARM GIC; ESP8266 uses ETS interrupts)
 *   - Exception vector install (VBAR_EL1 — ARM64-only register)
 *   - Context-switch assembly (context.S — ARM64 ABI)
 *   - arch_enable_irq() / arch_wfe()  (ARM64 system instructions)
 *
 * Memory management: KMEM_Init() sets up a 24 KB bump allocator inside
 * the ESP8266's 80 KB data RAM. MiniOS-owned allocations must go through
 * KMEM_Alloc()/KMEM_ArenaAlloc(), which are simple flat physical allocators
 * with no free. SDK internals and any bundled libraries keep using the
 * NonOS SDK heap provided by libmain.a; we do not override malloc/free.
 *
 * Scheduler: the ARM64 SCHED_Start() busy-loops in a while(1) that would
 * instantly WDT-reset the ESP8266.  We replace it with SCHED_Init() which
 * registers a system_os_task + os_timer and then returns.  The SDK event
 * loop drives the scheduler from that point.
 *
 * Shell: SHELL_Init() arms an os_timer that polls HAL_UART_HasChar() every
 * 20 ms.  HAL_UART_GetChar() is called ONLY when HasChar() returns true,
 * so it never blocks.  The shell runs entirely in timer-callback context.
 *
 * -----------------------------------------------------------------------
 * BOOT SEQUENCE (user_init)
 * -----------------------------------------------------------------------
 *   1.  HAL_UART_Init()     — UART0 at 115200, 8-N-1, polling mode
 *   2.  KMEM_Init()         — 24 KB bump heap in dRAM, no MMU needed
 *   3.  HAL_Timer_Init()    — reset os_timer state, set 10 ms tick period
 *   4.  SCHED_Init()        — register system_os_task + arm tick timer
 *   5.  HAL_WiFi_Init()     — begin STATION mode association (async)
 *   6.  HAL_WiFi_WaitForIP()— poll up to 10 s for DHCP (feeds WDT)
 *   7.  UDP_Init()          — reset espconn UDP socket layer
 *   8.  SFU_Init()          — bind UDP:9000, init CRC state
 *   9.  SFU_SelfTest()      — CRC16 round-trip sanity check
 *  10.  INFER_Init()        — load default ONNX model from flash
 *  11.  SHELL_Init()        — arm shell poll timer (20 ms)
 *  12.  sfu_tick_timer_init()— arm SFU retransmit timer (100 ms)
 *  13.  HAL_Timer_Enable()  — arm system tick timer (10 ms)
 *  14.  return              — SDK event loop takes over
 *
 * After step 14 the CPU is driven by:
 *   - SDK Wi-Fi/TCP task at highest priority
 *   - sched_task_runner at ESP_TASK_PRIO_2 (our cooperative scheduler)
 *   - espconn callbacks for SFU packet reception
 *   - os_timer callbacks: shell poll, SFU tick, system tick
 */

/* ------------------------------------------------------------------ */
/*  Shared MiniOS headers (ESP8266 variants in esp8266/include/)       */
/* ------------------------------------------------------------------ */

#include "hal/uart.h"
#include "hal/timer.h"
#include "hal/wifi.h"
#include "kernel/kmem.h"
#include "kernel/shell.h"
#include "kernel/sched.h"    /* NEW — cooperative scheduler */
#include "net/udp.h"
#include "net/sfu.h"
#include "net/infer_server.h"
#include "types.h"
#include "status.h"
#include "user_config.h"

/*
 * ARM64-specific headers are intentionally excluded.
 * Each of these would fail to compile on xtensa-lx106-elf-gcc:
 *
 *   #ifdef ARCH_ARM64
 *   #  include "hal/mmu.h"    // ARM64 MMU (TTBR0/TTBR1, page tables)
 *   #  include "hal/gic.h"    // ARM GIC-400 interrupt controller
 *   #  include "hal/arch.h"   // aarch64 system registers (mrs/msr)
 *   #  include "kernel/thread.h"  // cpu_context_t with x19-x29/lr/sp
 *   #endif
 */

/* ------------------------------------------------------------------ */
/*  ESP8266 NonOS SDK                                                  */
/* ------------------------------------------------------------------ */

#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"

/* ------------------------------------------------------------------ */
/*  SFU retransmission tick timer                                      */
/* ------------------------------------------------------------------ */

static os_timer_t g_sfu_tick_timer;

/**
 * @brief SFU tick callback — handles RUDP retransmissions.
 *
 * Runs every SFU_TICK_INTERVAL_MS (100 ms) in os_timer context.
 * SFU_Tick() is O(open_sessions) and completes in < 1 ms normally.
 */
static void ICACHE_FLASH_ATTR sfu_tick_cb(void *arg)
{
    (void)arg;
    SFU_Tick();
}

static void ICACHE_FLASH_ATTR sfu_tick_timer_init(void)
{
    os_timer_disarm(&g_sfu_tick_timer);
    os_timer_setfn(&g_sfu_tick_timer,
                   (os_timer_func_t)sfu_tick_cb,
                   NULL);
    os_timer_arm(&g_sfu_tick_timer, SFU_TICK_INTERVAL_MS, 1 /* repeat */);
}

/* ------------------------------------------------------------------ */
/*  Boot banner                                                        */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR print_banner(void)
{
    HAL_UART_PutString("\n");
    HAL_UART_PutString("========================================\n");
    HAL_UART_PutString("  MiniOS-ESP8266  —  Checkpoint 1\n");
    HAL_UART_PutString("  Xtensa LX106 @ 80 MHz | NonOS SDK\n");
    HAL_UART_PutString("  Cooperative Scheduler + Shell\n");
    HAL_UART_PutString("========================================\n");
}

/* ------------------------------------------------------------------ */
/*  IP-ready banner (called after Wi-Fi DHCP succeeds)                */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR print_ip_ready(void)
{
    char ip[16];
    HAL_WiFi_GetIPString(ip);
    HAL_UART_PutString("[BOOT] IP address  : ");
    HAL_UART_PutString(ip);
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] SFU server  : udp://");
    HAL_UART_PutString(ip);
    HAL_UART_PutString(":9000\n");
    HAL_UART_PutString("[BOOT] Client cmd  : python3 sfu_client.py\n\n");
}

/* ------------------------------------------------------------------ */
/*  user_pre_init — called by SDK before user_init()                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Pre-init hook called by libmain before user_init().
 *
 * Required by ESP8266 NonOS SDK v2.0+ (libmain.a app_main.o).
 * Use this for any early hardware configuration that must happen before
 * the RF stack initialises (e.g. disabling sleep modes, setting CPU freq).
 * We have nothing to do here — leave empty.
 */
void ICACHE_FLASH_ATTR user_pre_init(void)
{
    /* no early init required */
}

/*  user_init — ESP8266 NonOS SDK mandatory entry point               */
/* ------------------------------------------------------------------ */

/**
 * @brief Boot entry point called by the ESP8266 NonOS SDK.
 *
 * This function MUST RETURN.  The SDK continues into its own event loop
 * after user_init() returns.  Blocking here for any significant duration
 * will trigger the hardware WDT (~1.6 s) or software WDT (~6 s) and
 * reset the chip.
 *
 * The only intentional "long" wait here is HAL_WiFi_WaitForIP() which
 * internally calls os_delay_us() in 10 ms chunks and feeds the WDT on
 * each iteration, keeping the chip alive during DHCP association.
 *
 * ARCH_ARM64-guarded code removed from this version:
 *   - install_vectors()   → VBAR_EL1 MSR (ARM64-only)
 *   - HAL_MMU_Init()      → page-table setup (no MMU on LX106)
 *   - HAL_GIC_Init()      → ARM GIC-400 (no such peripheral)
 *   - HAL_GIC_EnableIRQ() → GIC distributor register write
 *   - arch_enable_irq()   → DAIF mask clear (ARM64-only)
 *   - arch_wfe()          → WFE instruction (ARM64-only)
 *   - SCHED_Start()       → blocking while(1) — replaced by SCHED_Init()
 */
void ICACHE_FLASH_ATTR user_init(void)
{
    Status s;

    /* ---- Step 1: UART — first so all subsequent logs are visible ---- */
    HAL_UART_Init();
    print_banner();
    HAL_UART_PutString("[BOOT] UART0 ready (115200 8-N-1, polling)\n");

    /*
     * ARM64-only step SKIPPED:
     *   install_vectors() / HAL_MMU_Init() / HAL_GIC_Init()
     *
     * #ifdef ARCH_ARM64
     *   install_vectors();
     *   HAL_MMU_Init();
     *   HAL_GIC_Init();
     *   HAL_GIC_EnableIRQ(IRQ_TIMER_PHYS);
     * #endif
     *
     * The ESP8266 uses ETS (Xtensa interrupt subsystem) managed by the
     * NonOS SDK.  We must not touch it directly.
     */

    /* ---- Step 2: Memory manager — flat physical bump allocator ----
     *
     * KMEM_Init() carves KMEM_HEAP_SIZE (24 KB, from user_config.h)
     * out of dRAM as a static array.  All KMEM_Alloc() calls are a
     * trivial pointer bump — no free, no fragmentation, no MMU needed.
     *
     * ARM64 used a tiered slab+arena system backed by virtual pages.
     * Here: one contiguous physical chunk, allocated once at boot.
     *
     * MiniOS code on ESP8266 must allocate through KMEM_* APIs.
     * The SDK and its bundled libraries retain ownership of plain
     * malloc/free/calloc/realloc from libmain.a.
     */
    s = KMEM_Init();
    if (s != STATUS_OK) {
        HAL_UART_PutString("[BOOT] KMEM init FAILED — halting\n");
        /* Cannot proceed without a heap. Soft-reset the chip. */
        system_restart();
        return;
    }
    HAL_UART_PutString("[BOOT] KMEM  : ");
    HAL_UART_PutDec(KMEM_HEAP_SIZE / 1024);
    HAL_UART_PutString(" KB heap ready\n");
    HAL_UART_PutString("[BOOT] Free SDK heap: ");
    HAL_UART_PutDec(system_get_free_heap_size());
    HAL_UART_PutString(" bytes\n");

    /* ---- Step 3: Timer HAL — reset os_timer book-keeping ---- */
    HAL_Timer_Init();
    HAL_Timer_SetInterval(10000);   /* 10 ms tick period (in µs) */
    HAL_UART_PutString("[BOOT] Timer : 10 ms tick configured\n");

    /* ---- Step 4: Cooperative Scheduler ----
     *
     * SCHED_Init() replaces the ARM64 SCHED_Start() which entered a
     * blocking  while(1) { THREAD_Yield(); arch_wfe(); }.
     *
     * On ESP8266:
     *   - Registers a system_os_task at ESP_TASK_PRIO_2
     *   - Arms a repeating os_timer at SCHED_TICK_MS (20 ms)
     *   - Returns immediately — the SDK event loop drives everything
     *
     * Tasks submitted via SCHED_Submit() execute in the system_os_task
     * context, one per 20 ms tick, with a max wall time of ~18 ms each.
     */
    s = SCHED_Init();
    if (s != STATUS_OK) {
        HAL_UART_PutString("[BOOT] SCHED init FAILED\n");
    } else {
        HAL_UART_PutString("[BOOT] SCHED : cooperative scheduler running\n");
    }

    /* ---- Step 5: Wi-Fi — async station association ---- */
    s = HAL_WiFi_Init();
    if (s != STATUS_OK) {
        HAL_UART_PutString("[BOOT] WiFi init FAILED (continuing offline)\n");
    } else {
        HAL_UART_PutString("[BOOT] WiFi  : associating to '");
        HAL_UART_PutString(WIFI_SSID);
        HAL_UART_PutString("'...\n");
    }

    /* ---- Step 6: Wait for DHCP (up to 10 s) ----
     *
     * HAL_WiFi_WaitForIP() internally loops in 10 ms chunks calling
     * os_delay_us(10000) and system_soft_wdt_feed(), so the WDT is
     * kept alive during the wait.  At most 1000 iterations = 10 s.
     */
    s = HAL_WiFi_WaitForIP(10000);
    if (s == STATUS_OK) {
        print_ip_ready();
    } else {
        HAL_UART_PutString("[BOOT] WARNING: no IP obtained — "
                           "SFU will be unavailable until connected\n\n");
    }

    /* ---- Step 7: UDP socket layer ---- */
    UDP_Init();
    HAL_UART_PutString("[BOOT] UDP   : socket layer ready\n");

    /* ---- Step 8: SFU protocol — bind UDP:9000 ---- */
    SFU_Init();
    SFU_SelfTest();   /* CRC16 round-trip check */
    HAL_UART_PutString("[BOOT] SFU   : listening on UDP:9000\n");

    /* ---- Step 9: ONNX runtime + default model from flash ---- */
    INFER_Init();
    HAL_UART_PutString("[BOOT] INFER : model '");
    HAL_UART_PutString(DEFAULT_MODEL);
    HAL_UART_PutString("' loaded\n");

    /* ---- Step 10: UART shell (20 ms poll timer) ----
     *
     * SHELL_Init() arms its own os_timer that calls shell_poll_cb()
     * every 20 ms.  The callback reads from UART using HAL_UART_HasChar()
     * (non-blocking check) followed by HAL_UART_GetChar() only when a
     * byte is available.
     *
     * HAL_UART_GetChar() on ESP8266 blocks in a spin-wait on the RX
     * FIFO count register.  Because we always check HasChar() first,
     * GetChar() returns immediately — no blocking in practice.
     *
     * The shell dispatches to command handlers which must themselves
     * be non-blocking.  Long commands (e.g. "infer") should call
     * SCHED_Submit() to defer heavy work rather than running inline.
     */
    SHELL_Init();
    HAL_UART_PutString("[BOOT] Shell : ready (poll every 20 ms)\n");

    /* ---- Step 11: SFU retransmit timer (100 ms) ---- */
    sfu_tick_timer_init();
    HAL_UART_PutString("[BOOT] SFU tick timer: 100 ms\n");

    /* ---- Step 12: Enable system tick timer ---- */
    HAL_Timer_Enable();
    HAL_UART_PutString("[BOOT] System tick : enabled (10 ms)\n");

    /* ---- Boot complete ---- */
    HAL_UART_PutString("\n");
    HAL_UART_PutString("[BOOT] ====================================\n");
    HAL_UART_PutString("[BOOT]  Boot complete — SDK event loop\n");
    HAL_UART_PutString("[BOOT]  Scheduler tick : ");
    HAL_UART_PutDec(SCHED_TICK_MS);
    HAL_UART_PutString(" ms\n");
    HAL_UART_PutString("[BOOT]  Free heap      : ");
    HAL_UART_PutDec(system_get_free_heap_size());
    HAL_UART_PutString(" bytes\n");
    HAL_UART_PutString("[BOOT] ====================================\n\n");

    /*
     * user_init() RETURNS HERE.
     *
     * The NonOS SDK event loop takes over and drives:
     *   - Wi-Fi state machine (beacon keep-alive, DHCP renewal)
     *   - espconn callbacks (SFU packet receive)
     *   - os_timer callbacks (shell poll, SFU tick, system tick)
     *   - system_os_task callbacks (our cooperative scheduler tasks)
     *
     * Nothing in the ARM64 SCHED_Start() while(1) loop executes.
     * The scheduler's os_timer+system_os_task pair replicates its
     * intent without any blocking.
     */
}

/* ------------------------------------------------------------------ */
/*  Required NonOS SDK stubs                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Called by SDK before RF calibration.
 * Must be defined; can be empty.
 */
void ICACHE_FLASH_ATTR user_rf_pre_init(void) { }

/**
 * @brief Tell the SDK which flash sector to use for RF calibration.
 *
 * For a 4 MB flash (ESP-12E default):
 *   Total sectors = 4*1024*1024 / 4096 = 1024 = 0x400
 *   RF cal at 0x400 - 5 = 0x3FB
 *
 * @return Sector index for RF calibration data.
 */
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    return 0x3FBUL;
}
