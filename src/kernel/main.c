/**
 * @file main.c
 * @brief MiniOS-NetProtocol kernel entry point
 *
 * Integrates the RUDP network layer with the existing MiniOS
 * ML inference pipeline. This is the kernel main for the
 * net-protocol branch.
 *
 * Boot sequence:
 *   1. HAL init  (from MiniOS-BootLoader_and_HAL)
 *   2. RUDP/ETH init
 *   3. Open session to configured peer
 *   4. Cooperative main loop:
 *        RUDP_Receive() → dispatch command → RUDP_Poll()
 *
 * The ONNX inference commands (0x01-0x04) are forwarded to
 * the ML runtime (MiniOS-feat-onnx / MiniOS-build stubs here).
 */

#include "../include/net/rudp.h"
#include "../include/net/net_types.h"
/* HAL from MiniOS-BootLoader_and_HAL */
#include "../../MiniOS-BootLoader_and_HAL/include/hal/uart.h"
#include "../../MiniOS-BootLoader_and_HAL/include/status.h"

/* ------------------------------------------------------------------ */
/*  Configuration (override via Makefile -DNET_PEER_MAC=...           */
/* ------------------------------------------------------------------ */
#ifndef NET_LOCAL_MAC
/* Default: locally administered, QEMU-friendly MAC */
static const uint8_t LOCAL_MAC[ETH_ALEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
#else
static const uint8_t LOCAL_MAC[ETH_ALEN] = NET_LOCAL_MAC;
#endif

#ifndef NET_PEER_MAC
/* Default: QEMU tap0 MAC (52:54:00:12:34:57) */
static const uint8_t PEER_MAC[ETH_ALEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x57};
#else
static const uint8_t PEER_MAC[ETH_ALEN] = NET_PEER_MAC;
#endif

/* ------------------------------------------------------------------ */
/*  Stubs — replace with real implementations from feat-onnx branch  */
/* ------------------------------------------------------------------ */
static void on_load_model(const uint8_t *data, uint16_t len)
{
    HAL_UART_PutString("[NET] CMD_LOAD_MODEL received, bytes=");
    HAL_UART_PutDec(len);
    HAL_UART_PutString("\r\n");
    /* TODO: call ONNX_LoadModel(data, len) from MiniOS-feat-onnx */
    (void)data;
}

static void on_set_input(const uint8_t *data, uint16_t len)
{
    HAL_UART_PutString("[NET] CMD_SET_INPUT received, bytes=");
    HAL_UART_PutDec(len);
    HAL_UART_PutString("\r\n");
    /* TODO: call ML_SetInputTensor(data, len) */
    (void)data;
}

static void on_run_inference(void)
{
    HAL_UART_PutString("[NET] CMD_RUN_INFERENCE\r\n");
    /* TODO: call EXEC_RunGraph() */
}

static void on_get_results(RudpSession *session)
{
    HAL_UART_PutString("[NET] CMD_GET_RESULTS — sending stub response\r\n");
    /* TODO: collect real result tensor; for now send 4-byte placeholder */
    uint8_t result[4] = {0x00, 0x01, 0x02, 0x03};
    RUDP_Send(session, NET_CMD_GET_RESULTS, result, sizeof(result));
}

static void on_system_status(RudpSession *session)
{
    /* Send stats as payload */
    RudpStats stats;
    RUDP_GetStats(&stats);
    uint8_t payload[sizeof(RudpStats)];
    const uint8_t *sp = (const uint8_t*)&stats;
    for (size_t i = 0; i < sizeof(RudpStats); i++) payload[i] = sp[i];
    RUDP_Send(session, NET_CMD_SYSTEM_STATUS, payload, (uint16_t)sizeof(payload));
}

/* ------------------------------------------------------------------ */
/*  Receive callback (called from RUDP_Receive)                       */
/* ------------------------------------------------------------------ */
static RudpSession *g_session = NULL;

static void net_receive_callback(NetCommand cmd,
                                  const uint8_t *payload,
                                  uint16_t payload_len)
{
    switch (cmd) {
    case NET_CMD_LOAD_MODEL:
        on_load_model(payload, payload_len);
        break;
    case NET_CMD_SET_INPUT:
        on_set_input(payload, payload_len);
        break;
    case NET_CMD_RUN_INFERENCE:
        on_run_inference();
        break;
    case NET_CMD_GET_RESULTS:
        on_get_results(g_session);
        break;
    case NET_CMD_SYSTEM_STATUS:
        on_system_status(g_session);
        break;
    case NET_CMD_CONFIG_UPDATE:
        HAL_UART_PutString("[NET] CMD_CONFIG_UPDATE\r\n");
        break;
    case NET_CMD_BENCHMARK:
        HAL_UART_PutString("[NET] CMD_BENCHMARK\r\n");
        RUDP_PrintStats();
        break;
    default:
        HAL_UART_PutString("[NET] Unknown command: 0x");
        HAL_UART_PutHex((uint8_t)cmd);
        HAL_UART_PutString("\r\n");
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Kernel entry                                                      */
/* ------------------------------------------------------------------ */
void kernel_main(void)
{
    static RudpSession session;
    g_session = &session;

    HAL_UART_PutString("\r\n[MiniOS-NetProtocol] RUDP stack starting...\r\n");

    /* Initialize RUDP / Ethernet */
    Status st = RUDP_Init(LOCAL_MAC);
    if (st != STATUS_OK) {
        HAL_UART_PutString("[NET] RUDP_Init failed\r\n");
        return;
    }
    HAL_UART_PutString("[NET] Ethernet initialized\r\n");

    /* Register receive callback */
    RUDP_RegisterCallback(net_receive_callback);

    /* Open session to peer (sends KEEPALIVE) */
    st = RUDP_OpenSession(&session, PEER_MAC);
    if (st != STATUS_OK) {
        HAL_UART_PutString("[NET] RUDP_OpenSession failed — continuing in server mode\r\n");
        session.active = true; /* remain open for incoming */
        for (int i = 0; i < ETH_ALEN; i++) session.remote_mac[i] = PEER_MAC[i];
        session.tx_seq = 1;
        session.rx_expected = 1;
    }
    HAL_UART_PutString("[NET] Session open — entering cooperative loop\r\n");

    /* ----------------------------------------------------------------
     * Cooperative main loop
     * Call RUDP_Receive() and RUDP_Poll() from here.
     * In the full integration, these calls would be embedded into the
     * existing MiniOS-build or MiniOS-feat-onnx main loop alongside
     * EXEC_RunGraph() and PERF_Monitor().
     * ---------------------------------------------------------------- */
    for (;;) {
        /* Process one incoming frame (non-blocking) */
        RUDP_Receive(&session);

        /* Service retransmit timers */
        RUDP_Poll(&session);

        /* In full integration: yield to ML scheduler here
         *   EXEC_YieldToScheduler();
         */
    }
}