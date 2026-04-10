/**
 * @file onnx_runtime.h
 * @brief ONNX Runtime API for MiniOS-ESP8266
 *
 * API-compatible with the ARM64 original onnx_runtime.h so that
 * infer_server_esp.c compiles without changes to call sites.
 */

#ifndef MINIOS_ESP8266_ONNX_RUNTIME_H
#define MINIOS_ESP8266_ONNX_RUNTIME_H

#include "types.h"
#include "status.h"
#include "onnx/onnx_types.h"

Status ONNX_Runtime_Init(ONNX_InferenceContext *ctx, ONNX_Graph *graph,
                          uint32_t workspace_size);

void ONNX_Runtime_Cleanup(ONNX_InferenceContext *ctx);

Status ONNX_Runtime_InferenceSimple(
    ONNX_InferenceContext *ctx,
    const void **in_ptrs,  uint32_t *in_sizes,  uint8_t num_inputs,
    void **out_ptrs,       uint32_t *out_sizes,  uint8_t num_outputs);

const char *ONNX_GetOperatorName(ONNX_OperatorType op);

#endif /* MINIOS_ESP8266_ONNX_RUNTIME_H */
