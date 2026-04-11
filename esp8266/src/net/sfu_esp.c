/**
 * @file sfu_esp.c
 * @brief Simple Framed UDP (SFU) Implementation for MiniOS-ESP8266
 *
 * The SFU wire protocol is IDENTICAL to the ARM64 original — same magic,
 * same 24-byte header, same CRC16-CCITT — so sfu_client.py works with
 * zero protocol changes. Only the transport layer differs (Wi-Fi UDP via
 * espconn instead of VirtIO-Net).
 *
 * Key changes vs original sfu.c:
 *   1. No VNIC_Poll() in SFU_Tick() — espconn is interrupt-driven
 *   2. SFU_MAX_INFLIGHT reduced from 8 to 2 (ESP8266 RAM constraint)
 *   3. Timing via HAL_Timer_GetSystemTicks() (os_timer based)
 *   4. All 64-bit fields cast to 32-bit (ESP8266 is 32-bit)
 */

#include "net/sfu.h"
#include "net/udp.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Static TX staging buffer                                          */
/* ------------------------------------------------------------------ */

static uint8_t sfu_tx_buf[SFU_MAX_PACKET];

/* ------------------------------------------------------------------ */
/*  Module state                                                      */
/* ------------------------------------------------------------------ */

static uint32_t sfu_req_id_counter = 1u;

typedef struct {
    uint8_t  in_use;
    uint32_t req_id;
    uint32_t sent_at_ms;
    uint8_t  retries;
    uint8_t  buf[SFU_MAX_PACKET];
    uint16_t buf_len;
    uint32_t dst_ip;
    uint16_t dst_port;
} sfu_inflight_t;

static sfu_inflight_t  sfu_inflight[SFU_MAX_INFLIGHT];
static sfu_stats_t     sfu_stats;
static sfu_infer_handler_t sfu_infer_handler = (sfu_infer_handler_t)0;
static sfu_cmd_handler_t   sfu_cmd_handler   = (sfu_cmd_handler_t)0;
static void (*sfu_timeout_cb)(uint32_t) = (void(*)(uint32_t))0;

/* ------------------------------------------------------------------ */
/*  Timing helper                                                     */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR uint32_t sfu_get_ms(void)
{
    return HAL_Timer_GetSystemTicks() * HAL_Timer_GetTickPeriodMs();
}

/* ------------------------------------------------------------------ */
/*  Internal: byte-by-byte memcpy                                     */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR void sfu_memcpy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
}

/* ------------------------------------------------------------------ */
/*  Internal: known type validation                                   */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR int sfu_is_known_type(uint8_t t)
{
    return (t == SFU_MSG_INFER_REQUEST  ||
            t == SFU_MSG_INFER_RESPONSE ||
            t == SFU_MSG_ACK            ||
            t == SFU_MSG_NACK           ||
            t == SFU_MSG_PING           ||
            t == SFU_MSG_PONG           ||
            t == SFU_MSG_CMD            ||
            t == SFU_MSG_CMD_RESPONSE   ||
            t == SFU_MSG_ERROR);
}

/* ------------------------------------------------------------------ */
/*  SFU_Checksum — CRC16-CCITT (poly=0x1021, init=0xFFFF)            */
/*  MUST match sfu_client.py CRC16.compute() exactly.                */
/* ------------------------------------------------------------------ */

uint16_t ICACHE_FLASH_ATTR SFU_Checksum(uint8_t *payload, uint16_t len)
{
    if (!payload || len == 0) return 0u;
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)payload[i] << 8);
        for (uint8_t bit = 0; bit < 8u; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/*  SFU_Validate                                                      */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR SFU_Validate(sfu_header_t *hdr)
{
    if (hdr->magic   != SFU_MAGIC)   return -1;
    if (hdr->version != SFU_VERSION) return -1;
    if (!sfu_is_known_type(hdr->msg_type)) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SFU_Serialize                                                     */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR SFU_Serialize(sfu_header_t *hdr, uint8_t *payload,
                  uint8_t *out_buf, uint16_t *out_len)
{
    uint16_t plen = hdr->payload_len;
    if (plen > SFU_MAX_PAYLOAD) return -1;
    hdr->checksum = SFU_Checksum(payload, plen);
    sfu_memcpy(out_buf, (const uint8_t *)hdr, SFU_HEADER_SIZE);
    if (plen > 0 && payload) sfu_memcpy(out_buf + SFU_HEADER_SIZE, payload, plen);
    *out_len = (uint16_t)(SFU_HEADER_SIZE + plen);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SFU_Deserialize                                                   */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR SFU_Deserialize(uint8_t *buf, uint16_t len,
                    sfu_header_t *hdr_out,
                    uint8_t **payload_out, uint16_t *payload_len_out)
{
    if (len < SFU_HEADER_SIZE) return -1;
    sfu_memcpy((uint8_t *)hdr_out, buf, SFU_HEADER_SIZE);
    if (SFU_Validate(hdr_out) != 0) return -1;

    uint16_t plen = hdr_out->payload_len;
    if ((uint32_t)SFU_HEADER_SIZE + plen > (uint32_t)len) return -1;

    uint8_t *pptr = buf + SFU_HEADER_SIZE;
    uint16_t crc  = SFU_Checksum(pptr, plen);
    if (crc != hdr_out->checksum) return -2;

    *payload_out     = pptr;
    *payload_len_out = plen;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SFU_SendRaw                                                       */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR SFU_SendRaw(uint32_t dst_ip, uint16_t dst_port,
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
    hdr.checksum    = 0u;
    hdr.payload_len = payload_len;

    uint16_t out_len = 0;
    if (SFU_Serialize(&hdr, payload, sfu_tx_buf, &out_len) != 0) return -1;

    int ret = UDP_Send(dst_ip, dst_port, SFU_PORT, sfu_tx_buf, out_len);
    if (ret == 0) {
        sfu_stats.tx_packets++;
        sfu_stats.tx_bytes += payload_len;
        if (msg_type == SFU_MSG_PONG)           sfu_stats.pong_count++;
        if (msg_type == SFU_MSG_INFER_RESPONSE) sfu_stats.infer_responses++;
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  SFU_SendPong / SFU_SendNack                                       */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR SFU_SendPing(uint32_t dst_ip, uint16_t dst_port)
{
    return SFU_SendRaw(dst_ip, dst_port, SFU_MSG_PING,
                       sfu_req_id_counter++, (uint8_t *)0, 0u);
}

void ICACHE_FLASH_ATTR SFU_SendPong(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id)
{
    SFU_SendRaw(dst_ip, dst_port, SFU_MSG_PONG, req_id, (uint8_t *)0, 0u);
}

void ICACHE_FLASH_ATTR SFU_SendNack(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id)
{
    SFU_SendRaw(dst_ip, dst_port, SFU_MSG_NACK, req_id, (uint8_t *)0, 0u);
}

/* ------------------------------------------------------------------ */
/*  SFU_OnReceive — packet dispatch                                   */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR SFU_OnReceive(uint32_t src_ip, uint16_t src_port,
                   uint8_t *buf, uint16_t len)
{
    sfu_header_t hdr;
    uint8_t     *payload     = (uint8_t *)0;
    uint16_t     payload_len = 0u;

    int rc = SFU_Deserialize(buf, len, &hdr, &payload, &payload_len);
    if (rc == -1) {
        SFU_SendNack(src_ip, src_port, 0u);
        sfu_stats.bad_magic++;
        return;
    }
    if (rc == -2) {
        SFU_SendNack(src_ip, src_port, hdr.req_id);
        sfu_stats.checksum_errors++;
        return;
    }

    sfu_stats.rx_packets++;
    sfu_stats.rx_bytes += payload_len;

#if DEBUG_SFU_VERBOSE
    HAL_UART_PutString("[SFU ] RX type=0x");
    HAL_UART_PutHex(hdr.msg_type);
    HAL_UART_PutString(" req=");
    HAL_UART_PutDec(hdr.req_id);
    HAL_UART_PutString(" plen=");
    HAL_UART_PutDec(payload_len);
    HAL_UART_PutString("\n");
#endif

    switch (hdr.msg_type) {

        case SFU_MSG_PING:
            sfu_stats.ping_count++;
            SFU_SendPong(src_ip, src_port, hdr.req_id);
            break;

        case SFU_MSG_INFER_REQUEST:
            sfu_stats.infer_requests++;
            SFU_SendRaw(src_ip, src_port, SFU_MSG_ACK, hdr.req_id,
                        (uint8_t *)0, 0u);
            if (sfu_infer_handler) {
                sfu_infer_handler(src_ip, src_port, hdr.req_id,
                                  payload, payload_len);
            }
            break;

        case SFU_MSG_CMD:
            sfu_stats.cmd_count++;
            if (sfu_cmd_handler && payload_len > 0) {
                static char cmd_buf[128];
                uint16_t clen = payload_len < 127 ? payload_len : 127;
                for (uint16_t i = 0; i < clen; i++) cmd_buf[i] = (char)payload[i];
                cmd_buf[clen] = '\0';
                sfu_cmd_handler(src_ip, src_port, hdr.req_id, cmd_buf, clen);
            }
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Registration / stats                                              */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR SFU_SetInferHandler(sfu_infer_handler_t h) { sfu_infer_handler = h; }
void ICACHE_FLASH_ATTR SFU_SetCmdHandler(sfu_cmd_handler_t h)     { sfu_cmd_handler   = h; }
void ICACHE_FLASH_ATTR SFU_GetStats(sfu_stats_t *out) { if (out) *out = sfu_stats; }

/* ------------------------------------------------------------------ */
/*  SFU_Tick — retransmission timer (called every 100ms by os_timer)  */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR SFU_Tick(void)
{
    /* NOTE: No VNIC_Poll() — espconn is interrupt-driven on ESP8266 */
    uint32_t now = sfu_get_ms();
    for (int i = 0; i < SFU_MAX_INFLIGHT; i++) {
        if (!sfu_inflight[i].in_use) continue;
        if ((now - sfu_inflight[i].sent_at_ms) >= SFU_TIMEOUT_MS) {
            if (sfu_inflight[i].retries < SFU_MAX_RETRIES) {
                UDP_Send(sfu_inflight[i].dst_ip, sfu_inflight[i].dst_port,
                         SFU_PORT, sfu_inflight[i].buf,
                         sfu_inflight[i].buf_len);
                sfu_inflight[i].retries++;
                sfu_inflight[i].sent_at_ms = now;
            } else {
                if (sfu_timeout_cb) sfu_timeout_cb(sfu_inflight[i].req_id);
                sfu_inflight[i].in_use = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  SFU_SelfTest                                                      */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR SFU_SelfTest(void)
{
    HAL_UART_PutString("[SFU ] self-test...\n");

    static uint8_t test_payload[4] = {0x01,0x02,0x03,0x04};
    sfu_header_t orig = {
        .magic       = SFU_MAGIC,
        .version     = SFU_VERSION,
        .msg_type    = SFU_MSG_PING,
        .flags       = 0,
        .req_id      = 0xDEADBEEFu,
        .seq_num     = 0,
        .total_seq   = 1,
        .checksum    = 0,
        .payload_len = 4,
    };

    static uint8_t st_buf[SFU_MAX_PACKET];
    uint16_t out_len = 0;
    if (SFU_Serialize(&orig, test_payload, st_buf, &out_len) != 0) {
        HAL_UART_PutString("[SFU ] FAILED: Serialize\n"); return;
    }

    sfu_header_t parsed;
    uint8_t *pld = (uint8_t *)0;
    uint16_t plen = 0;
    if (SFU_Deserialize(st_buf, out_len, &parsed, &pld, &plen) != 0) {
        HAL_UART_PutString("[SFU ] FAILED: Deserialize\n"); return;
    }
    if (parsed.magic != SFU_MAGIC || parsed.req_id != 0xDEADBEEFu ||
        plen != 4 || pld[0] != 1 || pld[3] != 4) {
        HAL_UART_PutString("[SFU ] FAILED: field mismatch\n"); return;
    }

    HAL_UART_PutString("[SFU ] self-test PASSED\n");
}

/* ------------------------------------------------------------------ */
/*  SFU_Init                                                          */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR SFU_Init(void)
{
    sfu_req_id_counter = 1u;
    for (int i = 0; i < SFU_MAX_INFLIGHT; i++) sfu_inflight[i].in_use = 0;
    /* Zero stats */
    uint8_t *ps = (uint8_t *)&sfu_stats;
    for (uint32_t i = 0; i < sizeof(sfu_stats); i++) ps[i] = 0;

    UDP_Bind(SFU_PORT, SFU_OnReceive);

    HAL_UART_PutString("[SFU ] init ok, listening on port ");
    HAL_UART_PutDec(SFU_PORT);
    HAL_UART_PutString("\n");
}
