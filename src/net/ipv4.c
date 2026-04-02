/**
 * @file ipv4.c
 * @brief IPv4 packet layer implementation for MiniOS
 *
 * Handles building and parsing of fixed 20-byte IPv4 headers
 * (IHL=5, no options). Only UDP (protocol 17) is dispatched upward;
 * all other protocols are silently dropped.
 *
 * TX path: builds header, computes checksum, calls ETH_Send().
 * RX path: validates header then calls UDP_Receive().
 *
 * No fragmentation, no reassembly, no ICMP, no routing table.
 * All outbound packets use the host MAC obtained from ARP_GetHostMAC().
 *
 * @note Per SRS FR-NET-IPv4
 *
 * @complexity
 *   IPV4_Send     : O(payload_len) — header build + copy
 *   IPV4_Receive  : O(1) — parse + validate + dispatch
 *   IPV4_Checksum : O(IHL) — fixed 20-byte header → O(10)
 */

#include "net/ipv4.h"
#include "net/ethernet.h"
#include "net/arp.h"
#include "net/udp.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Byte order helpers                                               */
/* ------------------------------------------------------------------ */

/** Swap bytes of a 16-bit value (LE host ↔ network big-endian) */
static inline uint16_t ip_htons(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

/* ntohs is identical to htons for byte-swap only */
#define ip_ntohs  ip_htons

/* ------------------------------------------------------------------ */
/*  Module-private state                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Monotonically increasing packet identification counter
 *
 * Incremented for each outgoing IPv4 packet. Stored in host byte order;
 * written to the header via htons().
 */
static uint16_t g_ip_id;

/**
 * @brief Static IPv4 TX staging buffer
 *
 * Holds one outgoing IPv4 datagram (header + payload) before handing
 * it to ETH_Send(). Max size = 20-byte header + 1480-byte UDP payload.
 */
static uint8_t ipv4_tx_buf[1500];

/* ------------------------------------------------------------------ */
/*  IPV4_Checksum                                                    */
/* ------------------------------------------------------------------ */

uint16_t IPV4_Checksum(void *hdr, uint16_t len)
{
    const uint16_t *words = (const uint16_t *)hdr;
    uint32_t sum = 0;
    uint16_t count = (uint16_t)(len / 2u);

    for (uint16_t i = 0; i < count; i++) {
        sum += words[i];
    }

    /* Fold 32-bit sum to 16 bits: add any carry bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

/* ------------------------------------------------------------------ */
/*  IPV4_Init                                                        */
/* ------------------------------------------------------------------ */

void IPV4_Init(void)
{
    g_ip_id = 1u;
    HAL_UART_PutString("[IPV4] init ok\n");
}

/* ------------------------------------------------------------------ */
/*  IPV4_Send                                                        */
/* ------------------------------------------------------------------ */

int IPV4_Send(uint32_t dst_ip, uint8_t protocol,
              uint8_t *payload, uint16_t payload_len)
{
    /* Enforce MTU: IPv4 header (20) + payload must fit in 1500 bytes */
    if ((uint32_t)payload_len + 20u > 1500u) {
        HAL_UART_PutString("[IPV4] ERROR: payload too large for MTU\n");
        return -1;
    }

    /* Build IPv4 header at the start of the staging buffer */
    IPv4Hdr_t *hdr = (IPv4Hdr_t *)ipv4_tx_buf;

    hdr->ver_ihl    = 0x45u;                        /* IPv4, IHL=5 (20 bytes) */
    hdr->dscp_ecn   = 0u;
    hdr->total_len  = ip_htons((uint16_t)(20u + payload_len));
    hdr->id         = ip_htons(g_ip_id++);
    hdr->flags_frag = ip_htons(0x4000u);            /* DF bit set */
    hdr->ttl        = 64u;
    hdr->protocol   = protocol;
    hdr->checksum   = 0u;                           /* zero before computation */
    hdr->src_ip     = GUEST_IP;
    hdr->dst_ip     = dst_ip;

    /* Compute and fill header checksum */
    hdr->checksum = IPV4_Checksum(ipv4_tx_buf, 20u);

    /* Copy payload after the 20-byte header */
    uint8_t *pdst = ipv4_tx_buf + 20u;
    for (uint16_t i = 0; i < payload_len; i++) {
        pdst[i] = payload[i];
    }

    /* Resolve Ethernet destination: always the host/gateway */
    uint8_t dst_mac[6];
    ARP_GetHostMAC(dst_mac);

    uint16_t total = (uint16_t)(20u + payload_len);
    return ETH_Send(dst_mac, ETH_TYPE_IPV4, ipv4_tx_buf, total);
}

/* ------------------------------------------------------------------ */
/*  IPV4_Receive                                                     */
/* ------------------------------------------------------------------ */

void IPV4_Receive(uint8_t *buf, uint16_t len)
{
    /* Need at least a 20-byte header */
    if (len < 20u) {
        return;
    }

    const IPv4Hdr_t *hdr = (const IPv4Hdr_t *)buf;

    /* Accept only IPv4 with IHL=5 (no options) */
    if (hdr->ver_ihl != 0x45u) {
        return;
    }

    /* Verify the packet is addressed to us */
    if (hdr->dst_ip != GUEST_IP) {
        return;
    }

    /*
     * Validate header checksum.
     * IPV4_Checksum() over a correct header (with its checksum field
     * included) should yield 0x0000 because the one's complement sum
     * of all 16-bit words including the checksum word equals 0xFFFF,
     * and the function returns ~0xFFFF == 0x0000.
     */
    uint16_t saved = hdr->checksum;
    (void)saved;      /* used only for documentation clarity */

    /*
     * To verify without mutating the receive buffer (which may be
     * a DMA buffer):  compute checksum of the header as-is; if the
     * header is valid the result is 0x0000.
     */
    uint16_t cksum = IPV4_Checksum(buf, 20u);
    if (cksum != 0x0000u) {
        HAL_UART_PutString("[IPV4] dropped: bad checksum\n");
        return;
    }

    /* Compute payload start and length */
    uint16_t total_len   = ip_ntohs(hdr->total_len);
    if (total_len < 20u || total_len > len) {
        return;
    }
    uint8_t  *payload    = buf + 20u;
    uint16_t  payload_len = (uint16_t)(total_len - 20u);

    /* Dispatch to upper layer */
    if (hdr->protocol == IP_PROTO_UDP) {
        UDP_Receive(hdr->src_ip, payload, payload_len);
    }
    /* All other protocols are silently dropped */
}
