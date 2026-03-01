/**
 * @file eth_driver.h
 * @brief Ethernet hardware abstraction for MiniOS-NetProtocol
 *
 * Abstracts the physical Ethernet controller so that the RUDP layer
 * is portable across:
 *   • QEMU virtio-net (development / QEMU virt machine)
 *   • SMSC LAN9118 (Raspberry Pi bare-metal / Versatile Express)
 *   • Generic MMIO Ethernet (future hardware targets)
 *
 * All send/receive paths avoid dynamic allocation — buffers are
 * caller-supplied per the DC-002 development constraint.
 */

#ifndef MINIOS_ETH_DRIVER_H
#define MINIOS_ETH_DRIVER_H

#include "../net/net_types.h"

/* ------------------------------------------------------------------ */
/*  Driver selection (compile-time via Makefile -DETH_DRIVER_xxx)     */
/* ------------------------------------------------------------------ */
#ifndef ETH_DRIVER_VIRTIO
  #ifndef ETH_DRIVER_LAN9118
    /* Default: QEMU virtio-net for development */
    #define ETH_DRIVER_VIRTIO  1
  #endif
#endif

/* QEMU virtio-net MMIO base (virt machine) */
#define VIRTIO_NET_BASE     0x0A003E00UL
/* LAN9118 base address (Versatile Express / RPi variants) */
#define LAN9118_BASE        0x4E000000UL

/* Maximum raw frame size (ETH header + MTU) */
#define ETH_FRAME_MAX       (ETH_HDR_LEN + ETH_MTU)

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the Ethernet controller.
 *
 * Configures the MAC address, enables Tx/Rx, clears interrupt flags.
 * Must be called once after MMU/cache initialization.
 *
 * @param[in] mac  6-byte MAC address to assign
 * @return STATUS_OK on success
 */
Status ETH_Init(const uint8_t mac[ETH_ALEN]);

/**
 * @brief Send a raw Ethernet frame.
 *
 * @param[in] frame   Pointer to raw frame bytes (ETH header + payload)
 * @param[in] len     Total frame length (bytes)
 * @return STATUS_OK on success, STATUS_ERROR_TIMEOUT if Tx FIFO full
 */
Status ETH_Send(const uint8_t *frame, uint16_t len);

/**
 * @brief Receive a raw Ethernet frame (non-blocking).
 *
 * @param[out] frame   Destination buffer (must be >= ETH_FRAME_MAX bytes)
 * @param[out] len     Number of bytes received
 * @return STATUS_OK if a frame was received, STATUS_ERROR_TIMEOUT if none
 */
Status ETH_Recv(uint8_t *frame, uint16_t *len);

/**
 * @brief Get this node's MAC address (set during ETH_Init).
 * @param[out] mac  6-byte buffer filled with MAC address
 */
void ETH_GetMac(uint8_t mac[ETH_ALEN]);

/**
 * @brief Return true if Tx FIFO has space for at least one frame.
 */
bool ETH_TxReady(void);

/**
 * @brief Return true if Rx FIFO has a pending frame.
 */
bool ETH_RxAvailable(void);

#endif /* MINIOS_ETH_DRIVER_H */