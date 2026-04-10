/**
 * @file infer_server.h
 * @brief SFU Inference Server API for MiniOS-ESP8266
 */

#ifndef MINIOS_ESP8266_NET_INFER_SERVER_H
#define MINIOS_ESP8266_NET_INFER_SERVER_H

#include "types.h"
#include "status.h"

#define INFER_MODEL_NAME_MAX  32

void INFER_Init(void);
void INFER_OnRequest(uint32_t src_ip, uint16_t src_port,
                     uint32_t req_id, uint8_t *payload, uint16_t payload_len);
void INFER_OnCmd(uint32_t src_ip, uint16_t src_port,
                 uint32_t req_id, const char *cmd, uint16_t cmd_len);
void INFER_ListModels(void);
int  INFER_SelectModel(const char *name);
const char *INFER_GetActiveModel(void);

#endif /* MINIOS_ESP8266_NET_INFER_SERVER_H */
