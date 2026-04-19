/**
 * @file arp.h
 * @brief ARP (Address Resolution Protocol) layer for MiniOS
 *
 * Handles ARP requests for the guest IP (10.0.2.15) and caches the
 * host MAC address (10.0.2.2) so upper layers can populate Ethernet
 * destination addresses without running ARP themselves.
 *
 * Only ARP over Ethernet for IPv4 (hw_type=1, proto_type=0x0800)
 * is supported. All other ARP packets are silently dropped.
 *
 * IP addresses are stored as uint32_t in network (big-endian) byte
 * order, matching their on-wire representation.
 */

#ifndef MINIOS_NET_ARP_H
#define MINIOS_NET_ARP_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  Network addresses (network byte order / big-endian)               */
/* ------------------------------------------------------------------ */

/**
 * @defgroup ip_addrs Static IP addresses in network byte order
 *
 * On a little-endian ARM host 10.0.2.15 is stored as:
 *   byte[0]=0x0a  byte[1]=0x00  byte[2]=0x02  byte[3]=0x0f
 * as a uint32_t that is 0x0a00020f.
 * @{
 */
#define GUEST_IP  0x0f02000aUL  /**< 10.0.2.15 — guest (our) IP, little-endian word */
#define HOST_IP   0x0202000aUL  /**< 10.0.2.2  — SLIRP host/gateway IP, little-endian word */
/** @} */

/* ------------------------------------------------------------------ */
/*  ARP operation codes                                               */
/* ------------------------------------------------------------------ */

#define ARP_OP_REQUEST  1u  /**< ARP who-has request */
#define ARP_OP_REPLY    2u  /**< ARP is-at reply */

/* ------------------------------------------------------------------ */
/*  ARP packet structure (Ethernet + IPv4, 28 bytes)                  */
/* ------------------------------------------------------------------ */

/**
 * @brief ARP packet for Ethernet/IPv4
 *
 * All multi-byte fields are in network (big-endian) byte order.
 */
typedef struct __attribute__((packed)) {
    uint16_t hw_type;       /**< Hardware type: 0x0001 = Ethernet */
    uint16_t proto_type;    /**< Protocol type: 0x0800 = IPv4 */
    uint8_t  hw_len;        /**< Hardware address length: 6 */
    uint8_t  proto_len;     /**< Protocol address length: 4 */
    uint16_t operation;     /**< ARP_OP_REQUEST (1) or ARP_OP_REPLY (2) */
    uint8_t  sender_mac[6]; /**< Sender hardware address */
    uint32_t sender_ip;     /**< Sender protocol address (network order) */
    uint8_t  target_mac[6]; /**< Target hardware address */
    uint32_t target_ip;     /**< Target protocol address (network order) */
} ArpPkt_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize ARP state
 *
 * Clears the cached host MAC entry (sets to broadcast so callers
 * can safely use it before the first ARP exchange).
 */
void ARP_Init(void);

/**
 * @brief Process a received ARP packet
 *
 * Called by ETH_Receive() when ethertype == ETH_TYPE_ARP.
 *
 * Behaviour:
 *  - Any packet where sender_ip == HOST_IP updates the cached host MAC.
 *  - ARP requests where target_ip == GUEST_IP trigger a unicast ARP reply.
 *  - All other packets are silently dropped.
 *
 * @param[in] buf  Pointer to ARP payload (starts at hw_type field)
 * @param[in] len  Length of buf in bytes
 */
void ARP_Receive(uint8_t *buf, uint16_t len);

/**
 * @brief Get the cached host (gateway) MAC address
 *
 * Returns the MAC most recently seen from HOST_IP (10.0.2.2).
 * If no ARP packet from the host has been received yet, returns
 * the Ethernet broadcast address ff:ff:ff:ff:ff:ff so that the
 * first UDP packet is still delivered (SLIRP accepts broadcasts).
 *
 * @param[out] mac  6-byte buffer to receive the host MAC
 */
void ARP_GetHostMAC(uint8_t mac[6]);

#endif /* MINIOS_NET_ARP_H */
