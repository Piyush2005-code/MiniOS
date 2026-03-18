/**
 * @file rudp.c
 * @brief MiniOS Reliable UDP (RUDP) — Core Implementation
 *
 * Protocol overview:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  SEND RELIABLE (ML control plane)                          │
 *   │  Sender → [DATA, seq=N, RELIABLE flag] → Receiver          │
 *   │  Receiver → [ACK, ack_seq=N]           → Sender            │
 *   │  On timeout/NACK: retry up to RUDP_MAX_RETRIES times       │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  SEND BEST-EFFORT (telemetry, status)                      │
 *   │  Sender → [DATA, seq=N, no RELIABLE] → Receiver            │
 *   │  No ACK expected. Sequence still increments for ordering.  │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  FRAGMENTATION (large ONNX models)                         │
 *   │  Frag 0 → [FRAG flag, seq=N,   cmd=LOAD_MODEL] → ACK      │
 *   │  Frag 1 → [FRAG flag, seq=N+1, cmd=FRAG_DATA]  → ACK      │
 *   │  Last   → [FRAG_END,  seq=N+k, cmd=FRAG_DATA]  → ACK      │
 *   │  Callback fired ONCE with reassembled buffer.              │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * All frame fields are big-endian on the wire.
 * Cooperative-scheduler friendly: no blocking waits; callers must
 * call RUDP_Poll() and RUDP_Receive() from the main loop.
 *
 * CHANGE LOG:
 *   fix: RUDP_Poll() now gates retransmits by RUDP_RETRY_TIMEOUT_MS
 *        using HAL_Timer_GetMs(). Previously it retransmitted every
 *        call, causing a retransmit storm on every loop iteration.
 *   fix: RUDP_Receive() now reassembles multi-fragment messages into
 *        session->defrag_buf and fires the callback only once the
 *        FRAG_END frame arrives. Previously each fragment triggered a
 *        separate callback call with an incomplete payload.
 *   fix: include paths changed from relative ../../ to use the project
 *        include directory via -I flags in the Makefile, so this file
 *        compiles correctly when integrated into the main build.
 */

#include "net/rudp.h"
#include "net/eth_driver.h"
#include "net/crc16.h"
#include "hal/uart.h"
#include "hal/timer.h"

/* ------------------------------------------------------------------ */
/*  Internal state (no dynamic allocation — SRS DC-002)               */
/* ------------------------------------------------------------------ */

static uint8_t          s_local_mac[ETH_ALEN];
static RudpStats        s_stats;
static RudpRecvCallback s_callback = NULL;

/* Scratch buffers for Tx/Rx — static allocation                      */
static uint8_t s_tx_frame[ETH_HDR_LEN + RUDP_HDR_LEN + RUDP_MAX_PAYLOAD + 2];
static uint8_t s_rx_frame[ETH_HDR_LEN + RUDP_HDR_LEN + RUDP_MAX_PAYLOAD + 2];

/* ------------------------------------------------------------------ */
/*  Utility: big-endian encode/decode                                 */
/* ------------------------------------------------------------------ */

static void put_be32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >>  8);
    buf[3] = (uint8_t)(val      );
}

static uint32_t get_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] <<  8) |
            (uint32_t)buf[3];
}

static void put_be16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val     );
}

static uint16_t get_be16(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

/* ------------------------------------------------------------------ */
/*  Build a raw frame into s_tx_frame                                 */
/*  Returns total frame length (including CRC16 at end)               */
/* ------------------------------------------------------------------ */

static uint16_t build_frame(const uint8_t  dst_mac[ETH_ALEN],
                             uint8_t        flags,
                             uint8_t        cmd,
                             uint32_t       seq,
                             uint16_t       ack_seq,
                             const uint8_t *payload,
                             uint16_t       payload_len)
{
    uint8_t *p = s_tx_frame;

    /* Ethernet header */
    for (int i = 0; i < ETH_ALEN; i++) p[i] = dst_mac[i];
    p += ETH_ALEN;
    for (int i = 0; i < ETH_ALEN; i++) p[i] = s_local_mac[i];
    p += ETH_ALEN;
    put_be16(p, RUDP_ETHERTYPE); p += 2;

    /* RUDP header */
    *p++ = RUDP_MAGIC_BYTE;
    *p++ = flags;
    *p++ = cmd;
    *p++ = 0x00;  /* reserved */
    put_be32(p, seq);         p += 4;
    put_be16(p, payload_len); p += 2;
    put_be16(p, ack_seq);     p += 2;

    /* Payload */
    if (payload && payload_len > 0) {
        for (uint16_t i = 0; i < payload_len; i++) p[i] = payload[i];
        p += payload_len;
    }

    /* CRC-16 over everything so far */
    uint16_t total_so_far = (uint16_t)(p - s_tx_frame);
    uint16_t crc = CRC16_Compute(s_tx_frame, total_so_far);
    put_be16(p, crc); p += 2;

    return (uint16_t)(p - s_tx_frame);
}

/* ------------------------------------------------------------------ */
/*  Store a frame in the retransmit window                            */
/* ------------------------------------------------------------------ */

static void store_in_window(RudpSession   *session,
                             uint32_t       seq,
                             const uint8_t *frame,
                             uint16_t       len)
{
    uint8_t slot = (uint8_t)(seq % RUDP_WINDOW_SIZE);
    session->tx_buf_seq[slot]     = seq;
    session->tx_buf_len[slot]     = len;
    session->tx_buf_pending[slot] = true;
    for (uint16_t i = 0; i < len; i++) {
        session->tx_buf[slot][i] = frame[i];
    }
}

/* ------------------------------------------------------------------ */
/*  Public: Init / Shutdown                                           */
/* ------------------------------------------------------------------ */

Status RUDP_Init(const uint8_t local_mac[ETH_ALEN])
{
    for (int i = 0; i < ETH_ALEN; i++) s_local_mac[i] = local_mac[i];
    RUDP_ResetStats();
    s_callback = NULL;
    return ETH_Init(local_mac);
}

void RUDP_Shutdown(void)
{
    /* Nothing to free — all static allocation */
}

/* ------------------------------------------------------------------ */
/*  Public: Session management                                        */
/* ------------------------------------------------------------------ */

Status RUDP_OpenSession(RudpSession *session,
                        const uint8_t remote_mac[ETH_ALEN])
{
    if (!session) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Zero-init entire session struct */
    for (size_t i = 0; i < sizeof(RudpSession); i++) {
        ((uint8_t *)session)[i] = 0;
    }
    for (int i = 0; i < ETH_ALEN; i++) session->remote_mac[i] = remote_mac[i];
    session->tx_seq        = 1;
    session->rx_expected   = 1;
    session->active        = true;
    session->last_retry_ms = HAL_Timer_GetMs();

    /* Send KEEPALIVE to peer */
    uint16_t len = build_frame(remote_mac,
                               RUDP_FLAG_KEEPALIVE | RUDP_FLAG_RELIABLE,
                               NET_CMD_KEEPALIVE,
                               session->tx_seq++,
                               0,
                               NULL, 0);
    s_stats.keepalives_sent++;
    return ETH_Send(s_tx_frame, len);
}

void RUDP_CloseSession(RudpSession *session)
{
    if (!session || !session->active) return;

    build_frame(session->remote_mac,
                RUDP_FLAG_RESET,
                NET_CMD_RESET,
                session->tx_seq++,
                0, NULL, 0);
    ETH_Send(s_tx_frame, (uint16_t)(ETH_HDR_LEN + RUDP_HDR_LEN + 2));
    session->active = false;
    s_stats.session_resets++;
}

/* ------------------------------------------------------------------ */
/*  Public: Poll (call from cooperative scheduler loop)               */
/*                                                                    */
/*  FIX: Previously this function retransmitted pending frames on     */
/*  every single call, which could happen thousands of times per      */
/*  second in a tight loop. Now it checks the elapsed time since the  */
/*  last retransmit pass and only acts after RUDP_RETRY_TIMEOUT_MS.  */
/* ------------------------------------------------------------------ */

Status RUDP_Poll(RudpSession *session)
{
    if (!session || !session->active) return STATUS_ERROR_NOT_INITIALIZED;

    uint64_t now_ms  = HAL_Timer_GetMs();
    uint64_t elapsed = now_ms - session->last_retry_ms;

    /* Only retransmit if the timeout window has elapsed */
    if (elapsed < (uint64_t)RUDP_RETRY_TIMEOUT_MS) {
        return STATUS_OK;
    }

    /* Check if any slot is pending */
    bool any_pending = false;
    for (int slot = 0; slot < RUDP_WINDOW_SIZE; slot++) {
        if (session->tx_buf_pending[slot]) {
            any_pending = true;
            break;
        }
    }

    if (!any_pending) {
        /* No pending frames — reset timestamp to avoid drift */
        session->last_retry_ms = now_ms;
        return STATUS_OK;
    }

    /* At least one pending frame: increment retry counter */
    session->retry_count++;
    if (session->retry_count > RUDP_MAX_RETRIES) {
        HAL_UART_PutString("[RUDP] Session dead: max retries exceeded\r\n");
        session->active = false;
        s_stats.session_resets++;
        return STATUS_ERROR_TIMEOUT;
    }

    HAL_UART_PutString("[RUDP] Retransmit pass #");
    HAL_UART_PutDec(session->retry_count);
    HAL_UART_PutString("\r\n");

    /* Retransmit all pending slots */
    for (int slot = 0; slot < RUDP_WINDOW_SIZE; slot++) {
        if (!session->tx_buf_pending[slot]) continue;
        ETH_Send(session->tx_buf[slot], session->tx_buf_len[slot]);
        s_stats.retransmits++;
    }

    session->last_retry_ms = now_ms;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public: Send                                                      */
/* ------------------------------------------------------------------ */

Status RUDP_Send(RudpSession   *session,
                 NetCommand     cmd,
                 const uint8_t *payload,
                 uint16_t       payload_len)
{
    if (!session || !session->active) return STATUS_ERROR_NOT_INITIALIZED;
    if (payload_len > RUDP_MAX_PAYLOAD)  return STATUS_ERROR_INVALID_ARGUMENT;

    bool    reliable  = NET_CMD_IS_RELIABLE((uint8_t)cmd);
    uint8_t flags     = reliable ? RUDP_FLAG_RELIABLE : 0;
    uint32_t seq      = session->tx_seq++;

    uint16_t frame_len = build_frame(session->remote_mac,
                                     flags, (uint8_t)cmd,
                                     seq, 0,
                                     payload, payload_len);
    if (reliable) {
        store_in_window(session, seq, s_tx_frame, frame_len);
    }

    Status st = ETH_Send(s_tx_frame, frame_len);
    if (st != STATUS_OK) return st;

    s_stats.frames_sent++;
    return STATUS_OK;
}

Status RUDP_SendLarge(RudpSession   *session,
                      NetCommand     cmd,
                      const uint8_t *data,
                      size_t         total_len)
{
    if (!session || !data) return STATUS_ERROR_INVALID_ARGUMENT;

    size_t   offset   = 0;
    uint32_t frag_num = 0;

    while (offset < total_len) {
        size_t chunk = total_len - offset;
        bool   last  = (chunk <= (size_t)RUDP_MAX_PAYLOAD);
        if (!last) chunk = (size_t)RUDP_MAX_PAYLOAD;

        uint8_t flags     = RUDP_FLAG_RELIABLE |
                            (last ? RUDP_FLAG_FRAG_END : RUDP_FLAG_FRAG);
        uint8_t frame_cmd = (frag_num == 0) ? (uint8_t)cmd
                                            : (uint8_t)NET_CMD_FRAG_DATA;

        uint32_t seq      = session->tx_seq++;
        uint16_t frame_len = build_frame(session->remote_mac,
                                         flags, frame_cmd,
                                         seq, (uint16_t)frag_num,
                                         data + offset, (uint16_t)chunk);
        store_in_window(session, seq, s_tx_frame, frame_len);

        Status st = ETH_Send(s_tx_frame, frame_len);
        if (st != STATUS_OK) return st;

        s_stats.frames_sent++;
        s_stats.fragments_sent++;
        offset   += chunk;
        frag_num++;
    }

    return STATUS_OK;
}

Status RUDP_SendAck(RudpSession *session, uint32_t ack_seq)
{
    if (!session) return STATUS_ERROR_INVALID_ARGUMENT;

    uint16_t len = build_frame(session->remote_mac,
                               RUDP_FLAG_ACK,
                               NET_CMD_KEEPALIVE,       /* cmd unused for ACK */
                               session->tx_seq,         /* don't increment    */
                               (uint16_t)(ack_seq & 0xFFFFu),
                               NULL, 0);
    s_stats.frames_acked++;
    return ETH_Send(s_tx_frame, len);
}

/* ------------------------------------------------------------------ */
/*  Public: Receive                                                   */
/*                                                                    */
/*  FIX: Fragment reassembly now implemented.                         */
/*                                                                    */
/*  Previously, when a large ONNX model arrived as N fragments,       */
/*  the callback was called N times — once per fragment — each time  */
/*  with a partial payload. The application layer had no way to tell  */
/*  it had received an incomplete model.                              */
/*                                                                    */
/*  Now:                                                              */
/*    • On RUDP_FLAG_FRAG (not last): bytes are accumulated in        */
/*      session->defrag_buf. The callback is NOT called.             */
/*    • On RUDP_FLAG_FRAG_END (last): bytes are appended, then the   */
/*      callback is called ONCE with the complete reassembled buffer. */
/*    • Non-fragment frames (small single-frame payloads) behave      */
/*      exactly as before — callback called immediately.              */
/*                                                                    */
/*  Buffer overflow protection: if a model exceeds RUDP_DEFRAG_BUF_MAX
 *  the excess bytes are dropped and an error is logged. The transfer
 *  must be reattempted with a smaller model.                         */
/* ------------------------------------------------------------------ */

void RUDP_RegisterCallback(RudpRecvCallback cb)
{
    s_callback = cb;
}

Status RUDP_Receive(RudpSession *session)
{
    uint16_t rx_len = 0;
    Status st = ETH_Recv(s_rx_frame, &rx_len);
    if (st != STATUS_OK) return STATUS_ERROR_TIMEOUT;

    /* Minimum viable frame: ETH header + RUDP header + 2-byte CRC */
    if (rx_len < (uint16_t)(ETH_HDR_LEN + RUDP_HDR_LEN + 2)) {
        s_stats.crc_errors++;
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* Verify CRC-16 over the entire received frame */
    if (!CRC16_Verify(s_rx_frame, rx_len)) {
        s_stats.crc_errors++;
        return STATUS_ERROR_CRC_MISMATCH;
    }

    /* Check EtherType */
    uint16_t ethertype = get_be16(s_rx_frame + 12);
    if (ethertype != RUDP_ETHERTYPE) return STATUS_ERROR_NOT_SUPPORTED;

    /* Parse RUDP header (starts after 14-byte Ethernet header) */
    const uint8_t *rudp = s_rx_frame + ETH_HDR_LEN;
    if (rudp[0] != RUDP_MAGIC_BYTE) {
        s_stats.crc_errors++;
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    uint8_t        flags       = rudp[1];
    uint8_t        cmd         = rudp[2];
    uint32_t       seq         = get_be32(rudp + 4);
    uint16_t       payload_len = get_be16(rudp + 8);
    uint16_t       ack_seq_rx  = get_be16(rudp + 10);
    const uint8_t *payload     = rudp + RUDP_HDR_LEN;

    s_stats.frames_recv++;

    /* ---- ACK: clear the matching retransmit window slot ---- */
    if (flags & RUDP_FLAG_ACK) {
        if (session) {
            uint8_t slot = (uint8_t)(ack_seq_rx % RUDP_WINDOW_SIZE);
            if (session->tx_buf_pending[slot] &&
                session->tx_buf_seq[slot] == (uint32_t)ack_seq_rx) {
                session->tx_buf_pending[slot] = false;
                session->retry_count = 0;
            }
        }
        return STATUS_OK;
    }

    /* ---- NACK: immediately retransmit the requested sequence ---- */
    if (flags & RUDP_FLAG_NACK) {
        if (session) {
            uint8_t slot = (uint8_t)(ack_seq_rx % RUDP_WINDOW_SIZE);
            if (session->tx_buf_pending[slot]) {
                ETH_Send(session->tx_buf[slot], session->tx_buf_len[slot]);
                s_stats.retransmits++;
            }
        }
        return STATUS_OK;
    }

    /* ---- KEEPALIVE: echo an ACK back ---- */
    if (flags & RUDP_FLAG_KEEPALIVE) {
        if (session) RUDP_SendAck(session, seq);
        return STATUS_OK;
    }

    /* ---- Sequence check for reliable frames ---- */
    if ((flags & RUDP_FLAG_RELIABLE) && session) {
        if (seq != session->rx_expected) {
            s_stats.seq_errors++;
            /* Send NACK asking for the expected sequence */
            uint16_t len = build_frame(session->remote_mac,
                                       RUDP_FLAG_NACK, cmd,
                                       session->tx_seq,
                                       (uint16_t)(session->rx_expected & 0xFFFFu),
                                       NULL, 0);
            ETH_Send(s_tx_frame, len);
            return STATUS_ERROR_INVALID_ARGUMENT;
        }
        session->rx_expected++;
        RUDP_SendAck(session, seq);
    }

    /* ---- Fragment handling (reassembly) ---- */
    bool is_frag     = (flags & RUDP_FLAG_FRAG)     != 0;
    bool is_frag_end = (flags & RUDP_FLAG_FRAG_END) != 0;

    if (is_frag || is_frag_end) {
        s_stats.fragments_recv++;

        if (!session) {
            /* No session context — cannot reassemble */
            return STATUS_ERROR_NOT_INITIALIZED;
        }

        if (is_frag && !is_frag_end) {
            /*
             * Intermediate fragment — accumulate bytes, do NOT fire callback.
             * On the very first fragment (defrag_active == false), record the
             * original command code so we can pass it to the callback later.
             */
            if (!session->defrag_active) {
                session->defrag_active = true;
                session->defrag_cmd    = cmd;   /* NET_CMD_LOAD_MODEL etc. */
                session->defrag_len    = 0;
            }

            uint32_t space = RUDP_DEFRAG_BUF_MAX - session->defrag_len;
            uint16_t copy  = payload_len;
            if ((uint32_t)copy > space) {
                copy = (uint16_t)space;
                HAL_UART_PutString("[RUDP] WARNING: defrag buffer full, bytes truncated\r\n");
            }
            for (uint16_t i = 0; i < copy; i++) {
                session->defrag_buf[session->defrag_len + i] = payload[i];
            }
            session->defrag_len += copy;
            return STATUS_OK;  /* callback NOT called yet */
        }

        if (is_frag_end) {
            /*
             * Final fragment — append last bytes and fire callback once
             * with the complete reassembled payload.
             */
            if (!session->defrag_active) {
                /*
                 * FRAG_END without a preceding FRAG means this is a
                 * single-fragment "large" send (rare but valid). Treat
                 * the payload as the entire message.
                 */
                session->defrag_cmd = cmd;
                session->defrag_len = 0;
            }

            uint32_t space = RUDP_DEFRAG_BUF_MAX - session->defrag_len;
            uint16_t copy  = payload_len;
            if ((uint32_t)copy > space) {
                copy = (uint16_t)space;
                HAL_UART_PutString("[RUDP] WARNING: defrag buffer full on FRAG_END, bytes truncated\r\n");
            }
            for (uint16_t i = 0; i < copy; i++) {
                session->defrag_buf[session->defrag_len + i] = payload[i];
            }
            session->defrag_len += copy;

            HAL_UART_PutString("[RUDP] Reassembly complete: ");
            HAL_UART_PutDec(session->defrag_len);
            HAL_UART_PutString(" bytes\r\n");

            /* Fire the callback with the full reassembled buffer */
            if (s_callback) {
                s_callback((NetCommand)session->defrag_cmd,
                           session->defrag_buf,
                           (uint16_t)(session->defrag_len > 0xFFFF
                                      ? 0xFFFF : session->defrag_len));
            }

            /* Reset defrag state for next message */
            session->defrag_active = false;
            session->defrag_len    = 0;
            session->defrag_cmd    = 0;
            return STATUS_OK;
        }
    }

    /* ---- Normal (non-fragment) frame: deliver directly ---- */
    if (s_callback) {
        s_callback((NetCommand)cmd, payload, payload_len);
    }

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public: Statistics                                                */
/* ------------------------------------------------------------------ */

void RUDP_GetStats(RudpStats *stats)
{
    if (!stats) return;
    *stats = s_stats;
}

void RUDP_ResetStats(void)
{
    uint8_t *p = (uint8_t *)&s_stats;
    for (size_t i = 0; i < sizeof(RudpStats); i++) p[i] = 0;
}

void RUDP_PrintStats(void)
{
    HAL_UART_PutString("\r\n=== RUDP Statistics ===\r\n");
    HAL_UART_PutString("frames_sent:    "); HAL_UART_PutDec(s_stats.frames_sent);    HAL_UART_PutString("\r\n");
    HAL_UART_PutString("frames_recv:    "); HAL_UART_PutDec(s_stats.frames_recv);    HAL_UART_PutString("\r\n");
    HAL_UART_PutString("frames_acked:   "); HAL_UART_PutDec(s_stats.frames_acked);   HAL_UART_PutString("\r\n");
    HAL_UART_PutString("retransmits:    "); HAL_UART_PutDec(s_stats.retransmits);    HAL_UART_PutString("\r\n");
    HAL_UART_PutString("crc_errors:     "); HAL_UART_PutDec(s_stats.crc_errors);     HAL_UART_PutString("\r\n");
    HAL_UART_PutString("seq_errors:     "); HAL_UART_PutDec(s_stats.seq_errors);     HAL_UART_PutString("\r\n");
    HAL_UART_PutString("frags_sent:     "); HAL_UART_PutDec(s_stats.fragments_sent); HAL_UART_PutString("\r\n");
    HAL_UART_PutString("frags_recv:     "); HAL_UART_PutDec(s_stats.fragments_recv); HAL_UART_PutString("\r\n");
    HAL_UART_PutString("keepalives:     "); HAL_UART_PutDec(s_stats.keepalives_sent);HAL_UART_PutString("\r\n");
    HAL_UART_PutString("sess_resets:    "); HAL_UART_PutDec(s_stats.session_resets); HAL_UART_PutString("\r\n");
    HAL_UART_PutString("=======================\r\n");
}
