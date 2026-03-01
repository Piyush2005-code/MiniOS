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
 *   │  Frag 0 → [FRAG flag, seq=N]   → ACK                      │
 *   │  Frag 1 → [FRAG flag, seq=N+1] → ACK                      │
 *   │  Last   → [FRAG_END, seq=N+k]  → ACK                      │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * All frame indices are big-endian on the wire.
 * Cooperative-scheduler friendly: no blocking waits; callers must
 * call RUDP_Poll() and RUDP_Receive() from the main loop.
 */

#include "../include/net/rudp.h"
#include "../include/net/eth_driver.h"
#include "../include/net/crc16.h"
/* Reuse HAL from MiniOS-BootLoader_and_HAL */
#include "../../MiniOS-BootLoader_and_HAL/include/hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Internal state (no dynamic allocation — DC-002)                   */
/* ------------------------------------------------------------------ */

static uint8_t         s_local_mac[ETH_ALEN];
static RudpStats       s_stats;
static RudpRecvCallback s_callback = NULL;

/* Scratch buffers for Tx/Rx — static allocation                      */
static uint8_t s_tx_frame[ETH_FRAME_MAX + 2];  /* +2 for CRC16       */
static uint8_t s_rx_frame[ETH_FRAME_MAX + 2];

/* ------------------------------------------------------------------ */
/*  Utility: big-endian 32-bit encode/decode                          */
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
    put_be32(p, seq);    p += 4;
    put_be16(p, payload_len); p += 2;
    put_be16(p, ack_seq); p += 2;

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
static void store_in_window(RudpSession *session,
                             uint32_t     seq,
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

    /* Zero-init session */
    for (size_t i = 0; i < sizeof(RudpSession); i++) {
        ((uint8_t*)session)[i] = 0;
    }
    for (int i = 0; i < ETH_ALEN; i++) session->remote_mac[i] = remote_mac[i];
    session->tx_seq      = 1;
    session->rx_expected = 1;
    session->active      = true;

    /* Send KEEPALIVE and wait for response */
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
    ETH_Send(s_tx_frame,
             ETH_HDR_LEN + RUDP_HDR_LEN + 2); /* header only + CRC   */
    session->active = false;
    s_stats.session_resets++;
}

/* ------------------------------------------------------------------ */
/*  Public: Poll (call from cooperative scheduler loop)               */
/* ------------------------------------------------------------------ */

Status RUDP_Poll(RudpSession *session)
{
    if (!session || !session->active) return STATUS_ERROR_NOT_INITIALIZED;

    /* Retransmit any pending unACKed frames */
    for (int slot = 0; slot < RUDP_WINDOW_SIZE; slot++) {
        if (!session->tx_buf_pending[slot]) continue;

        session->retry_count++;
        if (session->retry_count > RUDP_MAX_RETRIES) {
            session->active = false;
            s_stats.session_resets++;
            return STATUS_ERROR_TIMEOUT;
        }

        ETH_Send(session->tx_buf[slot], session->tx_buf_len[slot]);
        s_stats.retransmits++;
    }

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public: Send                                                      */
/* ------------------------------------------------------------------ */

Status RUDP_Send(RudpSession *session,
                 NetCommand   cmd,
                 const uint8_t *payload,
                 uint16_t     payload_len)
{
    if (!session || !session->active) return STATUS_ERROR_NOT_INITIALIZED;
    if (payload_len > RUDP_MAX_PAYLOAD) return STATUS_ERROR_INVALID_ARGUMENT;

    bool reliable = NET_CMD_IS_RELIABLE((uint8_t)cmd);
    uint8_t flags = reliable ? RUDP_FLAG_RELIABLE : 0;

    uint32_t seq = session->tx_seq++;
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

Status RUDP_SendLarge(RudpSession *session,
                      NetCommand   cmd,
                      const uint8_t *data,
                      size_t       total_len)
{
    if (!session || !data) return STATUS_ERROR_INVALID_ARGUMENT;

    size_t   offset    = 0;
    uint32_t frag_num  = 0;

    while (offset < total_len) {
        size_t   chunk = total_len - offset;
        bool     last  = (chunk <= RUDP_MAX_PAYLOAD);
        if (!last) chunk = RUDP_MAX_PAYLOAD;

        uint8_t flags = RUDP_FLAG_RELIABLE |
                        (last ? RUDP_FLAG_FRAG_END : RUDP_FLAG_FRAG);

        /* Use NET_CMD_FRAG_DATA for continuation frames, original cmd for first */
        uint8_t frame_cmd = (frag_num == 0) ? (uint8_t)cmd
                                            : (uint8_t)NET_CMD_FRAG_DATA;

        uint32_t seq = session->tx_seq++;
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
                               NET_CMD_KEEPALIVE,  /* cmd unused for ACK */
                               session->tx_seq,    /* don't increment    */
                               (uint16_t)(ack_seq & 0xFFFF),
                               NULL, 0);
    s_stats.frames_acked++;
    return ETH_Send(s_tx_frame, len);
}

/* ------------------------------------------------------------------ */
/*  Public: Receive                                                   */
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

    /* Minimum viable frame: ETH + RUDP header + CRC16 */
    if (rx_len < (uint16_t)(ETH_HDR_LEN + RUDP_HDR_LEN + 2)) {
        s_stats.crc_errors++;
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* Verify CRC-16 */
    if (!CRC16_Verify(s_rx_frame, rx_len)) {
        s_stats.crc_errors++;
        return STATUS_ERROR_CRC_MISMATCH;
    }

    /* Check EtherType */
    uint16_t ethertype = get_be16(s_rx_frame + 12);
    if (ethertype != RUDP_ETHERTYPE) return STATUS_ERROR_NOT_SUPPORTED;

    /* Parse RUDP header */
    const uint8_t *rudp = s_rx_frame + ETH_HDR_LEN;
    if (rudp[0] != RUDP_MAGIC_BYTE) {
        s_stats.crc_errors++;
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    uint8_t  flags       = rudp[1];
    uint8_t  cmd         = rudp[2];
    uint32_t seq         = get_be32(rudp + 4);
    uint16_t payload_len = get_be16(rudp + 8);
    uint16_t ack_seq_rx  = get_be16(rudp + 10);

    const uint8_t *payload = rudp + RUDP_HDR_LEN;

    s_stats.frames_recv++;

    /* Handle ACK: clear the window slot */
    if (flags & RUDP_FLAG_ACK) {
        uint8_t slot = (uint8_t)(ack_seq_rx % RUDP_WINDOW_SIZE);
        if (session && session->tx_buf_pending[slot] &&
            session->tx_buf_seq[slot] == (uint32_t)ack_seq_rx) {
            session->tx_buf_pending[slot] = false;
            session->retry_count = 0;
        }
        return STATUS_OK;
    }

    /* Handle NACK: schedule retransmit */
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

    /* Handle KEEPALIVE: send ACK back */
    if (flags & RUDP_FLAG_KEEPALIVE) {
        if (session) RUDP_SendAck(session, seq);
        return STATUS_OK;
    }

    /* Sequence check for reliable frames */
    if ((flags & RUDP_FLAG_RELIABLE) && session) {
        if (seq != session->rx_expected) {
            s_stats.seq_errors++;
            /* Send NACK requesting expected */
            uint16_t len = build_frame(session->remote_mac,
                                       RUDP_FLAG_NACK, cmd,
                                       session->tx_seq,
                                       (uint16_t)(session->rx_expected & 0xFFFF),
                                       NULL, 0);
            ETH_Send(s_tx_frame, len);
            return STATUS_ERROR_INVALID_ARGUMENT;
        }
        session->rx_expected++;

        /* Send ACK */
        RUDP_SendAck(session, seq);
    }

    /* Fragment reassembly tracking (simple: deliver each fragment) */
    if (flags & RUDP_FLAG_FRAG) s_stats.fragments_recv++;

    /* Deliver to callback */
    if (s_callback && !(flags & RUDP_FLAG_ACK)) {
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
    uint8_t *p = (uint8_t*)&s_stats;
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
    HAL_UART_PutString("=======================\r\n");
}