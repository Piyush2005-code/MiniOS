/**
 * @file ethernet.c
 * @brief Ethernet II frame layer implementation for MiniOS
 *
 * Sits directly above the VirtIO-Net MMIO driver. Registers
 * ETH_Receive() as the VNIC rx_callback and dispatches received
 * frames to the ARP or IPv4 layers based on EtherType.
 *
 * TX path uses a single static staging buffer; no dynamic allocation.
 *
 * @note Per SRS FR-NET-ETH
 *
 * @complexity
 *   ETH_Send    : O(payload_len) — frame copy
 *   ETH_Receive : O(1) — parse + dispatch
 */

#include "net/ethernet.h"
#include "net/arp.h"
#include "net/ipv4.h"
#include "drivers/virtio_net.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Byte order helpers                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Swap bytes of a 16-bit value (host ↔ network order on LE ARM)
 *
 * ARM64 is little-endian; all network protocol fields are big-endian.
 * This inline performs the byte swap in a single instruction path.
 */
static inline uint16_t eth_htons(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

/* ------------------------------------------------------------------ */
/*  Module-private state                                             */
/* ------------------------------------------------------------------ */

/** Our MAC address, read from the VirtIO-Net device config space */
static uint8_t g_our_mac[6];

/**
 * @brief Static TX staging buffer
 *
 * Holds one outgoing Ethernet frame at a time: 14-byte header +
 * up to 1500-byte payload = 1514 bytes maximum (no FCS).
 */
static uint8_t eth_tx_buf[ETH_FRAME_MAX];

/* ------------------------------------------------------------------ */
/*  Internal: UART hex helpers                                       */
/* ------------------------------------------------------------------ */

/** Print a single hex nibble to UART */
static void eth_put_nibble(uint8_t n)
{
    n &= 0xFu;
    HAL_UART_PutChar((char)(n < 10u ? (char)('0' + (char)n) : (char)('a' + (char)n - 10)));
}

/** Print a byte as two uppercase hex digits to UART */
static void eth_put_hex8(uint8_t v)
{
    eth_put_nibble(v >> 4);
    eth_put_nibble(v);
}

/* ------------------------------------------------------------------ */
/*  ETH_Init                                                         */
/* ------------------------------------------------------------------ */

void ETH_Init(void)
{
    /* Register our frame receive callback with the VirtIO-Net driver */
    VNIC_Init(ETH_Receive);

    /* Cache our MAC from the NIC */
    VNIC_GetMAC(g_our_mac);

    /* Print init banner with MAC */
    HAL_UART_PutString("[ETH ] init ok, MAC ");
    for (uint32_t i = 0; i < 6u; i++) {
        eth_put_hex8(g_our_mac[i]);
        if (i < 5u) {
            HAL_UART_PutChar(':');
        }
    }
    HAL_UART_PutString("\n");
}

/* ------------------------------------------------------------------ */
/*  ETH_Send                                                         */
/* ------------------------------------------------------------------ */

int ETH_Send(uint8_t dst_mac[6], uint16_t ethertype,
             uint8_t *payload, uint16_t payload_len)
{
    if (payload_len > ETH_MTU) {
        HAL_UART_PutString("[ETH ] ERROR: ETH_Send: payload too large\n");
        return -1;
    }

    /* Build Ethernet II header in the staging buffer */
    EthHdr_t *hdr = (EthHdr_t *)eth_tx_buf;

    for (uint32_t i = 0; i < 6u; i++) {
        hdr->dst_mac[i] = dst_mac[i];
        hdr->src_mac[i] = g_our_mac[i];
    }
    hdr->ethertype = eth_htons(ethertype);

    /* Copy payload immediately after the header */
    uint8_t *dst = eth_tx_buf + sizeof(EthHdr_t);
    const uint8_t *src = payload;
    for (uint16_t i = 0; i < payload_len; i++) {
        dst[i] = src[i];
    }

    uint16_t frame_len = (uint16_t)(sizeof(EthHdr_t) + payload_len);
    return VNIC_Send(eth_tx_buf, frame_len);
}

/* ------------------------------------------------------------------ */
/*  ETH_Receive                                                      */
/* ------------------------------------------------------------------ */

void ETH_Receive(uint8_t *frame, uint16_t len)
{
    /* Need at least a full Ethernet header */
    if (len < (uint16_t)sizeof(EthHdr_t)) {
        return;
    }

    const EthHdr_t *hdr = (const EthHdr_t *)frame;

    /* Payload starts after the 14-byte header */
    uint8_t  *payload     = frame + sizeof(EthHdr_t);
    uint16_t  payload_len = (uint16_t)(len - (uint16_t)sizeof(EthHdr_t));

    /* Decode EtherType (big-endian → host order) */
    uint16_t etype = eth_htons(hdr->ethertype);

    switch (etype) {
        case ETH_TYPE_ARP:
            ARP_Receive(payload, payload_len);
            break;

        case ETH_TYPE_IPV4:
            IPV4_Receive(payload, payload_len);
            break;

        default:
            /* Unknown EtherType — silently drop */
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  ETH_GetMAC                                                       */
/* ------------------------------------------------------------------ */

void ETH_GetMAC(uint8_t mac[6])
{
    for (uint32_t i = 0; i < 6u; i++) {
        mac[i] = g_our_mac[i];
    }
}
