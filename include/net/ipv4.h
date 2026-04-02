/**
 * @file ipv4.h
 * @brief IPv4 packet layer for MiniOS
 *
 * Minimal IPv4 implementation supporting only:
 *   - Fixed 20-byte header (IHL = 5, no options)
 *   - Protocol 17 (UDP) only — all others dropped
 *   - Don't-Fragment bit always set
 *   - No fragmentation, no reassembly
 *   - No routing: all outgoing packets use the host MAC from ARP
 *
 * IP addresses are uint32_t stored in network (big-endian) byte order,
 * consistent with how they appear on the wire and in ARP packets.
 */

#ifndef MINIOS_NET_IPV4_H
#define MINIOS_NET_IPV4_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  IPv4 protocol numbers                                             */
/* ------------------------------------------------------------------ */

#define IP_PROTO_UDP    17u   /**< User Datagram Protocol */

/* ------------------------------------------------------------------ */
/*  IPv4 header (20 bytes, no options)                                */
/* ------------------------------------------------------------------ */

/**
 * @brief IPv4 header with IHL = 5 (no options)
 *
 * All multi-byte fields are in network (big-endian) byte order.
 * Size: exactly 20 bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;       /**< Version (4) | IHL (5) packed as 0x45 */
    uint8_t  dscp_ecn;      /**< DSCP + ECN, set to 0 */
    uint16_t total_len;     /**< Total length (header + payload), big-endian */
    uint16_t id;            /**< Identification field, big-endian */
    uint16_t flags_frag;    /**< Flags | Fragment offset; 0x4000 = DF */
    uint8_t  ttl;           /**< Time to live (64) */
    uint8_t  protocol;      /**< IP protocol number (e.g. IP_PROTO_UDP) */
    uint16_t checksum;      /**< One's complement checksum of header only */
    uint32_t src_ip;        /**< Source IP address, network byte order */
    uint32_t dst_ip;        /**< Destination IP address, network byte order */
} IPv4Hdr_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the IPv4 layer
 *
 * Resets the packet ID counter to 1. Prints an init log line to UART.
 */
void IPV4_Init(void);

/**
 * @brief Transmit an IPv4 packet
 *
 * Builds a 20-byte IPv4 header (IHL=5, DF set, TTL=64), computes the
 * header checksum, looks up the destination MAC via ARP_GetHostMAC(),
 * and calls ETH_Send(ETH_TYPE_IPV4, ...).
 *
 * @param[in] dst_ip       Destination IP in network byte order
 * @param[in] protocol     IP protocol number (e.g. IP_PROTO_UDP)
 * @param[in] payload      Upper-layer payload bytes
 * @param[in] payload_len  Payload length in bytes
 *
 * @return  0 on success
 *         -1 if total size would exceed MTU
 */
int IPV4_Send(uint32_t dst_ip, uint8_t protocol,
              uint8_t *payload, uint16_t payload_len);

/**
 * @brief Process a received IPv4 packet
 *
 * Called by ETH_Receive() when ethertype == ETH_TYPE_IPV4.
 * Validates version, IHL, checksum, and destination IP, then
 * dispatches UDP payloads to UDP_Receive().
 *
 * @param[in] buf  Pointer to IPv4 packet (starts at version/IHL byte)
 * @param[in] len  Length of buf in bytes
 */
void IPV4_Receive(uint8_t *buf, uint16_t len);

/**
 * @brief Compute the standard IP one's complement header checksum
 *
 * Sums all 16-bit words in the header, folds carries, and returns
 * the one's complement of the result. The checksum field in the
 * header must be zeroed before calling.
 *
 * @param[in] hdr  Pointer to the start of the IPv4 header
 * @param[in] len  Header length in bytes (always 20 for IHL=5)
 *
 * @return 16-bit checksum in network byte order
 */
uint16_t IPV4_Checksum(void *hdr, uint16_t len);

#endif /* MINIOS_NET_IPV4_H */
