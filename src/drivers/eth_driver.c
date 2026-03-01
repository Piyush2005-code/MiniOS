/**
 * @file eth_driver.c
 * @brief Ethernet driver for MiniOS-NetProtocol
 *
 * Implements ETH_Init / ETH_Send / ETH_Recv for:
 *   ETH_DRIVER_VIRTIO  — QEMU virtio-net MMIO (default, development)
 *   ETH_DRIVER_LAN9118 — SMSC LAN9118 (Versatile Express / RPi bare-metal)
 *
 * Selected at compile time via Makefile:
 *   make ETH_DRIVER=virtio   (default)
 *   make ETH_DRIVER=lan9118
 *
 * Register-level access uses REG32() macro from types.h — no libc needed.
 */

#include "../include/net/eth_driver.h"

/* ===================================================================
 * COMMON helpers
 * =================================================================== */

static uint8_t s_mac[ETH_ALEN];

void ETH_GetMac(uint8_t mac[ETH_ALEN])
{
    for (int i = 0; i < ETH_ALEN; i++) mac[i] = s_mac[i];
}

/* ===================================================================
 * VIRTIO-NET (QEMU 'virt' machine MMIO transport)
 * =================================================================== */
#ifdef ETH_DRIVER_VIRTIO

/*
 * Simplified virtio-net MMIO interface.
 * This implements the subset needed for bare-metal use:
 *   - No virtqueue interrupt (polling mode)
 *   - Single Rx / Tx queue
 *   - Fixed 1500-byte MTU
 *
 * Reference: virtio spec 1.1 section 5.1 (network device)
 *            https://docs.oasis-open.org/virtio/virtio/v1.1/
 */

#define VIRTIO_MMIO_BASE        VIRTIO_NET_BASE

/* Virtio MMIO register offsets */
#define VIRTIO_MMIO_MAGIC           0x000  /* Must read 0x74726976 */
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008  /* 1 = network */
#define VIRTIO_MMIO_VENDOR_ID       0x00C
#define VIRTIO_MMIO_HOST_FEATURES   0x010
#define VIRTIO_MMIO_HOST_FEAT_SEL   0x014
#define VIRTIO_MMIO_GUEST_FEATURES  0x020
#define VIRTIO_MMIO_GUEST_FEAT_SEL  0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03C
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STAT  0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070

/* Status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8

/* Feature bits (net device) */
#define VIRTIO_NET_F_MAC    (1 << 5)

/* Virtio net config (MAC address starts at offset 0x100 + 0) */
#define VIRTIO_NET_CONFIG_MAC   (VIRTIO_MMIO_BASE + 0x100)

/* Simple poll-mode ring buffer (single descriptor) */
#define RING_SIZE   16
static uint8_t s_rx_ring[RING_SIZE][ETH_FRAME_MAX + 12]; /* +12 virtio hdr */
static uint8_t s_tx_buf[ETH_FRAME_MAX + 12];
static uint8_t s_rx_head = 0;
static uint8_t s_rx_tail = 0;

Status ETH_Init(const uint8_t mac[ETH_ALEN])
{
    /* Verify magic */
    uint32_t magic = REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_MAGIC);
    if (magic != 0x74726976) return STATUS_ERROR_HARDWARE_FAULT;

    /* Check device ID = 1 (net) */
    if (REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_DEVICE_ID) != 1)
        return STATUS_ERROR_HARDWARE_FAULT;

    /* Acknowledge + driver */
    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_STATUS) =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

    /* Read features — accept MAC feature */
    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_HOST_FEAT_SEL) = 0;
    uint32_t features = REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_HOST_FEATURES);
    features &= VIRTIO_NET_F_MAC;
    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_GUEST_FEAT_SEL) = 0;
    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_GUEST_FEATURES) = features;

    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_FEATURES_OK;

    /* Read MAC from device config */
    volatile uint8_t *cfg = (volatile uint8_t *)VIRTIO_NET_CONFIG_MAC;
    for (int i = 0; i < ETH_ALEN; i++) s_mac[i] = cfg[i];
    /* Override with requested MAC if provided */
    if (mac) {
        for (int i = 0; i < ETH_ALEN; i++) {
            s_mac[i] = mac[i];
            /* Write back — some virtio implementations allow this */
        }
    }

    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER_OK;

    s_rx_head = 0;
    s_rx_tail = 0;

    return STATUS_OK;
}

Status ETH_Send(const uint8_t *frame, uint16_t len)
{
    if (!frame || len == 0 || len > ETH_FRAME_MAX + 2) 
        return STATUS_ERROR_INVALID_ARGUMENT;

    /* Virtio-net header (10 bytes, all zeros for basic TX) */
    for (int i = 0; i < 10; i++) s_tx_buf[i] = 0;
    for (uint16_t i = 0; i < len; i++) s_tx_buf[10 + i] = frame[i];

    /* Notify queue 0 (Tx) — in a real driver this uses descriptors.
     * For QEMU poll mode, we write to QUEUE_NOTIFY. */
    REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_QUEUE_NOTIFY) = 1; /* queue idx 1 = Tx */

    return STATUS_OK;
}

Status ETH_Recv(uint8_t *frame, uint16_t *len)
{
    if (!frame || !len) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Poll Rx queue (queue 0) */
    if (s_rx_head == s_rx_tail) {
        /* No frame available */
        REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_QUEUE_NOTIFY) = 0; /* kick Rx */
        return STATUS_ERROR_TIMEOUT;
    }

    /* Skip virtio-net header (10 bytes) and copy to caller */
    uint8_t *buf = s_rx_ring[s_rx_head % RING_SIZE];
    uint16_t frame_len = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    if (frame_len > ETH_FRAME_MAX + 2) frame_len = ETH_FRAME_MAX + 2;

    for (uint16_t i = 0; i < frame_len; i++) frame[i] = buf[10 + i];
    *len = frame_len;
    s_rx_head++;

    /* Acknowledge interrupt */
    uint32_t isr = REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_INTERRUPT_STAT);
    if (isr) REG32(VIRTIO_MMIO_BASE + VIRTIO_MMIO_INTERRUPT_ACK) = isr;

    return STATUS_OK;
}

bool ETH_TxReady(void)
{
    return true; /* polling mode always ready */
}

bool ETH_RxAvailable(void)
{
    return (s_rx_head != s_rx_tail);
}

#endif /* ETH_DRIVER_VIRTIO */

/* ===================================================================
 * LAN9118 (SMSC LAN9118 — Versatile Express / RPi bare-metal)
 * =================================================================== */
#ifdef ETH_DRIVER_LAN9118

#define LAN9118_ID_REV          (LAN9118_BASE + 0x50)
#define LAN9118_INT_STS         (LAN9118_BASE + 0x54)
#define LAN9118_TX_CFG          (LAN9118_BASE + 0x70)
#define LAN9118_TX_STATUS_FIFO  (LAN9118_BASE + 0x48)
#define LAN9118_TX_DATA_FIFO    (LAN9118_BASE + 0x20)
#define LAN9118_RX_CFG          (LAN9118_BASE + 0x6C)
#define LAN9118_RX_STATUS_FIFO  (LAN9118_BASE + 0x40)
#define LAN9118_RX_DATA_FIFO    (LAN9118_BASE + 0x00)
#define LAN9118_TX_FIFO_INF     (LAN9118_BASE + 0x7C)
#define LAN9118_MAC_CSR_CMD     (LAN9118_BASE + 0xA4)
#define LAN9118_MAC_CSR_DATA    (LAN9118_BASE + 0xA8)

/* MAC CSR register indices */
#define LAN9118_MAC_CR      1
#define LAN9118_MAC_ADDRH   2
#define LAN9118_MAC_ADDRL   3

static void lan9118_mac_write(uint32_t reg, uint32_t val)
{
    REG32(LAN9118_MAC_CSR_DATA) = val;
    REG32(LAN9118_MAC_CSR_CMD) = (1u << 31) | (1u << 30) | (reg & 0xFF);
    /* Busy-wait */
    while (REG32(LAN9118_MAC_CSR_CMD) & (1u << 31)) {}
}

static uint32_t lan9118_mac_read(uint32_t reg)
{
    REG32(LAN9118_MAC_CSR_CMD) = (1u << 31) | (reg & 0xFF);
    while (REG32(LAN9118_MAC_CSR_CMD) & (1u << 31)) {}
    return REG32(LAN9118_MAC_CSR_DATA);
}

Status ETH_Init(const uint8_t mac[ETH_ALEN])
{
    /* Check chip ID */
    uint32_t id = REG32(LAN9118_ID_REV);
    if ((id & 0xFFFF0000) != 0x01180000 &&
        (id & 0xFFFF0000) != 0x01170000) {
        return STATUS_ERROR_HARDWARE_FAULT;
    }

    /* Set MAC address */
    for (int i = 0; i < ETH_ALEN; i++) s_mac[i] = mac[i];

    uint32_t addrh = ((uint32_t)mac[5] <<  8) | mac[4];
    uint32_t addrl = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                     ((uint32_t)mac[1] <<  8) |  mac[0];
    lan9118_mac_write(LAN9118_MAC_ADDRH, addrh);
    lan9118_mac_write(LAN9118_MAC_ADDRL, addrl);

    /* Enable Tx/Rx */
    uint32_t cr = lan9118_mac_read(LAN9118_MAC_CR);
    cr |= (1u << 3) | (1u << 2);  /* TXEN | RXEN */
    lan9118_mac_write(LAN9118_MAC_CR, cr);

    /* Flush Tx status FIFO */
    REG32(LAN9118_TX_CFG) = (1u << 2); /* TXSAO */

    return STATUS_OK;
}

Status ETH_Send(const uint8_t *frame, uint16_t len)
{
    if (!frame || len == 0) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Check Tx FIFO free space */
    uint32_t fifo_inf = REG32(LAN9118_TX_FIFO_INF);
    uint32_t free_space = (fifo_inf & 0xFFFF) * 4;
    if (free_space < len + 8) return STATUS_ERROR_TIMEOUT;

    /* Write Tx command A and B */
    uint32_t cmd_a = (1u << 13) | (1u << 12) | (len & 0x7FF); /* first+last segment */
    uint32_t cmd_b = len & 0x7FF;
    REG32(LAN9118_TX_DATA_FIFO) = cmd_a;
    REG32(LAN9118_TX_DATA_FIFO) = cmd_b;

    /* Write frame data (word-aligned) */
    uint16_t words = (len + 3) / 4;
    const uint32_t *p32 = (const uint32_t *)frame;
    for (uint16_t i = 0; i < words; i++) {
        REG32(LAN9118_TX_DATA_FIFO) = p32[i];
    }

    return STATUS_OK;
}

Status ETH_Recv(uint8_t *frame, uint16_t *len)
{
    if (!frame || !len) return STATUS_ERROR_INVALID_ARGUMENT;

    uint32_t status = REG32(LAN9118_RX_STATUS_FIFO);
    if (status == 0xFFFFFFFF || (status & 0x3FFF) == 0) {
        return STATUS_ERROR_TIMEOUT;
    }

    uint16_t pkt_len = (uint16_t)((status >> 16) & 0x3FFF);
    if (pkt_len > ETH_FRAME_MAX + 2) {
        /* Flush oversized packet */
        uint16_t words = (pkt_len + 3) / 4;
        for (uint16_t i = 0; i < words; i++) REG32(LAN9118_RX_DATA_FIFO);
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    uint32_t *p32 = (uint32_t *)frame;
    uint16_t words = (pkt_len + 3) / 4;
    for (uint16_t i = 0; i < words; i++) p32[i] = REG32(LAN9118_RX_DATA_FIFO);
    *len = pkt_len;

    return STATUS_OK;
}

bool ETH_TxReady(void)
{
    uint32_t fifo_inf = REG32(LAN9118_TX_FIFO_INF);
    return ((fifo_inf & 0xFFFF) * 4) >= (ETH_FRAME_MAX + 8);
}

bool ETH_RxAvailable(void)
{
    uint32_t status = REG32(LAN9118_RX_STATUS_FIFO);
    return (status != 0xFFFFFFFF && (status & 0x3FFF) != 0);
}

#endif /* ETH_DRIVER_LAN9118 */