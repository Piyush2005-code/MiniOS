/**
 * @file rudp.h
 * @brief MiniOS Reliable UDP (RUDP) — Public API
 *
 * RUDP is the 9th branch of MiniOS providing an internet protocol layer
 * purpose-built for ARM64 bare-metal ML inference communication.
 *
 * Why RUDP instead of raw TCP or UDP?
 * ─────────────────────────────────────────────────────────────────────
 *  Raw UDP   — No delivery guarantee, silent loss for model/result frames
 *  Full TCP  — 3-way handshake, congestion control, complex state machine
 *              incompatible with bare-metal single-address-space unikernel
 *  RUDP      — Selective reliability per command class:
 *              • ML control frames (load, inference, results) → ACK+retry
 *              • Telemetry/status → best-effort (no overhead)
 *              • Fragmentation for large ONNX models (> 1452 bytes)
 *              • CRC-16 for data integrity (stronger than UART CRC-8)
 *              • Session keepalive compatible with cooperative scheduler
 *              • No OS sockets — directly drives LAN9118 / SMSC Ethernet
 *                or tunnelled over QEMU virtio-net for development
 *
 * Integration points:
 *   → Calls HAL_TIMER_GetMs()       (MiniOS-BootLoader_and_HAL)
 *   → Calls HAL_ETH_Send/Recv()     (net/eth_driver.h, this branch)
 *   → Shares NetCommand codes with  (MiniOS-UART_Implementation)
 *   → Used by main.c in            (MiniOS-feat-onnx, MiniOS-build)
 */

#ifndef MINIOS_RUDP_H
#define MINIOS_RUDP_H

#include "net_types.h"

/* ------------------------------------------------------------------ */
/*  Initialization & teardown                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the RUDP stack and underlying Ethernet driver.
 *
 * Must be called after HAL initialization (bootloader phase).
 * Configures the MAC address, resets session state, and enables
 * the Ethernet interrupt (if supported by hardware).
 *
 * @param[in] local_mac  6-byte MAC address for this node
 * @return STATUS_OK on success
 */
Status RUDP_Init(const uint8_t local_mac[ETH_ALEN]);

/**
 * @brief Shut down RUDP and disable Ethernet.
 */
void RUDP_Shutdown(void);

/* ------------------------------------------------------------------ */
/*  Session management                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Open a session to a remote peer.
 *
 * Sends a KEEPALIVE frame and waits for a response within
 * RUDP_RETRY_TIMEOUT_MS. No 3-way handshake — lightweight.
 *
 * @param[out] session     Session object to initialise
 * @param[in]  remote_mac  Peer MAC address
 * @return STATUS_OK if peer responded
 */
Status RUDP_OpenSession(RudpSession *session,
                        const uint8_t remote_mac[ETH_ALEN]);

/**
 * @brief Close and reset a session.
 * Sends a RESET frame (best-effort) to notify the peer.
 *
 * @param[in] session  Session to close
 */
void RUDP_CloseSession(RudpSession *session);

/**
 * @brief Poll the session keepalive and retransmit timers.
 *
 * Must be called periodically from the main cooperative scheduler
 * loop (every ~10 ms). Handles:
 *   • Retransmit of unACKed frames
 *   • Keepalive transmission
 *   • Dead session detection
 *
 * @param[in] session  Active session
 * @return STATUS_OK or STATUS_ERROR_TIMEOUT if session dead
 */
Status RUDP_Poll(RudpSession *session);

/* ------------------------------------------------------------------ */
/*  Send                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Send a command frame to the peer.
 *
 * Automatically selects reliability class based on command code
 * (see NET_CMD_IS_RELIABLE macro in net_types.h).
 *
 * For payloads larger than RUDP_MAX_PAYLOAD, use RUDP_SendLarge().
 *
 * @param[in] session      Active session
 * @param[in] cmd          Command code (NetCommand)
 * @param[in] payload      Payload bytes (may be NULL)
 * @param[in] payload_len  Length of payload (0..RUDP_MAX_PAYLOAD)
 * @return STATUS_OK on success
 */
Status RUDP_Send(RudpSession *session,
                 NetCommand   cmd,
                 const uint8_t *payload,
                 uint16_t     payload_len);

/**
 * @brief Send a large payload with automatic fragmentation.
 *
 * Used for loading ONNX model binaries that exceed one MTU.
 * Fragments are sent as RUDP_CLASS_RELIABLE frames; all must
 * be ACKed before the next fragment is sent (stop-and-wait per
 * fragment for simplicity compatible with cooperative scheduler).
 *
 * @param[in] session      Active session
 * @param[in] cmd          Command code (NET_CMD_LOAD_MODEL)
 * @param[in] data         Full data buffer
 * @param[in] total_len    Total bytes to send
 * @return STATUS_OK when all fragments ACKed
 */
Status RUDP_SendLarge(RudpSession *session,
                      NetCommand   cmd,
                      const uint8_t *data,
                      size_t       total_len);

/**
 * @brief Send a raw ACK for a received sequence number.
 *
 * Normally called internally; exposed for custom ACK scheduling.
 *
 * @param[in] session  Active session
 * @param[in] ack_seq  Sequence number to acknowledge
 * @return STATUS_OK
 */
Status RUDP_SendAck(RudpSession *session, uint32_t ack_seq);

/* ------------------------------------------------------------------ */
/*  Receive                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Receive callback type.
 *
 * Called from RUDP_Receive() when a complete, validated frame
 * arrives for a given command. ACK frames are handled internally
 * and never delivered to the callback.
 *
 * @param cmd          Received command
 * @param payload      Pointer to payload data (valid only during callback)
 * @param payload_len  Payload length
 */
typedef void (*RudpRecvCallback)(NetCommand     cmd,
                                 const uint8_t *payload,
                                 uint16_t       payload_len);

/**
 * @brief Register a receive callback.
 *
 * @param[in] cb  Callback function
 */
void RUDP_RegisterCallback(RudpRecvCallback cb);

/**
 * @brief Poll for incoming frames and process them.
 *
 * Non-blocking. Reads one frame from the Ethernet driver, validates
 * CRC-16, dispatches ACKs internally, and invokes the registered
 * callback for data frames.
 *
 * Should be called from the main cooperative loop alongside RUDP_Poll().
 *
 * @param[in] session  Active session
 * @return STATUS_OK if a frame was processed, STATUS_ERROR_TIMEOUT if none
 */
Status RUDP_Receive(RudpSession *session);

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t frames_sent;
    uint32_t frames_recv;
    uint32_t frames_acked;
    uint32_t retransmits;
    uint32_t crc_errors;
    uint32_t seq_errors;
    uint32_t fragments_sent;
    uint32_t fragments_recv;
    uint32_t keepalives_sent;
    uint32_t session_resets;
} RudpStats;

/**
 * @brief Return a snapshot of RUDP statistics.
 * @param[out] stats  Filled with current counters
 */
void RUDP_GetStats(RudpStats *stats);

/**
 * @brief Reset all statistics counters to zero.
 */
void RUDP_ResetStats(void);

/**
 * @brief Print statistics over HAL_UART (debug).
 */
void RUDP_PrintStats(void);

#endif /* MINIOS_RUDP_H */