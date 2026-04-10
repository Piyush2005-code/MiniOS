/**
 * @file sfu.h
 * @brief Simple Framed UDP (SFU) Protocol Header for MiniOS-ESP8266
 *
 * Wire format is IDENTICAL to the original MiniOS ARM64 protocol so
 * that sfu_client.py works without any protocol changes.
 *
 *  Offset  Size  Field
 *  ──────  ────  ─────────────────────────────
 *   0      4     magic       0xDEAD6969
 *   4      1     version     0x01
 *   5      1     msg_type    SFU_MSG_*
 *   6      2     flags       0x0000
 *   8      4     req_id
 *  12      4     seq_num     0
 *  16      4     total_seq   1
 *  20      2     checksum    CRC16-CCITT (payload only)
 *  22      2     payload_len
 *  24      N     payload
 */

#ifndef MINIOS_ESP8266_NET_SFU_H
#define MINIOS_ESP8266_NET_SFU_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Protocol constants (identical to original)                        */
/* ------------------------------------------------------------------ */

#define SFU_MAGIC           0xDEAD6969UL
#define SFU_VERSION         0x01u
#define SFU_PORT            9000u

#define SFU_MSG_INFER_REQUEST   0x01u
#define SFU_MSG_INFER_RESPONSE  0x02u
#define SFU_MSG_ACK             0x03u
#define SFU_MSG_NACK            0x04u
#define SFU_MSG_PING            0x05u
#define SFU_MSG_PONG            0x06u
#define SFU_MSG_CMD             0x07u
#define SFU_MSG_CMD_RESPONSE    0x08u
#define SFU_MSG_ERROR           0x10u

#define SFU_FLAG_FRAGMENTED     (1u << 0)
#define SFU_FLAG_LAST_FRAGMENT  (1u << 1)

/* Max UDP payload = 1500 - 20 (IP) - 8 (UDP) = 1472 */
#define SFU_MAX_PACKET      1472u
#define SFU_MAX_PAYLOAD     (SFU_MAX_PACKET - 24u)
#define SFU_HEADER_SIZE     24u

/* Reliability */
#define SFU_MAX_INFLIGHT    2u   /* reduced from 8 — ESP8266 RAM constraint */
#define SFU_TIMEOUT_MS      500u
#define SFU_MAX_RETRIES     3u

/* ------------------------------------------------------------------ */
/*  Wire header — packed, 24 bytes, little-endian                     */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msg_type;
    uint16_t flags;
    uint32_t req_id;
    uint32_t seq_num;
    uint32_t total_seq;
    uint16_t checksum;
    uint16_t payload_len;
} sfu_header_t;

/* Compile-time size guard */
typedef char sfu_hdr_size_check[(sizeof(sfu_header_t) == 24u) ? 1 : -1];

/* ------------------------------------------------------------------ */
/*  Handler callbacks                                                 */
/* ------------------------------------------------------------------ */

typedef void (*sfu_infer_handler_t)(uint32_t src_ip, uint16_t src_port,
    uint32_t req_id, uint8_t *payload, uint16_t payload_len);

typedef void (*sfu_cmd_handler_t)(uint32_t src_ip, uint16_t src_port,
    uint32_t req_id, const char *cmd, uint16_t cmd_len);

/* ------------------------------------------------------------------ */
/*  Stats                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t checksum_errors;
    uint32_t bad_magic;
    uint32_t infer_requests;
    uint32_t infer_responses;
    uint32_t ping_count;
    uint32_t pong_count;
    uint32_t cmd_count;
} sfu_stats_t;

/* ------------------------------------------------------------------ */
/*  Public API (mirrors original sfu.h)                               */
/* ------------------------------------------------------------------ */

void     SFU_Init(void);
void     SFU_SelfTest(void);

uint16_t SFU_Checksum(uint8_t *payload, uint16_t len);
int      SFU_Validate(sfu_header_t *hdr);
int      SFU_Serialize(sfu_header_t *hdr, uint8_t *payload,
                       uint8_t *out_buf, uint16_t *out_len);
int      SFU_Deserialize(uint8_t *buf, uint16_t len,
                         sfu_header_t *hdr_out,
                         uint8_t **payload_out, uint16_t *payload_len_out);

int  SFU_SendRaw(uint32_t dst_ip, uint16_t dst_port,
                 uint8_t msg_type, uint32_t req_id,
                 uint8_t *payload, uint16_t payload_len);
int  SFU_SendPing(uint32_t dst_ip, uint16_t dst_port);
void SFU_SendPong(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id);
void SFU_SendNack(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id);

void SFU_OnReceive(uint32_t src_ip, uint16_t src_port,
                   uint8_t *buf, uint16_t len);

void SFU_SetInferHandler(sfu_infer_handler_t h);
void SFU_SetCmdHandler(sfu_cmd_handler_t h);
void SFU_GetStats(sfu_stats_t *out);
void SFU_Tick(void);

#endif /* MINIOS_ESP8266_NET_SFU_H */
