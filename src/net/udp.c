/**
 * @file udp.c
 * @brief UDP layer implementation for MiniOS
 *
 * Provides stateless UDP send/receive with port-based demultiplexing.
 * A static binding table of up to UDP_MAX_HANDLERS entries maps
 * destination ports to handler callbacks.
 *
 * Checksum is disabled on TX (checksum = 0) which is valid per
 * RFC 768 for local/loopback-like SLIRP communication.
 *
 * @note Per SRS FR-NET-UDP
 *
 * @complexity
 *   UDP_Bind    : O(UDP_MAX_HANDLERS) — linear scan for free slot
 *   UDP_Send    : O(len) — header build + payload copy
 *   UDP_Receive : O(UDP_MAX_HANDLERS) — linear port search
 */

#include "net/udp.h"
#include "net/ipv4.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Byte order helpers                                               */
/* ------------------------------------------------------------------ */

/** Swap bytes of a 16-bit value (LE host ↔ network big-endian) */
static inline uint16_t udp_htons(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

/* ntohs is identical to htons for byte-swap only */
#define udp_ntohs  udp_htons

/* ------------------------------------------------------------------ */
/*  Module-private state                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Port-to-handler binding table
 *
 * Statically allocated — no dynamic memory. UDP_MAX_HANDLERS = 8
 * entries is sufficient for a minimal inference-serving stack.
 */
static UDPBinding_t g_bindings[UDP_MAX_HANDLERS];

/**
 * @brief Static UDP TX staging buffer
 *
 * Holds one outgoing UDP datagram (8-byte header + payload) before
 * handing it to IPV4_Send(). Max payload = 1500 - 20 - 8 = 1472 bytes.
 */
static uint8_t udp_tx_buf[1500];

/* ------------------------------------------------------------------ */
/*  UDP_Init                                                         */
/* ------------------------------------------------------------------ */

void UDP_Init(void)
{
    for (uint32_t i = 0; i < UDP_MAX_HANDLERS; i++) {
        g_bindings[i].port    = 0u;
        g_bindings[i].handler = (udp_handler_t)0;
        g_bindings[i].in_use  = 0u;
    }
    HAL_UART_PutString("[UDP ] init ok\n");
}

/* ------------------------------------------------------------------ */
/*  UDP_Bind                                                         */
/* ------------------------------------------------------------------ */

int UDP_Bind(uint16_t port, udp_handler_t handler)
{
    for (uint32_t i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (!g_bindings[i].in_use) {
            g_bindings[i].port    = port;
            g_bindings[i].handler = handler;
            g_bindings[i].in_use  = 1u;

            HAL_UART_PutString("[UDP ] bound port ");
            HAL_UART_PutDec(port);
            HAL_UART_PutString("\n");
            return 0;
        }
    }

    HAL_UART_PutString("[UDP ] ERROR: binding table full\n");
    return -1;
}

/* ------------------------------------------------------------------ */
/*  UDP_Send                                                         */
/* ------------------------------------------------------------------ */

int UDP_Send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
             uint8_t *payload, uint16_t len)
{
    /*
     * Maximum UDP payload: IP MTU (1500) - IP header (20) - UDP header (8)
     * = 1472 bytes.
     */
    if ((uint32_t)len + 8u + 20u > 1500u) {
        HAL_UART_PutString("[UDP ] ERROR: UDP_Send: payload too large\n");
        return -1;
    }

    /* Build UDP header at the start of the staging buffer */
    UDPHdr_t *hdr = (UDPHdr_t *)udp_tx_buf;

    hdr->src_port = udp_htons(src_port);
    hdr->dst_port = udp_htons(dst_port);
    hdr->length   = udp_htons((uint16_t)(8u + len));
    hdr->checksum = 0u;  /* disabled — valid per RFC 768 */

    /* Copy payload immediately after the 8-byte UDP header */
    uint8_t *dst = udp_tx_buf + sizeof(UDPHdr_t);
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = payload[i];
    }

    uint16_t total = (uint16_t)(sizeof(UDPHdr_t) + len);
    return IPV4_Send(dst_ip, IP_PROTO_UDP, udp_tx_buf, total);
}

/* ------------------------------------------------------------------ */
/*  UDP_Receive                                                      */
/* ------------------------------------------------------------------ */

void UDP_Receive(uint32_t src_ip, uint8_t *buf, uint16_t len)
{
    /* Require at least an 8-byte UDP header */
    if (len < (uint16_t)sizeof(UDPHdr_t)) {
        return;
    }

    const UDPHdr_t *hdr = (const UDPHdr_t *)buf;

    /* Convert ports to host byte order for table lookup */
    uint16_t dst_port = udp_ntohs(hdr->dst_port);
    uint16_t src_port = udp_ntohs(hdr->src_port);

    /* Payload starts after the 8-byte header */
    uint8_t  *payload     = buf + sizeof(UDPHdr_t);
    uint16_t  payload_len = (uint16_t)(len - (uint16_t)sizeof(UDPHdr_t));

    /* Linear search for a matching binding */
    for (uint32_t i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_bindings[i].in_use && g_bindings[i].port == dst_port) {
            g_bindings[i].handler(src_ip, src_port, payload, payload_len);
            return;
        }
    }

    /* No handler registered — silently drop */
}
