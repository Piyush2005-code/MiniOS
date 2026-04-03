/**
 * @file virtio_net.c
 * @brief VirtIO-Net MMIO NIC driver implementation for MiniOS
 *
 * Implements a minimal VirtIO 1.0 network device driver over the
 * MMIO transport. Targets QEMU `-device virtio-net-device,netdev=net0`
 * on the ARM virt machine with `-machine virt -cpu cortex-a53`.
 *
 * Memory layout assumed by this driver:
 *   Physical address == virtual address (identity-mapped, no IOMMU).
 *   VirtIO MMIO window (0x0a000000 – 0x0a003eff) falls inside L1
 *   entry 0 (0x00000000 – 0x3FFFFFFF), which mmu.c maps as
 *   Device-nGnRnE — no additional MMU configuration is required.
 *
 * VirtIO initialization sequence follows the spec (§3.1.1):
 *   Reset → ACKNOWLEDGE → DRIVER → feature negotiation →
 *   FEATURES_OK → virtqueue setup → DRIVER_OK
 *
 * @note Per SRS FR-NIC-001
 *
 * @complexity
 *   VNIC_Discover : O(32)               — bounded MMIO slot scan
 *   VNIC_Init     : O(VNET_QUEUE_SIZE)  — RX ring pre-fill
 *   VNIC_Send     : O(len)              — frame copy to DMA buffer
 *   VNIC_Poll     : O(k)               — k completed descriptors
 */

#include "drivers/virtio_net.h"
#include "hal/uart.h"
#include "hal/gic.h"
#include "kernel/kmem.h"
#include "lib/string.h"

/* ------------------------------------------------------------------ */
/*  MMIO Discovery Constants                                          */
/* ------------------------------------------------------------------ */

/** Top of the QEMU virt VirtIO MMIO window */
#define VIRT_MMIO_TOP    0x0a003e00UL
/** Bottom of the QEMU virt VirtIO MMIO window */
#define VIRT_MMIO_BOT    0x0a000000UL
/** Size of each MMIO slot */
#define VIRT_MMIO_STEP   0x200UL

/** VirtIO magic number ("virt" in little-endian ASCII) */
#define VIRTIO_MAGIC     0x74726976UL
/** VirtIO device ID for a network card */
#define VIRTIO_DEVID_NET 1UL

/* ------------------------------------------------------------------ */
/*  IRQ Calculation                                                   */
/* ------------------------------------------------------------------ */

/*
 * QEMU virt assigns GIC SPIs to MMIO slots linearly:
 *   SPI = 16 + slot_index
 *   GIC INTID = 32 + SPI = 32 + 16 + slot_index = 48 + slot_index
 *
 * Where slot_index = (base - VIRT_MMIO_BOT) / VIRT_MMIO_STEP
 * So slot 0 → INTID 48, slot 31 (0x0a003e00) → INTID 79.
 */
#define VNIC_IRQ_BASE    48UL

/* ------------------------------------------------------------------ */
/*  Singleton driver state                                            */
/* ------------------------------------------------------------------ */

static VirtioNet_t g_nic;

/* ------------------------------------------------------------------ */
/*  MMIO accessor helpers                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Write a 32-bit value to a VirtIO MMIO register
 *
 * @param[in] offset  Register offset from device base
 * @param[in] value   Value to write
 */
static inline void vnic_write(uint32_t offset, uint32_t value)
{
    REG32((uintptr_t)g_nic.base + offset) = value;
}

/**
 * @brief Read a 32-bit value from a VirtIO MMIO register
 *
 * @param[in] offset  Register offset from device base
 * @return Register value
 */
static inline uint32_t vnic_read(uint32_t offset)
{
    return REG32((uintptr_t)g_nic.base + offset);
}

/**
 * @brief Read one byte from the VirtIO-Net device config space
 *
 * The config space begins at VIRTIO_MMIO_CONFIG (0x100).
 * For the network device, bytes 0–5 are the MAC address.
 *
 * @param[in] byte_offset  Byte offset within config space
 * @return Byte value
 */
static inline uint8_t vnic_config_read_byte(uint32_t byte_offset)
{
    volatile uint8_t *cfg = (volatile uint8_t *)
        ((uintptr_t)g_nic.base + VIRTIO_MMIO_CONFIG + byte_offset);
    return *cfg;
}

/* ------------------------------------------------------------------ */
/*  Memory barrier helpers                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Full system memory/device barrier
 *
 * Ensures all preceding memory accesses are globally visible before
 * any subsequent accesses. Required before MMIO QUEUE_NOTIFY writes
 * and before writing the STATUS register to prevent descriptor ring
 * updates from being reordered past the notification.
 */
static inline void dsb_sy(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/**
 * @brief Data memory barrier
 *
 * Lighter-weight than dsb_sy; ensures ordering within the memory
 * system for ordinary memory (not device). Used when ordering
 * descriptor table writes relative to ring index updates.
 */
static inline void dmb_sy(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

/* ------------------------------------------------------------------ */
/*  UART helpers for hex printing (no printf available)               */
/* ------------------------------------------------------------------ */

/** Print a single hex nibble */
static void put_nibble(uint8_t n)
{
    n &= 0xF;
    HAL_UART_PutChar((char)(n < 10 ? '0' + n : 'a' + n - 10));
}

/** Print a byte as two hex digits */
static void put_hex8(uint8_t v)
{
    put_nibble(v >> 4);
    put_nibble(v);
}

/** Print a 32-bit value as 8 hex digits with "0x" prefix */
static void put_hex32(uint32_t v)
{
    HAL_UART_PutString("0x");
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        put_nibble((uint8_t)(v >> (uint32_t)shift));
    }
}

/* ------------------------------------------------------------------ */
/*  VNIC_Discover                                                     */
/* ------------------------------------------------------------------ */

uintptr_t VNIC_Discover(void)
{
    uintptr_t base;

    HAL_UART_PutString("[VNIC] Scanning MMIO window for virtio-net...\n");

    /*
     * Scan top-down from highest slot to lowest.
     * QEMU populates devices from the top, so the first device
     * added (-device virtio-net-device) typically lands at the
     * highest-numbered free slot.
     */
    for (base = VIRT_MMIO_TOP; base >= VIRT_MMIO_BOT; base -= VIRT_MMIO_STEP) {

        uint32_t magic   = REG32(base + VIRTIO_MMIO_MAGIC_VALUE);
        uint32_t dev_id  = REG32(base + VIRTIO_MMIO_DEVICE_ID);
        uint32_t version = REG32(base + VIRTIO_MMIO_VERSION);

        if (magic == VIRTIO_MAGIC && dev_id == VIRTIO_DEVID_NET) {

            /* Compute GIC INTID from slot index */
            uint32_t slot_index = (uint32_t)((base - VIRT_MMIO_BOT) / VIRT_MMIO_STEP);
            g_nic.irq  = (uint32_t)(VNIC_IRQ_BASE + slot_index);
            g_nic.base = (volatile uint8_t *)base;

            HAL_UART_PutString("[VNIC] Found virtio-net at ");
            put_hex32((uint32_t)base);
            HAL_UART_PutString(", slot ");
            HAL_UART_PutDec(slot_index);
            HAL_UART_PutString(" version ");
            HAL_UART_PutDec(version);
            HAL_UART_PutString(", IRQ ");
            HAL_UART_PutDec(g_nic.irq);
            HAL_UART_PutString("\n");

            return base;
        }

        /* Avoid underflow below VIRT_MMIO_BOT */
        if (base == VIRT_MMIO_BOT) {
            break;
        }
    }

    HAL_UART_PutString("[VNIC] ERROR: virtio-net device not found in MMIO window\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Internal: virtqueue setup helper                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Configure one VirtIO queue and write its rings to the device
 *
 * Selects the queue, writes the queue size, allocates the three rings
 * from the permanent bump allocator (16-byte aligned as required by
 * the VirtIO spec), writes the physical addresses to the device, and
 * marks the queue READY.
 *
 * @param[in]  queue_idx  0 = RX, 1 = TX
 * @param[out] out_desc   Receives pointer to descriptor table
 * @param[out] out_avail  Receives pointer to available ring
 * @param[out] out_used   Receives pointer to used ring
 *
 * @return  0 on success, -1 on allocation failure
 */
static int vnic_setup_queue(uint32_t   queue_idx,
                             VirtqDesc  **out_desc,
                             VirtqAvail **out_avail,
                             VirtqUsed  **out_used)
{
    /* Select queue */
    vnic_write(VIRTIO_MMIO_QUEUE_SEL, queue_idx);
    dsb_sy();

    /* Verify the device supports at least VNET_QUEUE_SIZE descriptors */
    uint32_t qmax = vnic_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax < VNET_QUEUE_SIZE) {
        HAL_UART_PutString("[VNIC] ERROR: queue ");
        HAL_UART_PutDec(queue_idx);
        HAL_UART_PutString(" max size ");
        HAL_UART_PutDec(qmax);
        HAL_UART_PutString(" < ");
        HAL_UART_PutDec(VNET_QUEUE_SIZE);
        HAL_UART_PutString("\n");
        return -1;
    }

    /* Tell device how many descriptors we will use */
    vnic_write(VIRTIO_MMIO_QUEUE_NUM, VNET_QUEUE_SIZE);

    /*
     * Legacy VirtIO REQUIRES a contiguous, specifically-aligned layout.
     * Memory layout: desc array, then avail struct, PAD to alignment, then used struct.
     */
    uint32_t align = 4096;
    uint32_t desc_sz = sizeof(VirtqDesc) * VNET_QUEUE_SIZE;
    uint32_t avail_sz = 2 + 2 + VNET_QUEUE_SIZE * 2 + 2;
    uint32_t used_offset = (desc_sz + avail_sz + align - 1) & ~(align - 1);
    uint32_t used_sz = 2 + 2 + VNET_QUEUE_SIZE * 8 + 2;
    uint32_t total_sz = used_offset + used_sz;

    uint8_t *vring = (uint8_t *)KMEM_Alloc(total_sz, align);
    if (!vring) {
        HAL_UART_PutString("[VNIC] ERROR: KMEM_Alloc failed for queue ");
        HAL_UART_PutDec(queue_idx);
        HAL_UART_PutString("\n");
        return -1;
    }

    for (uint32_t i = 0; i < total_sz; i++) {
        vring[i] = 0;
    }

    VirtqDesc  *desc  = (VirtqDesc  *)vring;
    VirtqAvail *avail = (VirtqAvail *)(vring + desc_sz);
    VirtqUsed  *used  = (VirtqUsed  *)(vring + used_offset);

    /* Tell device how to find the queues: alignment and PFN */
    vnic_write(VIRTIO_MMIO_QUEUE_ALIGN, align);
    
    uint32_t pfn = (uint32_t)(((uintptr_t)vring) / align);
    vnic_write(VIRTIO_MMIO_QUEUE_PFN, pfn);
    dsb_sy();

    *out_desc  = desc;
    *out_avail = avail;
    *out_used  = used;

    HAL_UART_PutString("[VNIC]   Queue ");
    HAL_UART_PutDec(queue_idx);
    HAL_UART_PutString(": desc=");
    put_hex32((uint32_t)(uintptr_t)desc);
    HAL_UART_PutString(" avail=");
    put_hex32((uint32_t)(uintptr_t)avail);
    HAL_UART_PutString(" used=");
    put_hex32((uint32_t)(uintptr_t)used);
    HAL_UART_PutString("\n");

    return 0;
}

/* ------------------------------------------------------------------ */
/*  VNIC_Init                                                         */
/* ------------------------------------------------------------------ */

int VNIC_Init(void (*rx_cb)(uint8_t *frame, uint16_t len))
{
    HAL_UART_PutString("[VNIC] Initializing VirtIO-Net driver...\n");

    /* ----- Step 1: Discover device ----- */
    if (VNIC_Discover() == 0) {
        return -1;  /* error already printed */
    }

    /* ----- Step 2: Reset device (write 0 to STATUS) ----- */
    vnic_write(VIRTIO_MMIO_STATUS, 0);
    dsb_sy();

    /*
     * Spin briefly after reset — QEMU processes the reset
     * synchronously but a barrier ensures ordering.
     */
    dmb_sy();

    /* ----- Step 3: Set ACKNOWLEDGE | DRIVER ----- */
    uint32_t status = 0;
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    vnic_write(VIRTIO_MMIO_STATUS, status);
    dsb_sy();

    status |= VIRTIO_STATUS_DRIVER;
    vnic_write(VIRTIO_MMIO_STATUS, status);
    dsb_sy();

    /* ----- Step 4: Feature negotiation ----- */
    /*
     * Read what the device advertises, then restrict to only what
     * we actually use: VIRTIO_NET_F_MAC (bit 5).
     * We deliberately do NOT negotiate VIRTIO_F_VERSION_1 so we
     * stay in legacy mode (VirtIO 1.0 §4.2.3.3 LEGACY transport).
     */
    uint32_t dev_features = vnic_read(VIRTIO_MMIO_DEVICE_FEATURES);
    uint32_t drv_features = dev_features & VIRTIO_NET_F_MAC;

    vnic_write(VIRTIO_MMIO_DRIVER_FEATURES, drv_features);
    dsb_sy();

    status |= VIRTIO_STATUS_FEATURES_OK;
    vnic_write(VIRTIO_MMIO_STATUS, status);
    dsb_sy();

    /* Verify device accepted our feature set */
    uint32_t status_readback = vnic_read(VIRTIO_MMIO_STATUS);
    if (!(status_readback & VIRTIO_STATUS_FEATURES_OK)) {
        HAL_UART_PutString(
            "[VNIC] ERROR: FEATURES_OK not set — device rejected feature set\n");
        vnic_write(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }

    HAL_UART_PutString("[VNIC] Feature negotiation OK (dev_features=");
    put_hex32(dev_features);
    HAL_UART_PutString(" drv_features=");
    put_hex32(drv_features);
    HAL_UART_PutString(")\n");

    /* Write guest page size (required once, before any queue setup) */
    vnic_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    dsb_sy();

    /* ----- Step 5: Set up RX virtqueue (queue 0) ----- */
    if (vnic_setup_queue(VNET_QUEUE_RX,
                         &g_nic.rx_desc,
                         &g_nic.rx_avail,
                         &g_nic.rx_used) != 0) {
        vnic_write(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }
    g_nic.rx_last_idx = 0;

    /*
     * Pre-fill all RX descriptors and hand them to the device
     * by advancing the available ring index to VNET_QUEUE_SIZE.
     * Each descriptor points to a VirtIO-Net-header-inclusive
     * DMA buffer (rx_buf[i]).  We mark each as WRITE-only so the
     * device can fill it with received data.
     */
    for (uint16_t i = 0; i < VNET_QUEUE_SIZE; i++) {
        g_nic.rx_desc[i].addr  = (uint64_t)(uintptr_t)g_nic.rx_buf[i];
        g_nic.rx_desc[i].len   = VNET_BUF_SIZE;
        g_nic.rx_desc[i].flags = VRING_DESC_F_WRITE;
        g_nic.rx_desc[i].next  = 0;
        g_nic.rx_avail->ring[i] = i;
    }
    dmb_sy();  /* Descriptor writes must be visible before ring index */
    g_nic.rx_avail->idx = VNET_QUEUE_SIZE;
    dsb_sy();

    /* Notify device that RX queue is ready (queue 0) */
    vnic_write(VIRTIO_MMIO_QUEUE_NOTIFY, VNET_QUEUE_RX);
    dsb_sy();

    /* ----- Step 6: Set up TX virtqueue (queue 1) ----- */
    if (vnic_setup_queue(VNET_QUEUE_TX,
                         &g_nic.tx_desc,
                         &g_nic.tx_avail,
                         &g_nic.tx_used) != 0) {
        vnic_write(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }
    g_nic.tx_last_idx = 0;
    /* TX avail ring starts at 0 — no buffers pre-submitted */

    /* ----- Step 7: DRIVER_OK ----- */
    status |= VIRTIO_STATUS_DRIVER_OK;
    vnic_write(VIRTIO_MMIO_STATUS, status);
    dsb_sy();

    HAL_UART_PutString("[VNIC] Device status set to DRIVER_OK\n");

    /* ----- Step 8: Read MAC address from config space ----- */
    for (uint32_t i = 0; i < 6; i++) {
        g_nic.mac[i] = vnic_config_read_byte(i);
    }

    HAL_UART_PutString("[VNIC] MAC: ");
    for (uint32_t i = 0; i < 6; i++) {
        put_hex8(g_nic.mac[i]);
        if (i < 5) {
            HAL_UART_PutChar(':');
        }
    }
    HAL_UART_PutString("\n");

    /* ----- Step 9: Store callback ----- */
    g_nic.rx_callback = rx_cb;

    /* ----- Step 10: Register IRQ with GIC ----- */
    HAL_GIC_SetPriority(g_nic.irq, 0xA0);
    HAL_GIC_EnableIRQ(g_nic.irq);

    HAL_UART_PutString("[VNIC] IRQ ");
    HAL_UART_PutDec(g_nic.irq);
    HAL_UART_PutString(" enabled\n");

    HAL_UART_PutString("[VNIC] VirtIO-Net initialized successfully\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  VNIC_Send                                                         */
/* ------------------------------------------------------------------ */

int VNIC_Send(uint8_t *frame, uint16_t len)
{
    if (len == 0 || (uint32_t)len + sizeof(VirtioNetHdr) > VNET_BUF_SIZE) {
        HAL_UART_PutString("[VNIC] ERROR: VNIC_Send: invalid frame length\n");
        return -1;
    }

    /*
     * Pick the next TX descriptor slot.
     * We use a simple modulo scheme. For VNET_QUEUE_SIZE == 16
     * (a power of 2) this is equivalent to masking.
     *
     * NOTE: We do not check whether the slot is still in flight —
     * for the initial smoke-test implementation we rely on the device
     * draining TX fast enough. A production driver would track in-flight
     * descriptors. VNIC_Poll recycles them.
     */
    uint16_t idx = g_nic.tx_avail->idx % VNET_QUEUE_SIZE;
    uint8_t *buf = g_nic.tx_buf[idx];

    /* Build a zeroed VirtioNetHdr at the start of the TX buffer */
    VirtioNetHdr hdr;
    for (size_t i = 0; i < sizeof(VirtioNetHdr); i++) {
        ((uint8_t *)&hdr)[i] = 0;
    }

    /* Copy header then frame data into DMA buffer */
    uint8_t *dst = buf;
    const uint8_t *src = (const uint8_t *)&hdr;
    for (size_t i = 0; i < sizeof(VirtioNetHdr); i++) {
        *dst++ = *src++;
    }
    src = frame;
    for (uint16_t i = 0; i < len; i++) {
        *dst++ = *src++;
    }

    /* Populate descriptor */
    g_nic.tx_desc[idx].addr  = (uint64_t)(uintptr_t)buf;
    g_nic.tx_desc[idx].len   = (uint32_t)(sizeof(VirtioNetHdr) + len);
    g_nic.tx_desc[idx].flags = 0;    /* No chaining, device reads this */
    g_nic.tx_desc[idx].next  = 0;

    /* Add descriptor to available ring */
    g_nic.tx_avail->ring[g_nic.tx_avail->idx % VNET_QUEUE_SIZE] = idx;

    /* Ensure descriptor and ring writes are visible before bumping idx */
    dmb_sy();

    g_nic.tx_avail->idx++;

    /* Full barrier then notify device of TX queue (queue 1) */
    dsb_sy();
    vnic_write(VIRTIO_MMIO_QUEUE_NOTIFY, VNET_QUEUE_TX);
    dsb_sy();

    return 0;
}

/* ------------------------------------------------------------------ */
/*  VNIC_Poll                                                         */
/* ------------------------------------------------------------------ */

void VNIC_Poll(void)
{
    /* ---- Drain TX used ring (free completed send slots) ---- */
    {
        uint16_t used_idx = g_nic.tx_used->idx;
        while (g_nic.tx_last_idx != used_idx) {
            /*
             * For bump-allocated TX buffers we don't need to do
             * anything — the buffer is reused by slot index on next send.
             * Just advance the shadow index to track completions.
             */
            g_nic.tx_last_idx++;
        }
    }

    /* ---- Process RX used ring (deliver received frames) ---- */
    {
        uint16_t used_idx = g_nic.rx_used->idx;

        while (g_nic.rx_last_idx != used_idx) {

            uint16_t slot = g_nic.rx_last_idx % VNET_QUEUE_SIZE;
            VirtqUsedElem *elem = &g_nic.rx_used->ring[slot];

            uint32_t desc_id   = elem->id;
            uint32_t total_len = elem->len;

            if (desc_id < VNET_QUEUE_SIZE) {

                uint8_t *buf = g_nic.rx_buf[desc_id];

                if (total_len > sizeof(VirtioNetHdr) &&
                    g_nic.rx_callback != NULL) {

                    /* Skip the VirtioNetHdr prefix before delivery */
                    uint8_t  *frame     = buf + sizeof(VirtioNetHdr);
                    uint16_t  frame_len = (uint16_t)(total_len -
                                                     sizeof(VirtioNetHdr));
                    g_nic.rx_callback(frame, frame_len);
                }

                /*
                 * Recycle the descriptor: reset its length (in case the
                 * device modified it) and re-add it to the available ring.
                 */
                g_nic.rx_desc[desc_id].len   = VNET_BUF_SIZE;
                g_nic.rx_desc[desc_id].flags = VRING_DESC_F_WRITE;

                uint16_t avail_slot =
                    g_nic.rx_avail->idx % VNET_QUEUE_SIZE;
                g_nic.rx_avail->ring[avail_slot] = (uint16_t)desc_id;

                dmb_sy();
                g_nic.rx_avail->idx++;
                dsb_sy();

                /* Notify device that a new RX buffer is available */
                vnic_write(VIRTIO_MMIO_QUEUE_NOTIFY, VNET_QUEUE_RX);
                dsb_sy();
            }

            g_nic.rx_last_idx++;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  VNIC_IRQHandler                                                   */
/* ------------------------------------------------------------------ */

void VNIC_IRQHandler(void)
{
    /*
     * Read and acknowledge the interrupt reason bitmask.
     * Bit 0 = used ring updated (normal TX/RX completion).
     * Bit 1 = device config space changed (MAC address update, etc.)
     *
     * We write back the same value read to ACK all pending reasons.
     */
    uint32_t isr = vnic_read(VIRTIO_MMIO_INTERRUPT_STATUS);
    vnic_write(VIRTIO_MMIO_INTERRUPT_ACK, isr);
    dsb_sy();

    if (isr & 0x1u) {
        /* Used ring notification — process completed TX and RX */
        VNIC_Poll();
    }
}

/* ------------------------------------------------------------------ */
/*  VNIC_GetMAC                                                       */
/* ------------------------------------------------------------------ */

void VNIC_GetMAC(uint8_t mac[6])
{
    for (uint32_t i = 0; i < 6; i++) {
        mac[i] = g_nic.mac[i];
    }
}

/* ------------------------------------------------------------------ */
/*  VNIC_GetIRQ                                                       */
/* ------------------------------------------------------------------ */

uint32_t VNIC_GetIRQ(void)
{
    return g_nic.irq;
}
