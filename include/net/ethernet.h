/**
 * @file ethernet.h
 * @brief Ethernet II frame layer for MiniOS
 *
 * Provides frame transmission, reception dispatch, and MAC address
 * management. Sits directly above the VirtIO-Net MMIO driver and
 * below the IPv4 and ARP layers.
 *
 * Supported EtherTypes:
 *   0x0800 — IPv4 → IPV4_Receive()
 *   0x0806 — ARP  → ARP_Receive()
 *   all others are silently dropped
 *
 * @note No dynamic allocation. All TX staging is done through a
 *       single static 1514-byte buffer (max Ethernet payload without FCS).
 */

#ifndef MINIOS_NET_ETHERNET_H
#define MINIOS_NET_ETHERNET_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  EtherType constants                                               */
/* ------------------------------------------------------------------ */

#define ETH_TYPE_IPV4   0x0800u   /**< Internet Protocol v4 */
#define ETH_TYPE_ARP    0x0806u   /**< Address Resolution Protocol */

/** Maximum Ethernet payload length (MTU, no FCS) */
#define ETH_MTU         1500u
/** Maximum full frame length (14-byte header + 1500-byte payload) */
#define ETH_FRAME_MAX   1514u

/* ------------------------------------------------------------------ */
/*  Ethernet II frame header (14 bytes, no padding)                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Ethernet II frame header
 *
 * All fields are in network (big-endian) byte order as laid out
 * on the wire. ethertype must be byte-swapped with htons() before
 * writing and ntohs() after reading on a little-endian host.
 */
typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];  /**< Destination MAC address */
    uint8_t  src_mac[6];  /**< Source MAC address */
    uint16_t ethertype;   /**< EtherType in big-endian (e.g. 0x0800) */
} EthHdr_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the Ethernet layer
 *
 * Calls VNIC_Init(ETH_Receive) to register the frame receive callback
 * and reads our MAC address via VNIC_GetMAC(). Prints an init log line
 * to UART.
 *
 * Must be called after GIC is initialized (VNIC_Init enables IRQ).
 */
void ETH_Init(void);

/**
 * @brief Transmit a raw Ethernet frame
 *
 * Builds an Ethernet II header (src = our MAC, dst = dst_mac,
 * ethertype = htons(ethertype)), copies header + payload into the
 * static TX staging buffer, and hands it to VNIC_Send().
 *
 * @param[in] dst_mac      6-byte destination MAC address
 * @param[in] ethertype    EtherType in host byte order (e.g. ETH_TYPE_IPV4)
 * @param[in] payload      Payload bytes after the Ethernet header
 * @param[in] payload_len  Payload length in bytes (must be <= ETH_MTU)
 *
 * @return  0 on success
 *         -1 if payload_len exceeds ETH_MTU
 */
int ETH_Send(uint8_t dst_mac[6], uint16_t ethertype,
             uint8_t *payload, uint16_t payload_len);

/**
 * @brief Ethernet frame receive callback (registered with VirtIO-Net)
 *
 * Parses the Ethernet header and dispatches the payload upward:
 *   ARP  → ARP_Receive()
 *   IPv4 → IPV4_Receive()
 *   else → silently dropped
 *
 * Called from VNIC_Poll() / VNIC_IRQHandler() — keep it fast.
 *
 * @param[in] frame  Pointer to raw frame including Ethernet header
 * @param[in] len    Total frame byte count
 */
void ETH_Receive(uint8_t *frame, uint16_t len);

/**
 * @brief Get our own MAC address
 *
 * @param[out] mac  6-byte buffer to receive the MAC address
 */
void ETH_GetMAC(uint8_t mac[6]);

#endif /* MINIOS_NET_ETHERNET_H */
