/**
 * @file udp.h
 * @brief UDP Abstraction Layer for MiniOS-ESP8266
 *
 * Same API surface as the original ARM64 udp.h so that sfu_esp.c
 * compiles without changes. Implemented over ESP8266 espconn UDP.
 */

#ifndef MINIOS_ESP8266_NET_UDP_H
#define MINIOS_ESP8266_NET_UDP_H

#include "types.h"
#include "status.h"

#define UDP_MAX_HANDLERS   4

/** Callback type: (src_ip, src_port, payload, payload_len) */
typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port,
                               uint8_t *payload, uint16_t len);

void UDP_Init(void);
int  UDP_Bind(uint16_t port, udp_handler_t handler);
int  UDP_Send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
              uint8_t *payload, uint16_t len);

#endif /* MINIOS_ESP8266_NET_UDP_H */
