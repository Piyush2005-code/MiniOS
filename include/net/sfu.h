/**
 * @file sfu.h
 * @brief Simple Framed UDP (SFU) framing layer for MiniOS
 *
 * Defines the binary wire format for every UDP payload exchanged
 * between the host (ML inference client) and the miniOS inference server.
 *
 * ════  Packet layout (sits inside the UDP payload)  ════
 *
 *  Offset  Size  Field
 *  ──────  ────  ─────────────────────────────────────────────
 *   0      4     magic        (0xDEAD6969 — reject everything else)
 *   4      1     version      (0x01)
 *   5      1     msg_type     (SFU_MSG_*)
 *   6      2     flags        (SFU_FLAG_* bitmask)
 *   8      4     req_id       (unique request ID, echoed in reply)
 *  12      4     seq_num      (fragment index, 0-based)
 *  16      4     total_seq    (total fragment count for this request)
 *  20      2     checksum     (CRC16-CCITT of payload bytes only)
 *  22      2     payload_len  (byte count immediately following header)
 *  24      N     payload      (tensor data, result bytes, error codes…)
 *
 * All multi-byte fields are little-endian (ARM64 native byte order).
 * Header size is exactly 24 bytes — enforced by _Static_assert.
 *
 * @note No dynamic memory is used anywhere in this module.
 *       All TX staging uses the static sfu_tx_buf.
 */

#ifndef MINIOS_NET_SFU_H
#define MINIOS_NET_SFU_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  Protocol constants                                               */
/* ------------------------------------------------------------------ */

/** Magic number that must appear in every valid SFU packet */
#define SFU_MAGIC           0xDEAD6969UL

/** Current wire protocol version */
#define SFU_VERSION         0x01u

/** UDP port used for all SFU traffic */
#define SFU_PORT            9000u

/* ------------------------------------------------------------------ */
/*  Message type codes (sfu_header_t.msg_type)                       */
/* ------------------------------------------------------------------ */

#define SFU_MSG_INFER_REQUEST   0x01u  /**< Host → guest: run inference */
#define SFU_MSG_INFER_RESPONSE  0x02u  /**< Guest → host: inference result */
#define SFU_MSG_ACK             0x03u  /**< Positive acknowledgement */
#define SFU_MSG_NACK            0x04u  /**< Negative acknowledgement / error */
#define SFU_MSG_PING            0x05u  /**< Connectivity probe */
#define SFU_MSG_PONG            0x06u  /**< Connectivity probe reply */
#define SFU_MSG_ERROR           0x10u  /**< Protocol-level error notification */

/* ------------------------------------------------------------------ */
/*  Flag bits (sfu_header_t.flags)                                   */
/* ------------------------------------------------------------------ */

/** Packet is a fragment of a larger logical message */
#define SFU_FLAG_FRAGMENTED     (1u << 0)

/** This is the last fragment of a fragmented message */
#define SFU_FLAG_LAST_FRAGMENT  (1u << 1)

/** Payload is compressed (reserved — not yet implemented) */
#define SFU_FLAG_COMPRESSED     (1u << 2)

/* ------------------------------------------------------------------ */
/*  Size limits                                                      */
/* ------------------------------------------------------------------ */

/** Maximum safe UDP payload: IP MTU (1500) - IP hdr (20) - UDP hdr (8) */
#define SFU_MAX_PACKET      1472u

/** Maximum SFU payload bytes: SFU_MAX_PACKET minus header */
#define SFU_MAX_PAYLOAD     (SFU_MAX_PACKET - 24u)   /* 1448 bytes */

/* ------------------------------------------------------------------ */
/*  Reliability limits                                               */
/* ------------------------------------------------------------------ */

/** Max concurrent unacked requests */
#define SFU_MAX_INFLIGHT    8

/** Retransmit after 200ms */
#define SFU_TIMEOUT_MS      200

/** Give up after 5 attempts */
#define SFU_MAX_RETRIES     5

/* ------------------------------------------------------------------ */
/*  Wire header (24 bytes, packed, all fields little-endian)         */
/* ------------------------------------------------------------------ */

/**
 * @brief SFU packet header — exactly 24 bytes on the wire
 *
 * Placed at offset 0 of every UDP payload. Followed immediately by
 * @c payload_len bytes of application payload.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;        /**< Must equal SFU_MAGIC (0xDEAD6969) */
    uint8_t  version;      /**< Must equal SFU_VERSION (0x01) */
    uint8_t  msg_type;     /**< SFU_MSG_* constant */
    uint16_t flags;        /**< SFU_FLAG_* bitmask */
    uint32_t req_id;       /**< Request identifier, echoed in replies */
    uint32_t seq_num;      /**< Fragment sequence index (0-based) */
    uint32_t total_seq;    /**< Total number of fragments for this req */
    uint16_t checksum;     /**< CRC16-CCITT over payload bytes only */
    uint16_t payload_len;  /**< Byte count of payload following header */
} sfu_header_t;

/** Compile-time size guard — triggers a build error if layout changes */
_Static_assert(sizeof(sfu_header_t) == 24u, "sfu_header_t must be 24 bytes");

/** Convenience: header size in bytes */
#define SFU_HEADER_SIZE     ((uint16_t)sizeof(sfu_header_t))

/* ------------------------------------------------------------------ */
/*  Public API                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the SFU layer
 *
 * Registers SFU_OnReceive with UDP_Bind(SFU_PORT) and resets the
 * request-ID counter. Must be called after UDP_Init().
 * Prints: "SFU: init ok, listening on port 9000"
 */
void SFU_Init(void);

/**
 * @brief SFU layer self-test
 *
 * Serializes a dummy PING packet, deserializes it, and verifies every
 * header field and payload byte.  Prints "SFU: selftest PASSED" on
 * success or "SFU: selftest FAILED at <field>" on the first mismatch.
 *
 * Must be called after SFU_Init().
 */
void SFU_SelfTest(void);

/**
 * @brief Compute CRC16-CCITT checksum over a payload buffer
 *
 * Polynomial: 0x1021, initial value: 0xFFFF.
 * Only the payload bytes are covered — the header's checksum field
 * is NOT included in the computation.
 *
 * @param[in] payload  Pointer to payload bytes (may be NULL)
 * @param[in] len      Number of bytes to checksum (0 returns 0)
 *
 * @return 16-bit CRC, or 0 if payload is NULL or len is 0
 */
uint16_t SFU_Checksum(uint8_t *payload, uint16_t len);

/**
 * @brief Serialize an SFU header + payload into a flat byte buffer
 *
 * Computes the checksum of @p payload and writes it into
 * @p hdr->checksum, then copies the header and payload
 * contiguously into @p out_buf.
 *
 * @param[in]  hdr          Header to serialize (checksum field is filled in)
 * @param[in]  payload      Payload bytes (may be NULL if payload_len == 0)
 * @param[out] out_buf      Destination buffer (must be >= SFU_MAX_PACKET)
 * @param[out] out_len      Total serialized byte count
 *
 * @return  0 on success
 *         -1 if hdr->payload_len exceeds SFU_MAX_PAYLOAD
 */
int SFU_Serialize(sfu_header_t *hdr, uint8_t *payload,
                  uint8_t *out_buf, uint16_t *out_len);

/**
 * @brief Deserialize an SFU packet from a flat byte buffer
 *
 * Validates magic, version, msg_type, and CRC16 checksum.
 *
 * @param[in]  buf             Raw packet buffer (from UDP_Receive)
 * @param[in]  len             Total buffer length in bytes
 * @param[out] hdr_out         Receives parsed header fields
 * @param[out] payload_out     Set to point into buf after the header
 * @param[out] payload_len_out Receives the payload byte count
 *
 * @return  0  success
 *         -1  magic/version/type validation failed
 *         -2  checksum mismatch
 */
int SFU_Deserialize(uint8_t *buf, uint16_t len,
                    sfu_header_t *hdr_out,
                    uint8_t **payload_out,
                    uint16_t *payload_len_out);

/**
 * @brief Pre-validate an SFU header without touching the payload
 *
 * Checks magic, version, and that msg_type is a known value.
 *
 * @param[in] hdr  Pointer to an already-parsed SFU header
 *
 * @return  0 if valid, -1 if any field is out of range
 */
int SFU_Validate(sfu_header_t *hdr);

/**
 * @brief Send a single non-fragmented SFU packet
 *
 * The caller specifies the message type; fragmentation fields are
 * set to (seq_num=0, total_seq=1, flags=0).
 *
 * @param[in] dst_ip       Destination IP (network byte order)
 * @param[in] dst_port     Destination UDP port (host byte order)
 * @param[in] msg_type     SFU_MSG_* constant
 * @param[in] req_id       Request ID to embed in the header
 * @param[in] payload      Payload bytes (may be NULL)
 * @param[in] payload_len  Payload byte count
 *
 * @return  0 on success, -1 on serialization or TX failure
 */
int SFU_SendRaw(uint32_t dst_ip, uint16_t dst_port,
                uint8_t msg_type, uint32_t req_id,
                uint8_t *payload, uint16_t payload_len);

/**
 * @brief Send a PING to the specified host
 *
 * Uses the next available request ID from the internal counter.
 *
 * @param[in] dst_ip    Destination IP (network byte order)
 * @param[in] dst_port  Destination UDP port
 *
 * @return  0 on success, -1 on TX failure
 */
int SFU_SendPing(uint32_t dst_ip, uint16_t dst_port);

/**
 * @brief Send a PONG reply, echoing the PING's request ID
 *
 * @param[in] dst_ip    Destination IP (network byte order)
 * @param[in] dst_port  Destination UDP port
 * @param[in] req_id    Request ID copied from the PING packet
 */
void SFU_SendPong(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id);

/**
 * @brief Send a NACK, echoing the offending packet's request ID
 *
 * @param[in] dst_ip    Destination IP (network byte order)
 * @param[in] dst_port  Destination UDP port
 * @param[in] req_id    Request ID from the packet being rejected
 */
void SFU_SendNack(uint32_t dst_ip, uint16_t dst_port, uint32_t req_id);

/**
 * @brief UDP receive callback — registered by SFU_Init()
 *
 * Deserializes the packet and dispatches on msg_type.
 * Invalid packets trigger a NACK reply. Unknown types are dropped.
 *
 * @param[in] src_ip    Sender's IP (network byte order)
 * @param[in] src_port  Sender's UDP port (host byte order)
 * @param[in] buf       Raw UDP payload (SFU header + payload)
 * @param[in] len       Total byte count of buf
 */
void SFU_OnReceive(uint32_t src_ip, uint16_t src_port,
                   uint8_t *buf, uint16_t len);

/**
 * @brief Inference request handler stub
 *
 * Called when msg_type == SFU_MSG_INFER_REQUEST.
 * Stubbed until the inference engine integration in Phase 5.
 *
 * @param[in] src_ip    Requester's IP
 * @param[in] src_port  Requester's port
 * @param[in] hdr       Parsed SFU header
 * @param[in] payload   Request payload (tensor data)
 * @param[in] len       Payload byte count
 */
void SFU_OnInferRequest(uint32_t src_ip, uint16_t src_port,
                        sfu_header_t *hdr,
                        uint8_t *payload, uint16_t len);

/* ------------------------------------------------------------------ */
/*  Reliability API                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Send a reliable SFU request
 *
 * @param[in]  dst_ip       Destination IP (network byte order)
 * @param[in]  dst_port     Destination UDP port (host byte order)
 * @param[in]  msg_type     SFU_MSG_* constant
 * @param[in]  payload      Payload bytes (may be NULL)
 * @param[in]  payload_len  Payload byte count
 * @param[out] out_req_id   Stores the generated request ID
 *
 * @return 0 on success, -1 if inflight table is full or serialization fails
 */
int SFU_SendReliable(uint32_t dst_ip, uint16_t dst_port,
                     uint8_t msg_type,
                     uint8_t *payload, uint16_t payload_len,
                     uint32_t *out_req_id);

/**
 * @brief Handle a received ACK
 *
 * @param[in] req_id        The ID being acknowledged
 */
void SFU_OnAck(uint32_t req_id);

/**
 * @brief Handle a received NACK
 *
 * @param[in] req_id        The ID being NACKed
 * @param[in] dst_ip        IP to retransmit to
 * @param[in] dst_port      Port to retransmit to
 */
void SFU_OnNack(uint32_t req_id, uint32_t dst_ip, uint16_t dst_port);

/**
 * @brief Periodic tick for retransmissions and timeouts
 */
void SFU_Tick(void);

/**
 * @brief Handle timeout of a reliable request
 *
 * @param[in] req_id        The timed out request ID
 */
void SFU_OnTimeout(uint32_t req_id);

/**
 * @brief Register a timeout callback
 *
 * @param[in] cb            Callback function
 */
void SFU_SetTimeoutCallback(void (*cb)(uint32_t req_id));

#endif /* MINIOS_NET_SFU_H */
