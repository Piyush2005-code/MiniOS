/**
 * @file onnx_loader.h
 * @brief ONNX Loader API for MiniOS-ESP8266
 */

#ifndef MINIOS_ESP8266_ONNX_LOADER_H
#define MINIOS_ESP8266_ONNX_LOADER_H

#include "types.h"
#include "status.h"
#include "onnx/onnx_types.h"

/**
 * @brief Load an embedded model by name into a graph struct.
 * @param graph       Output graph (caller-allocated)
 * @param model_name  Model name string e.g. "tiny_mlp"
 * @return STATUS_OK on success
 */
Status ONNX_LoadEmbedded(ONNX_Graph *graph, const char *model_name);

#endif /* MINIOS_ESP8266_ONNX_LOADER_H */
