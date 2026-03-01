/**
 * @file net_types.h
 * @brief Core type definitions for MiniOS-NetProtocol (RUDP layer)
 *
 * MiniOS Reliable UDP (RUDP) — a lightweight transport protocol
 * designed for ARM64 bare-metal unikernel ML inference.
 *
 * DESIGN RATIONALE:
 *   Raw UDP  → fast, zero-overhead, but no delivery guarantee
 *   Full TCP → delivery guaranteed, but heavy (3-way handshake,
 *              flow control, congestion control, state machine)
 *   RUDP     → selective reliability: guaranteed delivery of
 *              ML control frames (model load, results), best-effort
 *              for telemetry/status. Stateless server model suits
 *              the unikernel single-address-space design.
 *
 * RUDP Frame Format (over raw Ethernet / IP-less for bare-metal):
 *   [ETH_HDR 14B][RUDP_HDR 12B][PAYLOAD up to 1452B][CRC16 2B]
 *
 * Compatible with: MiniOS-UART_Implementation (same command codes),
 *                  MiniOS-BootLoader_and_HAL (HAL_UART / HAL_TIMER),
 *                  MiniOS-feat-onnx (ONNX command framing),
 *                  MiniOS-unikernel (benchmark hooks).
 */

#ifndef MINIOS_NET_TYPES_H
#define MINIOS_NET_TYPES_H

#include "../../include/types.h"   /* reuse project-wide types.h */
#include "../../include/status.h"  /* reuse project-wide status.h */

/* ------------------------------------------------------------------ */
/*  Ethernet / Link-layer constants                                   */
/* ------------------------------------------------------------------ */
#define ETH_ALEN            6       /* MAC address bytes              */
#define ETH_HDR_LEN         14      /* dst(6)+src(6)+ethertype(2)     */
#define ETH_MTU             1500    /* standard MTU                   */
#define RUDP_ETHERTYPE      0x88B5  /* locally administered ethertype */

/* ------------------------------------------------------------------ */
/*  RUDP Header (12 bytes, packed)                                    */
/*                                                                    */
/*   0       1       2       3                                        */
/*  +-------+-------+-------+-------+                                */
/*  |  MAGIC (0xRD) | FLAGS | CMD   |  byte 0-3                      */
/*  +-------+-------+-------+-------+                                */
/*  |       SEQUENCE NUMBER         |  byte 4-7 (big-endian)         */
/*  +-------+-------+-------+-------+                                */
/*  |    PAYLOAD LENGTH (2B)        |  byte 8-9 (big-endian)         */
/*  +-------+-------+-------+-------+                                */
/*  |      ACK/NACK SEQ (2B)        |  byte 10-11                    */
/*  +-------+-------+-------+-------+                                */
/* ------------------------------------------------------------------ */
#define RUDP_MAGIC          0xRD    /* value 0xAE — "Reliable Data"  */
#define RUDP_MAGIC_BYTE     0xAE

#define RUDP_HDR_LEN        12
#define RUDP_MAX_PAYLOAD    (ETH_MTU - ETH_HDR_LEN - RUDP_HDR_LEN - 2)
                                    /* 1472 bytes                     */

/* ------------------------------------------------------------------ */
/*  RUDP Flags (1 byte bitmask)                                       */
/* ------------------------------------------------------------------ */
#define RUDP_FLAG_RELIABLE  (1 << 0) /* Requires ACK                 */
#define RUDP_FLAG_ACK       (1 << 1) /* This frame IS an ACK         */
#define RUDP_FLAG_NACK      (1 << 2) /* Negative ACK — retransmit    */
#define RUDP_FLAG_FRAG      (1 << 3) /* Fragment (more fragments)    */
#define RUDP_FLAG_FRAG_END  (1 << 4) /* Last fragment                */
#define RUDP_FLAG_KEEPALIVE (1 << 5) /* Keepalive ping               */
#define RUDP_FLAG_RESET     (1 << 6) /* Reset session                */
#define RUDP_FLAG_RESERVED  (1 << 7) /* Reserved (must be 0)         */

/* ------------------------------------------------------------------ */
/*  Command codes — aligned with UART protocol (Appendix F SRS v2)   */
/* ------------------------------------------------------------------ */
typedef enum {
    NET_CMD_LOAD_MODEL      = 0x01,  /* Load ONNX model payload       */
    NET_CMD_SET_INPUT       = 0x02,  /* Set input tensor              */
    NET_CMD_RUN_INFERENCE   = 0x03,  /* Trigger inference             */
    NET_CMD_GET_RESULTS     = 0x04,  /* Retrieve results              */
    NET_CMD_SYSTEM_STATUS   = 0x05,  /* Health/status query           */
    NET_CMD_CONFIG_UPDATE   = 0x06,  /* Configuration parameter       */
    /* Extended network-only commands */
    NET_CMD_KEEPALIVE       = 0x10,  /* Ping/pong                     */
    NET_CMD_RESET           = 0x11,  /* Reset session state           */
    NET_CMD_FRAG_DATA       = 0x12,  /* Fragment continuation         */
    NET_CMD_BENCHMARK       = 0x13,  /* Benchmark hook (unikernel)    */
} NetCommand;

/* ------------------------------------------------------------------ */
/*  Reliability class — determines ACK behaviour                      */
/* ------------------------------------------------------------------ */
typedef enum {
    RUDP_CLASS_RELIABLE     = 0,  /* ACK required; retransmit on NACK */
    RUDP_CLASS_BEST_EFFORT  = 1,  /* No ACK; fire-and-forget          */
} RudpReliabilityClass;

/* Commands that MUST be reliable (ML control plane) */
#define NET_CMD_IS_RELIABLE(cmd) \
    ((cmd) == NET_CMD_LOAD_MODEL  || \
     (cmd) == NET_CMD_SET_INPUT   || \
     (cmd) == NET_CMD_RUN_INFERENCE || \
     (cmd) == NET_CMD_GET_RESULTS || \
     (cmd) == NET_CMD_CONFIG_UPDATE)

/* ------------------------------------------------------------------ */
/*  Packed RUDP header structure                                      */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint8_t  magic;       /* Must be RUDP_MAGIC_BYTE                  */
    uint8_t  flags;       /* Bitmask of RUDP_FLAG_*                   */
    uint8_t  cmd;         /* NetCommand                               */
    uint8_t  reserved;    /* Must be 0                                */
    uint32_t seq;         /* Sequence number (big-endian)             */
    uint16_t payload_len; /* Payload length (big-endian)              */
    uint16_t ack_seq;     /* Acknowledged seq / NACK requested seq    */
} RudpHeader;

/* ------------------------------------------------------------------ */
/*  Ethernet header                                                   */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;   /* Big-endian RUDP_ETHERTYPE                */
} EthHeader;

/* ------------------------------------------------------------------ */
/*  Complete RUDP frame (header + payload region)                     */
/* ------------------------------------------------------------------ */
typedef struct {
    EthHeader  eth;
    RudpHeader rudp;
    uint8_t    payload[RUDP_MAX_PAYLOAD];
    uint16_t   crc16;    /* CRC-16/CCITT over eth+rudp+payload        */
} RudpFrame;

/* ------------------------------------------------------------------ */
/*  Session / connection state                                        */
/* ------------------------------------------------------------------ */
#define RUDP_WINDOW_SIZE    8        /* Unacknowledged frames in flight */
#define RUDP_MAX_RETRIES    5        /* Retransmit attempts             */
#define RUDP_RETRY_TIMEOUT_MS  200   /* Retransmit wait (ms)            */
#define RUDP_KEEPALIVE_MS   5000     /* Keepalive interval              */
#define RUDP_SESSION_TIMEOUT_MS 15000 /* Dead session threshold         */

typedef struct {
    uint8_t  remote_mac[ETH_ALEN];  /* Peer MAC address               */
    uint32_t tx_seq;                 /* Next sequence number to send   */
    uint32_t rx_expected;            /* Next sequence expected to recv  */
    uint32_t last_acked;             /* Last ACKed sequence            */
    uint8_t  retry_count;            /* Current retransmit count       */
    bool     active;                 /* Session is established         */
    uint64_t last_rx_ms;             /* Timestamp of last received frame */
    /* Retransmit buffer (one window) */
    uint8_t  tx_buf[RUDP_WINDOW_SIZE][ETH_HDR_LEN + RUDP_HDR_LEN + RUDP_MAX_PAYLOAD + 2];
    uint16_t tx_buf_len[RUDP_WINDOW_SIZE];
    uint32_t tx_buf_seq[RUDP_WINDOW_SIZE];
    bool     tx_buf_pending[RUDP_WINDOW_SIZE];
} RudpSession;

#endif /* MINIOS_NET_TYPES_H */