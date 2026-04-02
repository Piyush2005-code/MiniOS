/**
 * @file udp.h
 * @brief UDP (User Datagram Protocol) layer for MiniOS
 *
 * Minimal UDP implementation supporting:
 *   - Port-based demultiplexing via a static binding table (up to 8 ports)
 *   - Stateless send: no connection state, no retransmission
 *   - Checksum disabled on send (checksum = 0, valid per RFC 768)
 *
 * Usage pattern:
 *   UDP_Init();
 *   UDP_Bind(9000, my_handler);
 *   // later:
 *   UDP_Send(HOST_IP, 9000, 9000, payload, len);
 */

#ifndef MINIOS_NET_UDP_H
#define MINIOS_NET_UDP_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  UDP handler callback type                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Callback invoked for each received UDP datagram
 *
 * @param[in] src_ip    Source IP address in network byte order
 * @param[in] src_port  Source UDP port in host byte order
 * @param[in] payload   Pointer to datagram payload (after UDP header)
 * @param[in] len       Payload length in bytes
 */
typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port,
                               uint8_t *payload, uint16_t len);

/* ------------------------------------------------------------------ */
/*  Binding table limits                                              */
/* ------------------------------------------------------------------ */

/** Maximum number of simultaneously bound UDP ports */
#define UDP_MAX_HANDLERS    8u

/* ------------------------------------------------------------------ */
/*  UDP header (8 bytes)                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief UDP datagram header
 *
 * All fields in network (big-endian) byte order.
 */
typedef struct __attribute__((packed)) {
    uint16_t src_port;   /**< Source port, big-endian */
    uint16_t dst_port;   /**< Destination port, big-endian */
    uint16_t length;     /**< Header + payload length, big-endian */
    uint16_t checksum;   /**< 0 = checksum disabled (RFC 768 allows this) */
} UDPHdr_t;

/* ------------------------------------------------------------------ */
/*  Port binding record (internal, exposed for size calculation only) */
/* ------------------------------------------------------------------ */

/** Internal binding table entry */
typedef struct {
    uint16_t      port;     /**< Local port in host byte order */
    udp_handler_t handler;  /**< Callback registered for this port */
    uint8_t       in_use;   /**< 1 = slot occupied */
} UDPBinding_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the UDP layer
 *
 * Clears all binding table entries. Prints an init log line to UART.
 * Must be called before UDP_Bind() or UDP_Send().
 */
void UDP_Init(void);

/**
 * @brief Bind a handler callback to a local UDP port
 *
 * Registers a function to receive all datagrams arriving on the
 * specified destination port. A maximum of UDP_MAX_HANDLERS bindings
 * can be active simultaneously.
 *
 * @param[in] port     Local UDP port to listen on (host byte order)
 * @param[in] handler  Callback function pointer
 *
 * @return  0 on success
 *         -1 if the binding table is full
 */
int UDP_Bind(uint16_t port, udp_handler_t handler);

/**
 * @brief Send a UDP datagram
 *
 * Constructs a UDP header (checksum = 0) and hands the complete
 * datagram to IPV4_Send().
 *
 * @param[in] dst_ip    Destination IP in network byte order
 * @param[in] dst_port  Destination UDP port (host byte order)
 * @param[in] src_port  Source UDP port (host byte order)
 * @param[in] payload   Datagram payload
 * @param[in] len       Payload length in bytes
 *
 * @return  0 on success
 *         -1 if payload is too large
 */
int UDP_Send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
             uint8_t *payload, uint16_t len);

/**
 * @brief Process a received UDP datagram
 *
 * Called by IPV4_Receive() when protocol == IP_PROTO_UDP.
 * Parses the UDP header and invokes the registered handler for
 * the destination port, or drops silently if none is registered.
 *
 * @param[in] src_ip  Source IP address in network byte order
 * @param[in] buf     Pointer to UDP datagram (starts at src_port field)
 * @param[in] len     Total length including UDP header
 */
void UDP_Receive(uint32_t src_ip, uint8_t *buf, uint16_t len);

#endif /* MINIOS_NET_UDP_H */
