/**
 * @file infer_server.h
 * @brief SFU Inference Server hook
 */

#ifndef MINIOS_NET_INFER_SERVER_H
#define MINIOS_NET_INFER_SERVER_H

#include "types.h"

/**
 * @brief Initialize the Inference Server
 *
 * Registers INFER_OnRequest with the SFU layer and
 * prepares static infer buffers.
 */
void INFER_Init(void);

/**
 * @brief Callback for incoming INFER_REQUEST packets
 *
 * Extracts the payload (array of floats), passes it to the ONNX
 * inference engine, and sends the response back using SFU_SendRaw.
 *
 * @param[in] src_ip        Sender IP
 * @param[in] src_port      Sender UDP port
 * @param[in] req_id        SFU request ID to reply to
 * @param[in] payload       Raw packet payload bytes
 * @param[in] payload_len   Payload size in bytes
 */
void INFER_OnRequest(uint32_t src_ip, uint16_t src_port,
                     uint32_t req_id,
                     uint8_t *payload, uint16_t payload_len);

/**
 * @brief Send an error response back to the client
 *
 * @param[in] dst_ip        Receiver IP
 * @param[in] dst_port      Receiver UDP port
 * @param[in] req_id        Original request ID that failed
 * @param[in] error_code    Error reason code to serialize
 */
void INFER_SendError(uint32_t dst_ip, uint16_t dst_port,
                     uint32_t req_id, uint32_t error_code);

#endif /* MINIOS_NET_INFER_SERVER_H */
