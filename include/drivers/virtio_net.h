/**
 * @file virtio_net.h
 * @brief VirtIO-Net MMIO NIC driver for MiniOS
 *
 * Implements a minimal VirtIO 1.0 network device driver over the
 * MMIO transport, targeting QEMU's virtio-net-device on the ARM
 * virt machine. Performs top-down discovery of the VirtIO-Net slot
 * in the QEMU virt MMIO window (0x0a000000 – 0x0a003eff).
 *
 * The MMIO region is already mapped Device-nGnRnE by mmu.c
 * (L1 entry 0 covers 0x00000000 – 0x3FFFFFFF) so no additional
 * MMU work is required.
 *
 * Network parameters (QEMU SLIRP):
 *   Guest IP  : 10.0.2.15
 *   Gateway   : 10.0.2.2
 *   UDP port  : 9000 (host port 9000 forwarded to guest port 9000)
 *
 * @note Per SRS FR-NIC-001
 *
 * @complexity
 *   VNIC_Discover : O(32) — fixed scan of 32 MMIO slots
 *   VNIC_Init     : O(VNET_QUEUE_SIZE) — fills RX ring
 *   VNIC_Send     : O(1)
 *   VNIC_Poll     : O(k) where k = newly completed descriptors
 */

#ifndef MINIOS_DRIVERS_VIRTIO_NET_H
#define MINIOS_DRIVERS_VIRTIO_NET_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  VirtIO MMIO Register Offsets (VirtIO 1.0 Spec §4.2.2)            */
/* ------------------------------------------------------------------ */

#define VIRTIO_MMIO_MAGIC_VALUE      0x000   /**< Must be 0x74726976 */
#define VIRTIO_MMIO_VERSION          0x004   /**< Version (1 = legacy, 2 = modern) */
#define VIRTIO_MMIO_DEVICE_ID        0x008   /**< Device type (1 = net) */
#define VIRTIO_MMIO_VENDOR_ID        0x00C   /**< Vendor ID */
#define VIRTIO_MMIO_DEVICE_FEATURES  0x010   /**< Device feature bits (read) */
#define VIRTIO_MMIO_DRIVER_FEATURES  0x020   /**< Driver feature bits (write) */
#define VIRTIO_MMIO_QUEUE_SEL        0x030   /**< Queue selector (write) */
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034   /**< Max queue size (read) */
#define VIRTIO_MMIO_QUEUE_NUM        0x038   /**< Queue size to use (write) */
#define VIRTIO_MMIO_QUEUE_READY      0x044   /**< Queue ready flag (write 1) */
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050   /**< Queue notification (write) */
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060   /**< Interrupt reason bitmask (read) */
#define VIRTIO_MMIO_INTERRUPT_ACK    0x064   /**< Interrupt acknowledge (write) */
#define VIRTIO_MMIO_STATUS           0x070   /**< Device status register */
#define VIRTIO_MMIO_QUEUE_DESC_LOW   0x080   /**< Descriptor table PA[31:0] */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH  0x084   /**< Descriptor table PA[63:32] */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW  0x090   /**< Available ring PA[31:0] */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094   /**< Available ring PA[63:32] */
#define VIRTIO_MMIO_QUEUE_USED_LOW   0x0A0   /**< Used ring PA[31:0] */
#define VIRTIO_MMIO_QUEUE_USED_HIGH  0x0A4   /**< Used ring PA[63:32] */
#define VIRTIO_MMIO_CONFIG           0x100   /**< Device-specific config space */

/* ------------------------------------------------------------------ */
/*  VirtIO Device Status Bits (VirtIO 1.0 §2.1)                       */
/* ------------------------------------------------------------------ */

#define VIRTIO_STATUS_ACKNOWLEDGE    (1u << 0)  /**< OS knows device */
#define VIRTIO_STATUS_DRIVER         (1u << 1)  /**< Driver loaded */
#define VIRTIO_STATUS_DRIVER_OK      (1u << 2)  /**< Driver setup done */
#define VIRTIO_STATUS_FEATURES_OK    (1u << 3)  /**< Features negotiated */
#define VIRTIO_STATUS_FAILED         (1u << 7)  /**< Unrecoverable error */

/* ------------------------------------------------------------------ */
/*  VirtIO-Net Feature Bits (VirtIO 1.0 §5.1.3)                       */
/* ------------------------------------------------------------------ */

#define VIRTIO_NET_F_MAC             (1u << 5)  /**< MAC address available */

/* ------------------------------------------------------------------ */
/*  VirtIO-Net Queue Indices                                          */
/* ------------------------------------------------------------------ */

#define VNET_QUEUE_RX  0   /**< Receive queue */
#define VNET_QUEUE_TX  1   /**< Transmit queue */

/* ------------------------------------------------------------------ */
/*  VirtQueue configuration                                           */
/* ------------------------------------------------------------------ */

/** Number of descriptors per virtqueue — must be a power of 2 */
#define VNET_QUEUE_SIZE   16

/** Maximum Ethernet frame size (headers + payload + headroom) */
#define VNET_BUF_SIZE     1536

/* ------------------------------------------------------------------ */
/*  VirtQueue Descriptor (VirtIO 1.0 §2.6.5)                          */
/* ------------------------------------------------------------------ */

/**
 * @brief VirtQueue descriptor table entry
 *
 * Describes a single buffer passed to the device.
 * All fields are little-endian.
 */
typedef struct __attribute__((packed)) {
    uint64_t addr;    /**< Physical address of buffer */
    uint32_t len;     /**< Buffer length in bytes */
    uint16_t flags;   /**< Chaining / write-only flags */
    uint16_t next;    /**< Next descriptor index (if VRING_DESC_F_NEXT set) */
} VirtqDesc;

/** Chaining flag: descriptor has a next field */
#define VRING_DESC_F_NEXT    (1u << 0)
/** Write-only flag: device should write into this buffer */
#define VRING_DESC_F_WRITE   (1u << 1)

/* ------------------------------------------------------------------ */
/*  VirtQueue Available Ring (VirtIO 1.0 §2.6.6)                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Available ring — driver → device notification
 *
 * The driver writes descriptor indices here to tell the device
 * which buffers are ready to process.
 */
typedef struct __attribute__((packed)) {
    uint16_t flags;                  /**< Suppresses used-ring interrupts if 1 */
    uint16_t idx;                    /**< Next slot index (wraps mod 65536) */
    uint16_t ring[VNET_QUEUE_SIZE];  /**< Descriptor indices offered to device */
} VirtqAvail;

/* ------------------------------------------------------------------ */
/*  VirtQueue Used Ring (VirtIO 1.0 §2.6.8)                            */
/* ------------------------------------------------------------------ */

/** Single entry in the used ring returned by the device */
typedef struct __attribute__((packed)) {
    uint32_t id;   /**< Descriptor chain head index */
    uint32_t len;  /**< Bytes written by device */
} VirtqUsedElem;

/**
 * @brief Used ring — device → driver completion notification
 *
 * The device writes completed descriptor heads here after processing.
 */
typedef struct __attribute__((packed)) {
    uint16_t     flags;                  /**< Reserved */
    uint16_t     idx;                    /**< Next slot (wraps mod 65536) */
    VirtqUsedElem ring[VNET_QUEUE_SIZE]; /**< Completed entries */
} VirtqUsed;

/* ------------------------------------------------------------------ */
/*  VirtIO-Net Header (VirtIO 1.0 §5.1.6)                              */
/* ------------------------------------------------------------------ */

/**
 * @brief VirtIO-Net packet header
 *
 * Prepended to every TX and RX frame. Zero all fields for basic
 * (non-GSO, non-checksum) operation.
 * Size: 10 bytes (legacy mode, num_buffers present).
 */
typedef struct __attribute__((packed)) {
    uint8_t  flags;        /**< VIRTIO_NET_HDR_F_* flags (0 = none) */
    uint8_t  gso_type;     /**< GSO type (VIRTIO_NET_HDR_GSO_NONE = 0) */
    uint16_t hdr_len;      /**< GSO: ethernet+IP+TCP header length */
    uint16_t gso_size;     /**< GSO: desired MSS */
    uint16_t csum_start;   /**< Checksum start offset */
    uint16_t csum_offset;  /**< Checksum field offset within csum_start */
    uint16_t num_buffers;  /**< Number of merged RX buffers (legacy: 1) */
} VirtioNetHdr;

/* ------------------------------------------------------------------ */
/*  Driver State                                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief VirtIO-Net driver instance (singleton, statically allocated)
 *
 * Holds all virtqueue pointers, buffer pools, and device metadata.
 * Allocated statically — never on the stack.
 */
typedef struct {
    volatile uint8_t *base;   /**< MMIO mapped base address */
    uint32_t          irq;    /**< GIC INTID for this device */
    uint8_t           mac[6]; /**< MAC address read from config space */

    /* RX virtqueue (index 0) */
    VirtqDesc  *rx_desc;      /**< Descriptor table (VNET_QUEUE_SIZE entries) */
    VirtqAvail *rx_avail;     /**< Driver-to-device ring */
    VirtqUsed  *rx_used;      /**< Device-to-driver completion ring */
    uint16_t    rx_last_idx;  /**< Shadow of rx_used->idx at last poll */

    /* TX virtqueue (index 1) */
    VirtqDesc  *tx_desc;
    VirtqAvail *tx_avail;
    VirtqUsed  *tx_used;
    uint16_t    tx_last_idx;

    /* DMA-accessible frame buffers (identity-mapped, device memory) */
    uint8_t rx_buf[VNET_QUEUE_SIZE][VNET_BUF_SIZE]; /**< RX DMA buffers */
    uint8_t tx_buf[VNET_QUEUE_SIZE][VNET_BUF_SIZE]; /**< TX DMA buffers */

    /** Called by VNIC_Poll for every received Ethernet frame */
    void (*rx_callback)(uint8_t *frame, uint16_t len);
} VirtioNet_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Discover the VirtIO-Net MMIO base address
 *
 * Scans the QEMU virt MMIO window top-down (0x0a003e00 → 0x0a000000,
 * step –0x200) looking for the first slot that presents:
 *   - magic value 0x74726976 at offset 0x000
 *   - device ID 1 (network) at offset 0x008
 *
 * @return MMIO base address on success, 0 if not found
 */
uintptr_t VNIC_Discover(void);

/**
 * @brief Initialize the VirtIO-Net driver
 *
 * Performs full VirtIO device initialization:
 *   1. Discovers MMIO base and IRQ via VNIC_Discover()
 *   2. Resets and negotiates features with the device
 *   3. Allocates virtqueue memory via KMEM_Alloc
 *   4. Pre-fills the RX ring with receive buffers
 *   5. Reads the MAC address from config space
 *   6. Enables the device and registers the IRQ handler
 *
 * @param[in] rx_cb  Callback invoked for each received Ethernet frame.
 *                   Called from VNIC_Poll context (ISR or thread).
 *                   Parameters: frame pointer (after VirtioNetHdr),
 *                   frame length in bytes.
 *
 * @return  0 on success
 *         -1 on failure (discovery failure or feature negotiation error)
 */
int VNIC_Init(void (*rx_cb)(uint8_t *frame, uint16_t len));

/**
 * @brief Transmit a raw Ethernet frame
 *
 * Prepends a zeroed VirtioNetHdr, copies the frame into the next
 * available TX DMA buffer, and notifies the device.
 *
 * @param[in] frame  Pointer to raw Ethernet frame (starting at dst MAC)
 * @param[in] len    Frame length in bytes (must be <= VNET_BUF_SIZE - 10)
 *
 * @return  0 on success, -1 if TX ring is full
 */
int VNIC_Send(uint8_t *frame, uint16_t len);

/**
 * @brief Poll virtqueues for completed TX and received RX frames
 *
 * Should be called either from the VNIC_IRQHandler or periodically
 * from a network thread. Calls rx_callback for each received frame
 * and recycles RX descriptors back into the available ring.
 */
void VNIC_Poll(void);

/**
 * @brief VirtIO-Net IRQ handler — called from HAL_IRQ_Handler
 *
 * Reads and acknowledges INTERRUPT_STATUS then delegates to VNIC_Poll.
 * Must be registered in main.c's HAL_IRQ_Handler dispatch.
 */
void VNIC_IRQHandler(void);

/**
 * @brief Copy the device MAC address into a caller-supplied buffer
 *
 * @param[out] mac  6-byte buffer to receive the MAC address
 */
void VNIC_GetMAC(uint8_t mac[6]);

/**
 * @brief Return the discovered IRQ number (for external registration)
 * @return GIC INTID used by the NIC, or 0 if not initialized
 */
uint32_t VNIC_GetIRQ(void);

#endif /* MINIOS_DRIVERS_VIRTIO_NET_H */
