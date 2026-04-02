/**
 * @file arp.c
 * @brief ARP layer implementation for MiniOS
 *
 * Processes ARP requests for the guest IP (10.0.2.15) and generates
 * unicast ARP replies. Caches the host MAC (10.0.2.2) so the IPv4
 * TX path can resolve the Ethernet destination without extra lookups.
 *
 * Only Ethernet/IPv4 ARP (hw_type=1, proto_type=0x0800) is handled.
 *
 * IP addresses in ARP packets are in network byte order.  On a
 * little-endian ARM64 system, 10.0.2.15 stored as a uint32_t in
 * memory has the byte layout: [0x0a][0x00][0x02][0x0f], which
 * corresponds to the little-endian word 0x0f02000a (GUEST_IP).
 *
 * @note Per SRS FR-NET-ARP
 *
 * @complexity
 *   ARP_Receive : O(1)
 *   ARP_GetHostMAC : O(1)
 */

#include "net/arp.h"
#include "net/ethernet.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Byte order helpers (internal)                                    */
/* ------------------------------------------------------------------ */

/** Swap bytes of a 16-bit value (little-endian host ↔ network order) */
static inline uint16_t arp_htons(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

/* ------------------------------------------------------------------ */
/*  ARP constants (in network byte order for direct comparison)      */
/* ------------------------------------------------------------------ */

/** ARP hardware type for Ethernet in network byte order */
#define ARP_HW_ETH_BE      0x0100u  /* htons(0x0001) */
/** ARP protocol type for IPv4 in network byte order */
#define ARP_PROTO_IPV4_BE  0x0008u  /* htons(0x0800) */
/** ARP REQUEST operation in network byte order */
#define ARP_OP_REQUEST_BE  0x0100u  /* htons(0x0001) */
/** ARP REPLY operation in network byte order */
#define ARP_OP_REPLY_BE    0x0200u  /* htons(0x0002) */

/* ------------------------------------------------------------------ */
/*  Module-private state                                             */
/* ------------------------------------------------------------------ */

/** Cached MAC address of the SLIRP host/gateway (10.0.2.2) */
static uint8_t g_host_mac[6];

/** Set to 1 once we learn the host MAC from an incoming ARP */
static uint8_t g_host_mac_valid;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                 */
/* ------------------------------------------------------------------ */

/** Print a byte as two hex digits to UART */
static void arp_put_hex8(uint8_t v)
{
    int hi = (int)((v >> 4) & 0xFu);
    int lo = (int)(v & 0xFu);
    HAL_UART_PutChar((char)(hi < 10 ? '0' + hi : 'a' + hi - 10));
    HAL_UART_PutChar((char)(lo < 10 ? '0' + lo : 'a' + lo - 10));
}

/* ------------------------------------------------------------------ */
/*  ARP_Init                                                         */
/* ------------------------------------------------------------------ */

void ARP_Init(void)
{
    /*
     * Set host MAC to broadcast — any TX before we learn it from
     * an ARP will still reach SLIRP because SLIRP accepts broadcasts.
     */
    for (uint32_t i = 0; i < 6u; i++) {
        g_host_mac[i] = 0xFFu;
    }
    g_host_mac_valid = 0;
}

/* ------------------------------------------------------------------ */
/*  ARP_Receive                                                      */
/* ------------------------------------------------------------------ */

void ARP_Receive(uint8_t *buf, uint16_t len)
{
    if (len < (uint16_t)sizeof(ArpPkt_t)) {
        return;
    }

    const ArpPkt_t *pkt = (const ArpPkt_t *)buf;

    /* Only handle Ethernet/IPv4 ARP */
    if (pkt->hw_type    != ARP_HW_ETH_BE)     { return; }
    if (pkt->proto_type != ARP_PROTO_IPV4_BE)  { return; }
    if (pkt->hw_len    != 6u)                  { return; }
    if (pkt->proto_len != 4u)                  { return; }

    /*
     * Cache the sender MAC whenever we see a packet from 10.0.2.2.
     * This works for both ARP requests originating from the host
     * and for gratuitous ARPs.
     */
    if (pkt->sender_ip == HOST_IP) {
        uint8_t changed = 0;
        for (uint32_t i = 0; i < 6u; i++) {
            if (g_host_mac[i] != pkt->sender_mac[i]) {
                changed = 1;
            }
            g_host_mac[i] = pkt->sender_mac[i];
        }
        if (!g_host_mac_valid || changed) {
            g_host_mac_valid = 1;
            HAL_UART_PutString("[ARP ] learned host MAC ");
            for (uint32_t i = 0; i < 6u; i++) {
                arp_put_hex8(g_host_mac[i]);
                if (i < 5u) HAL_UART_PutChar(':');
            }
            HAL_UART_PutString("\n");
        }
    }

    /* Only reply to requests directed at our IP */
    if (pkt->operation != ARP_OP_REQUEST_BE)  { return; }
    if (pkt->target_ip != GUEST_IP)           { return; }

    HAL_UART_PutString("[ARP ] received ARP request for our IP — replying\n");

    /* Build ARP reply in a local static buffer (no stack size issue) */
    static ArpPkt_t reply;

    reply.hw_type    = ARP_HW_ETH_BE;
    reply.proto_type = ARP_PROTO_IPV4_BE;
    reply.hw_len     = 6u;
    reply.proto_len  = 4u;
    reply.operation  = ARP_OP_REPLY_BE;

    /* sender = us */
    uint8_t our_mac[6];
    ETH_GetMAC(our_mac);
    for (uint32_t i = 0; i < 6u; i++) {
        reply.sender_mac[i] = our_mac[i];
    }
    reply.sender_ip = GUEST_IP;

    /* target = whoever asked */
    for (uint32_t i = 0; i < 6u; i++) {
        reply.target_mac[i] = pkt->sender_mac[i];
    }
    reply.target_ip = pkt->sender_ip;

    /* Send via Ethernet layer (unicast back to requester) */
    uint8_t dst[6];
    for (uint32_t i = 0; i < 6u; i++) {
        dst[i] = pkt->sender_mac[i];
    }
    ETH_Send(dst, ETH_TYPE_ARP, (uint8_t *)&reply, (uint16_t)sizeof(ArpPkt_t));

    HAL_UART_PutString("[ARP ] reply sent\n");
}

/* ------------------------------------------------------------------ */
/*  ARP_GetHostMAC                                                   */
/* ------------------------------------------------------------------ */

void ARP_GetHostMAC(uint8_t mac[6])
{
    for (uint32_t i = 0; i < 6u; i++) {
        mac[i] = g_host_mac[i];
    }
}
