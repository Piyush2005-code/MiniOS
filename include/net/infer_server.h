/**
 * @file infer_server.h
 * @brief SFU Inference Server — model management and request handling
 *
 * Supports loading any .onnx model from /storage via the CMD channel:
 *   LIST_MODELS         → lists all .onnx files in /storage
 *   SELECT_MODEL <name> → loads and activates the named model
 *   GET_MODEL           → returns the active model name
 *
 * Inference is then dispatched to whichever model is currently active.
 */

#ifndef MINIOS_NET_INFER_SERVER_H
#define MINIOS_NET_INFER_SERVER_H

#include "types.h"

/* Max model name length (without path prefix or .onnx suffix) */
#define INFER_MODEL_NAME_MAX  64

/**
 * @brief Initialize the inference server.
 *
 * Registers INFER_OnRequest and INFER_OnCmd with SFU layer.
 * Default active model is "tiny_mlp" (first available .onnx in /storage).
 */
void INFER_Init(void);

/**
 * @brief Inference request callback (INFER_REQUEST packets).
 */
void INFER_OnRequest(uint32_t src_ip, uint16_t src_port,
                     uint32_t req_id,
                     uint8_t *payload, uint16_t payload_len);

/**
 * @brief CMD channel handler (LIST_MODELS / SELECT_MODEL / GET_MODEL).
 */
void INFER_OnCmd(uint32_t src_ip, uint16_t src_port,
                 uint32_t req_id,
                 const char *cmd, uint16_t cmd_len);

/**
 * @brief Send an error response.
 */
void INFER_SendError(uint32_t dst_ip, uint16_t dst_port,
                     uint32_t req_id, uint32_t error_code);

/**
 * @brief Shell command: list available .onnx models in /storage.
 */
void INFER_ListModels(void);

/**
 * @brief Shell command: select an active model by filename stem.
 *
 * @param name  Model stem, e.g. "tiny_mlp" (without .onnx and without path)
 * @return 0 on success, -1 if model not found or load failed
 */
int INFER_SelectModel(const char *name);

/**
 * @brief Return the currently active model name (read-only).
 */
const char *INFER_GetActiveModel(void);

/**
 * @brief Register ONNX-related shell commands (models, model, infer).
 */
void INFER_RegisterShellCommands(void);

#endif /* MINIOS_NET_INFER_SERVER_H */
