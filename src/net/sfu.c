/**
 * @file sfu.c
 * @brief Simple Framed UDP (SFU) framing layer implementation
 *
 * Implements the SFU wire protocol over UDP port 9000. Packets are
 * validated (magic, version, msg_type, CRC16-CCITT checksum) on
 * receive and dispatched to type-specific handlers.
 *
 * TX path uses a single static staging buffer; no dynamic allocation.
 *
 * CRC16-CCITT details:
 *   Polynomial : 0x1021
 *   Initial val: 0xFFFF
 *   Input order: MSB first within each byte
 *   Final XOR  : none
 *
 * @note Per SRS FR-NET-SFU
 *
 * @complexity
 *   SFU_Checksum   : O(len) — byte-by-byte CRC
 *   SFU_Serialize  : O(payload_len) — memcpy
 *   SFU_Deserialize: O(payload_len) — validate + CRC
 *   SFU_OnReceive  : O(payload_len) — deserialize + dispatch
 */

#include "net/sfu.h"
#include "net/udp.h"
#include "net/arp.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Compile-time layout guard                                        */
/* ------------------------------------------------------------------ */

_Static_assert(sizeof(sfu_header_t) == 24u,
               "sfu_header_t must be exactly 24 bytes — check padding");

/* ------------------------------------------------------------------ */
/*  Module-private state                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Static TX staging buffer
 *
 * Holds one outgoing SFU packet (24-byte header + up to 1448 bytes
 * of payload) before handing it to UDP_Send(). Reused for every TX.
 */
static uint8_t sfu_tx_buf[SFU_MAX_PACKET];

/**
 * @brief Monotonically increasing request ID counter
 *
 * Incremented for each outgoing PING or INFER_REQUEST. Replies from
 * the peer echo the req_id they received — we do NOT increment here.
 */
static uint32_t sfu_req_id_counter;

/**
 * @brief In-flight reliable requests table
 */
typedef struct {
    uint8_t  in_use;
    uint32_t req_id;
    uint64_t sent_at_ms;           // from HAL_Timer or tick counter
    uint8_t  retries;
    uint8_t  buf[SFU_MAX_PACKET];  // full serialized packet copy
    uint16_t buf_len;
    uint32_t dst_ip;
    uint16_t dst_port;
} sfu_inflight_t;

static sfu_inflight_t sfu_inflight[SFU_MAX_INFLIGHT];

static void (*sfu_timeout_cb)(uint32_t req_id) = (void(*)(uint32_t))0;

/* ------------------------------------------------------------------ */
/*  Internal: Timing helpers                                         */
/* ------------------------------------------------------------------ */

static uint64_t SFU_GetMs(void)
{
    /* Convert system ticks to milliseconds */
    return HAL_Timer_GetSystemTicks() * (uint64_t)HAL_Timer_GetTickPeriodMs();
}

/* ------------------------------------------------------------------ */
/*  Internal: UART helpers                                           */
/* ------------------------------------------------------------------ */

/** Print a 32-bit value as 0x........ to UART */
static void sfu_put_hex32(uint32_t v)
{
    HAL_UART_PutString("0x");
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        int n = (int)((v >> (uint32_t)shift) & 0xFu);
        HAL_UART_PutChar((char)(n < 10 ? '0' + n : 'a' + n - 10));
    }
}

/** Print a 16-bit value as 0x.... to UART */
static void sfu_put_hex16(uint16_t v)
{
    HAL_UART_PutString("0x");
    for (int8_t shift = 12; shift >= 0; shift -= 4) {
        int n = (int)((v >> (uint32_t)shift) & 0xFu);
        HAL_UART_PutChar((char)(n < 10 ? '0' + n : 'a' + n - 10));
    }
}

/* ------------------------------------------------------------------ */
/*  Internal: memcpy substitute                                      */
/* ------------------------------------------------------------------ */

/** Byte-by-byte copy (avoids libc dependency in freestanding build) */
static void sfu_memcpy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/* ------------------------------------------------------------------ */
/*  Internal: msg_type validation helper                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Return 1 if msg_type is a known SFU message type, else 0
 */
static int sfu_is_known_type(uint8_t msg_type)
{
    switch (msg_type) {
        case SFU_MSG_INFER_REQUEST:
        case SFU_MSG_INFER_RESPONSE:
        case SFU_MSG_ACK:
        case SFU_MSG_NACK:
        case SFU_MSG_PING:
        case SFU_MSG_PONG:
        case SFU_MSG_ERROR:
            return 1;
        default:
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  SFU_Checksum  (CRC16-CCITT)                                     */
/* ------------------------------------------------------------------ */

uint16_t SFU_Checksum(uint8_t *payload, uint16_t len)
{
    if (payload == (uint8_t *)0 || len == 0u) {
        return 0u;
    }

    uint16_t crc = 0xFFFFu;

    for (uint16_t i = 0; i < len; i++) {
        /*
         * Process each byte MSB-first using CRC16-CCITT poly 0x1021.
         * Each byte produces 8 iterations; no lookup table needed at
         * our packet sizes (max 1448 bytes ≈ 11584 iterations).
         */
        crc ^= (uint16_t)((uint16_t)payload[i] << 8);
        for (uint8_t bit = 0; bit < 8u; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

/* ------------------------------------------------------------------ */
/*  SFU_Validate                                                     */
/* ------------------------------------------------------------------ */

int SFU_Validate(sfu_header_t *hdr)
{
    if (hdr->magic   != SFU_MAGIC)   { return -1; }
    if (hdr->version != SFU_VERSION) { return -1; }
    if (!sfu_is_known_type(hdr->msg_type)) { return -1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SFU_Serialize                                                    */
/* ------------------------------------------------------------------ */

int SFU_Serialize(sfu_header_t *hdr, uint8_t *payload,
                  uint8_t *out_buf, uint16_t *out_len)
{
    uint16_t plen = hdr->payload_len;

    if (plen > SFU_MAX_PAYLOAD) {
        HAL_UART_PutString("[SFU ] ERROR: Serialize: payload too large\n");
        return -1;
    }

    /* Compute and fill checksum (over payload bytes only) */
    hdr->checksum = SFU_Checksum(payload, plen);

    /* Copy header into output buffer */
    sfu_memcpy(out_buf, (const uint8_t *)hdr, SFU_HEADER_SIZE);

    /* Copy payload immediately after the header */
    if (plen > 0u && payload != (uint8_t *)0) {
        sfu_memcpy(out_buf + SFU_HEADER_SIZE, payload, plen);
    }

    *out_len = (uint16_t)(SFU_HEADER_SIZE + plen);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SFU_Deserialize                                                  */
/* ------------------------------------------------------------------ */

int SFU_Deserialize(uint8_t *buf, uint16_t len,
                    sfu_header_t *hdr_out,
                    uint8_t **payload_out,
                    uint16_t *payload_len_out)
{
    /* Need at least a full header */
    if (len < SFU_HEADER_SIZE) {
        return -1;
    }

    /* Copy header fields without aliasing the raw buffer */
    sfu_memcpy((uint8_t *)hdr_out, buf, SFU_HEADER_SIZE);

    /* Validate magic, version, msg_type */
    if (SFU_Validate(hdr_out) != 0) {
        return -1;
    }

    /* Bounds check: declared payload_len must fit within received buffer */
    uint16_t plen = hdr_out->payload_len;
    if ((uint32_t)SFU_HEADER_SIZE + plen > (uint32_t)len) {
        /* Truncated packet */
        return -1;
    }

    /* Verify CRC16 over the payload region */
    uint8_t *payload_ptr = buf + SFU_HEADER_SIZE;
    uint16_t computed_crc = SFU_Checksum(payload_ptr, plen);
    if (computed_crc != hdr_out->checksum) {
        HAL_UART_PutString("[SFU ] checksum mismatch: got ");
        sfu_put_hex16(hdr_out->checksum);
        HAL_UART_PutString(" expected ");
        sfu_put_hex16(computed_crc);
        HAL_UART_PutString("\n");
        return -2;
    }

    *payload_out     = payload_ptr;
    *payload_len_out = plen;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SFU_SendRaw                                                      */
/* ------------------------------------------------------------------ */

int SFU_SendRaw(uint32_t dst_ip, uint16_t dst_port,
                uint8_t msg_type, uint32_t req_id,
                uint8_t *payload, uint16_t payload_len)
{
    sfu_header_t hdr;

    hdr.magic       = SFU_MAGIC;
    hdr.version     = SFU_VERSION;
    hdr.msg_type    = msg_type;
    hdr.flags       = 0u;
    hdr.req_id      = req_id;
    hdr.seq_num     = 0u;
    hdr.total_seq   = 1u;
    hdr.checksum    = 0u;   /* filled by SFU_Serialize */
    hdr.payload_len = payload_len;

    uint16_t out_len = 0u;
    if (SFU_Serialize(&hdr, payload, sfu_tx_buf, &out_len) != 0) {
        return -1;
    }

    return UDP_Send(dst_ip, dst_port, SFU_PORT, sfu_tx_buf, out_len);
}

/* ------------------------------------------------------------------ */
/*  SFU_SendPing                                                     */
/* ------------------------------------------------------------------ */

int SFU_SendPing(uint32_t dst_ip, uint16_t dst_port)
{
    uint32_t id = sfu_req_id_counter++;
    HAL_UART_PutString("[SFU ] PING → req_id=");
    sfu_put_hex32(id);
    HAL_UART_PutString("\n");
    return SFU_SendRaw(dst_ip, dst_port, SFU_MSG_PING, id,
                       (uint8_t *)0, 0u);
}

/* ------------------------------------------------------------------ */
/*  SFU_SendPong                                                     */
/* ------------------------------------------------------------------ */

void SFU_SendPong(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id)
{
    HAL_UART_PutString("[SFU ] PONG → req_id=");
    sfu_put_hex32(req_id);
    HAL_UART_PutString("\n");
    SFU_SendRaw(dst_ip, dst_port, SFU_MSG_PONG, req_id,
                (uint8_t *)0, 0u);
}

/* ------------------------------------------------------------------ */
/*  SFU_SendNack                                                     */
/* ------------------------------------------------------------------ */

void SFU_SendNack(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id)
{
    HAL_UART_PutString("[SFU ] NACK → req_id=");
    sfu_put_hex32(req_id);
    HAL_UART_PutString("\n");
    SFU_SendRaw(dst_ip, dst_port, SFU_MSG_NACK, req_id,
                (uint8_t *)0, 0u);
}

/* ------------------------------------------------------------------ */
/*  SFU_OnInferRequest (stub — Phase 5)                              */
/* ------------------------------------------------------------------ */

void SFU_OnInferRequest(uint32_t src_ip, uint16_t src_port,
                        sfu_header_t *hdr,
                        uint8_t *payload, uint16_t len)
{
    (void)payload;
    (void)len;

    HAL_UART_PutString("[SFU ] INFER_REQUEST received (req_id=");
    sfu_put_hex32(hdr->req_id);
    HAL_UART_PutString(") — inference engine not yet connected\n");

    /* Reply with NACK until inference integration is complete */
    SFU_SendNack(src_ip, src_port, hdr->req_id);
}

/* ------------------------------------------------------------------ */
/*  SFU_OnReceive                                                    */
/* ------------------------------------------------------------------ */

void SFU_OnReceive(uint32_t src_ip, uint16_t src_port,
                   uint8_t *buf, uint16_t len)
{
    sfu_header_t hdr;
    uint8_t     *payload     = (uint8_t *)0;
    uint16_t     payload_len = 0u;

    int rc = SFU_Deserialize(buf, len, &hdr, &payload, &payload_len);
    if (rc == -1) {
        HAL_UART_PutString("[SFU ] RX: invalid packet (bad magic/version/type) — NACK\n");
        SFU_SendNack(src_ip, src_port, 0u);
        return;
    }
    if (rc == -2) {
        HAL_UART_PutString("[SFU ] RX: checksum mismatch — NACK\n");
        SFU_SendNack(src_ip, src_port, hdr.req_id);
        return;
    }

    HAL_UART_PutString("[SFU ] RX msg_type=");
    sfu_put_hex16(hdr.msg_type);
    HAL_UART_PutString(" req_id=");
    sfu_put_hex32(hdr.req_id);
    HAL_UART_PutString(" plen=");
    HAL_UART_PutDec(payload_len);
    HAL_UART_PutString("\n");

    switch (hdr.msg_type) {

        case SFU_MSG_PING:
            SFU_SendPong(src_ip, src_port, hdr.req_id);
            break;

        case SFU_MSG_INFER_REQUEST:
            SFU_OnInferRequest(src_ip, src_port, &hdr, payload, payload_len);
            break;

        case SFU_MSG_ACK:
            SFU_OnAck(hdr.req_id);
            break;

        case SFU_MSG_NACK:
            SFU_OnNack(hdr.req_id, src_ip, src_port);
            break;

        default:
            /* Unknown type already rejected by SFU_Deserialize — unreachable */
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Reliability API                                                  */
/* ------------------------------------------------------------------ */

int SFU_SendReliable(uint32_t dst_ip, uint16_t dst_port,
                     uint8_t msg_type,
                     uint8_t *payload, uint16_t payload_len,
                     uint32_t *out_req_id)
{
    int slot = -1;
    for (int i = 0; i < SFU_MAX_INFLIGHT; i++) {
        if (sfu_inflight[i].in_use == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        HAL_UART_PutString("[SFU ] SendReliable ERROR: in-flight table full\n");
        return -1;
    }

    uint32_t req_id = sfu_req_id_counter++;

    sfu_header_t hdr;
    hdr.magic       = SFU_MAGIC;
    hdr.version     = SFU_VERSION;
    hdr.msg_type    = msg_type;
    hdr.flags       = 0u;
    hdr.req_id      = req_id;
    hdr.seq_num     = 0u;
    hdr.total_seq   = 1u;
    hdr.checksum    = 0u;
    hdr.payload_len = payload_len;

    uint16_t out_len = 0u;
    if (SFU_Serialize(&hdr, payload, sfu_inflight[slot].buf, &out_len) != 0) {
        return -1;
    }

    /* Transmit immediately */
    UDP_Send(dst_ip, dst_port, SFU_PORT, sfu_inflight[slot].buf, out_len);

    sfu_inflight[slot].in_use     = 1u;
    sfu_inflight[slot].req_id     = req_id;
    sfu_inflight[slot].sent_at_ms = SFU_GetMs();
    sfu_inflight[slot].retries    = 0u;
    sfu_inflight[slot].dst_ip     = dst_ip;
    sfu_inflight[slot].dst_port   = dst_port;
    sfu_inflight[slot].buf_len    = out_len;

    *out_req_id = req_id;
    return 0;
}

void SFU_OnAck(uint32_t req_id)
{
    for (int i = 0; i < SFU_MAX_INFLIGHT; i++) {
        if (sfu_inflight[i].in_use && sfu_inflight[i].req_id == req_id) {
            sfu_inflight[i].in_use = 0;
            /* Optional debugging print */
            /*
            HAL_UART_PutString("[SFU ] req ");
            HAL_UART_PutDec(req_id);
            HAL_UART_PutString(" acked\n");
            */
            return;
        }
    }
}

void SFU_OnNack(uint32_t req_id, uint32_t dst_ip, uint16_t dst_port)
{
    for (int i = 0; i < SFU_MAX_INFLIGHT; i++) {
        if (sfu_inflight[i].in_use && sfu_inflight[i].req_id == req_id) {
            if (sfu_inflight[i].retries < SFU_MAX_RETRIES) {
                /* Retransmit immediately */
                UDP_Send(dst_ip, dst_port, SFU_PORT, sfu_inflight[i].buf, sfu_inflight[i].buf_len);
                sfu_inflight[i].retries++;
                sfu_inflight[i].sent_at_ms = SFU_GetMs();
            } else {
                /* Retry limit reached */
                SFU_OnTimeout(req_id);
                sfu_inflight[i].in_use = 0;
            }
            return;
        }
    }
}

void SFU_Tick(void)
{
    uint64_t now = SFU_GetMs();

    for (int i = 0; i < SFU_MAX_INFLIGHT; i++) {
        if (sfu_inflight[i].in_use) {
            uint64_t elapsed = now - sfu_inflight[i].sent_at_ms;
            
            if (elapsed >= SFU_TIMEOUT_MS) {
                if (sfu_inflight[i].retries < SFU_MAX_RETRIES) {
                    UDP_Send(sfu_inflight[i].dst_ip, sfu_inflight[i].dst_port, SFU_PORT, 
                             sfu_inflight[i].buf, sfu_inflight[i].buf_len);
                    sfu_inflight[i].retries++;
                    sfu_inflight[i].sent_at_ms = now;
                } else {
                    SFU_OnTimeout(sfu_inflight[i].req_id);
                    sfu_inflight[i].in_use = 0;
                }
            }
        }
    }
}

void SFU_OnTimeout(uint32_t req_id)
{
    HAL_UART_PutString("[SFU ] req ");
    HAL_UART_PutDec(req_id);
    HAL_UART_PutString(" timed out after ");
    HAL_UART_PutDec(SFU_MAX_RETRIES);
    HAL_UART_PutString(" retries\n");

    if (sfu_timeout_cb) {
        sfu_timeout_cb(req_id);
    }
}

void SFU_SetTimeoutCallback(void (*cb)(uint32_t req_id))
{
    sfu_timeout_cb = cb;
}

/* ------------------------------------------------------------------ */
/*  SFU_SelfTest                                                     */
/* ------------------------------------------------------------------ */

void SFU_SelfTest(void)
{
    HAL_UART_PutString("[SFU ] running self-test...\n");

    /* ── Build reference data ─────────────────────────────────────── */
    static uint8_t test_payload[4] = {0x01u, 0x02u, 0x03u, 0x04u};

    sfu_header_t orig;
    orig.magic       = SFU_MAGIC;
    orig.version     = SFU_VERSION;
    orig.msg_type    = SFU_MSG_PING;
    orig.flags       = 0u;
    orig.req_id      = 0xDEADBEEFu;
    orig.seq_num     = 0u;
    orig.total_seq   = 1u;
    orig.checksum    = 0u;          /* filled by Serialize */
    orig.payload_len = 4u;

    /* ── Serialize ────────────────────────────────────────────────── */
    static uint8_t st_buf[SFU_MAX_PACKET];
    uint16_t out_len = 0u;

    if (SFU_Serialize(&orig, test_payload, st_buf, &out_len) != 0) {
        HAL_UART_PutString("[SFU ] selftest FAILED at Serialize\n");
        return;
    }

    if (out_len != (uint16_t)(SFU_HEADER_SIZE + 4u)) {
        HAL_UART_PutString("[SFU ] selftest FAILED at out_len\n");
        return;
    }

    /* ── Deserialize ──────────────────────────────────────────────── */
    sfu_header_t parsed;
    uint8_t  *pld_ptr  = (uint8_t *)0;
    uint16_t  pld_len  = 0u;

    int rc = SFU_Deserialize(st_buf, out_len, &parsed, &pld_ptr, &pld_len);
    if (rc != 0) {
        HAL_UART_PutString("[SFU ] selftest FAILED at Deserialize (rc=");
        HAL_UART_PutDec((uint32_t)(rc < 0 ? (uint32_t)(-rc) : (uint32_t)rc));
        HAL_UART_PutString(")\n");
        return;
    }

    /* ── Verify header fields ─────────────────────────────────────── */
    if (parsed.magic != SFU_MAGIC) {
        HAL_UART_PutString("[SFU ] selftest FAILED at magic\n"); return;
    }
    if (parsed.version != SFU_VERSION) {
        HAL_UART_PutString("[SFU ] selftest FAILED at version\n"); return;
    }
    if (parsed.msg_type != SFU_MSG_PING) {
        HAL_UART_PutString("[SFU ] selftest FAILED at msg_type\n"); return;
    }
    if (parsed.flags != 0u) {
        HAL_UART_PutString("[SFU ] selftest FAILED at flags\n"); return;
    }
    if (parsed.req_id != 0xDEADBEEFu) {
        HAL_UART_PutString("[SFU ] selftest FAILED at req_id\n"); return;
    }
    if (parsed.seq_num != 0u) {
        HAL_UART_PutString("[SFU ] selftest FAILED at seq_num\n"); return;
    }
    if (parsed.total_seq != 1u) {
        HAL_UART_PutString("[SFU ] selftest FAILED at total_seq\n"); return;
    }
    if (parsed.payload_len != 4u) {
        HAL_UART_PutString("[SFU ] selftest FAILED at payload_len\n"); return;
    }

    /* Checksum must match the independently computed value */
    uint16_t expected_crc = SFU_Checksum(test_payload, 4u);
    if (parsed.checksum != expected_crc) {
        HAL_UART_PutString("[SFU ] selftest FAILED at checksum\n"); return;
    }

    /* ── Verify payload bytes ─────────────────────────────────────── */
    if (pld_len != 4u) {
        HAL_UART_PutString("[SFU ] selftest FAILED at pld_len\n"); return;
    }
    for (uint16_t i = 0u; i < 4u; i++) {
        if (pld_ptr[i] != test_payload[i]) {
            HAL_UART_PutString("[SFU ] selftest FAILED at payload[");
            HAL_UART_PutDec(i);
            HAL_UART_PutString("]\n");
            return;
        }
    }

    /* ── Verify that a tampered packet produces a checksum error ──── */
    uint8_t st_tampered[SFU_MAX_PACKET];
    sfu_memcpy(st_tampered, st_buf, out_len);
    st_tampered[SFU_HEADER_SIZE] ^= 0xFFu;   /* corrupt first payload byte */

    sfu_header_t tampered_hdr;
    uint8_t *tp = (uint8_t *)0; uint16_t tl = 0u;
    int trc = SFU_Deserialize(st_tampered, out_len, &tampered_hdr, &tp, &tl);
    if (trc != -2) {
        HAL_UART_PutString("[SFU ] selftest FAILED: tampered packet not caught\n");
        return;
    }

    HAL_UART_PutString("[SFU ] selftest PASSED\n");
}

/* ------------------------------------------------------------------ */
/*  SFU_Init                                                         */
/* ------------------------------------------------------------------ */

void SFU_Init(void)
{
    sfu_req_id_counter = 1u;

    /* Replace the debug_udp_handler on port 9000 with SFU_OnReceive */
    UDP_Bind(SFU_PORT, SFU_OnReceive);

    HAL_UART_PutString("[SFU ] init ok, listening on port ");
    HAL_UART_PutDec(SFU_PORT);
    HAL_UART_PutString("\n");
}
