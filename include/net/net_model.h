/**
 * @file net_model.h
 * @brief Network model transfer interface for MiniOS
 *
 * Receives an ONNX model file over the QEMU virtio-net interface
 * and writes it into the MiniFS /exec directory.
 *
 * On QEMU, traffic is bridged via a TAP/user-mode NIC.
 * We use a very simple custom framing protocol:
 *   [4 bytes: magic 0x4D494E49] [4 bytes: payload_size] [payload bytes]
 *
 * The host sends this with the provided host_send.py script.
 */

#ifndef MINIOS_NET_NET_MODEL_H
#define MINIOS_NET_NET_MODEL_H

#include "types.h"
#include "status.h"

/* Magic number that starts every model transfer frame */
#define NET_MODEL_MAGIC     0x4D494E49U   /* ASCII "MINI" */

/* The virtio-net MMIO base address on QEMU virt machine */
#define VIRTIO_NET_BASE     0x0A003E00U

/**
 * @brief Initialize the virtio-net driver.
 * Must be called once in kernel_main() before SCHED_Start().
 * @return STATUS_OK on success
 */
Status NET_Init(void);

/**
 * @brief Block until a model arrives over the network.
 *
 * Polls the virtio-net RX ring. When a complete frame arrives
 * with the correct magic, extracts the payload and writes it
 * to /exec/model.bin in MiniFS.
 *
 * Called by the "recv" shell command.
 */
void NET_ReceiveModel(void);

/**
 * @brief Load a model from MiniFS and run ONNX inference.
 *
 * @param path  Path in format "dir/filename", e.g. "exec/model.bin"
 *              (no leading slash)
 */
void NET_RunModel(const char *path);

#endif /* MINIOS_NET_NET_MODEL_H */