/**
 * @file onnx_loader.h
 * @brief ONNX model loading from various sources
 *
 * Provides functionality to load ONNX models from:
 * 1. Embedded C arrays (compiled into kernel)
 * 2. Custom binary format (optimized for embedded)
 * 3. Protobuf format (future - full ONNX compatibility)
 */

#ifndef ONNX_LOADER_H
#define ONNX_LOADER_H

#include "onnx_types.h"
#include "onnx_graph.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Load from Embedded C Array                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Load ONNX model from embedded C array
 * 
 * Use this for models compiled into the kernel binary.
 * Convert .onnx to C array using:
 *   xxd -i model.onnx > model.h
 * 
 * Example:
 *   #include "my_model.h"
 *   ONNX_LoadEmbedded(&graph, my_model_onnx, my_model_onnx_len);
 * 
 * @param graph Pointer to graph to populate
 * @param data Pointer to embedded ONNX data
 * @param size Size of data in bytes
 * @param format Format of embedded data (PROTOBUF or CUSTOM_BINARY)
 * @return STATUS_OK on success
 */
typedef enum {
    ONNX_FORMAT_PROTOBUF,      /* Standard ONNX protobuf format */
    ONNX_FORMAT_CUSTOM_BINARY, /* Our custom optimized format */
} ONNX_Format;

Status ONNX_LoadEmbedded(ONNX_Graph* graph,
                          const uint8_t* data,
                          uint64_t size,
                          ONNX_Format format);

/* ------------------------------------------------------------------ */
/*  Custom Binary Format                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Custom ONNX binary format header
 * 
 * Optimized for embedded systems - simple, fast to parse.
 * No dynamic allocation needed.
 */
#define ONNX_CUSTOM_MAGIC 0x4F4E4E58  /* 'ONNX' */
#define ONNX_CUSTOM_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;              /* ONNX_CUSTOM_MAGIC */
    uint32_t version;            /* ONNX_CUSTOM_VERSION */
    uint32_t num_nodes;          /* Number of operators */
    uint32_t num_tensors;        /* Number of tensors */
    uint32_t num_inputs;         /* Number of graph inputs */
    uint32_t num_outputs;        /* Number of graph outputs */
    uint64_t tensor_data_offset; /* Offset to tensor data section */
} ONNX_CustomHeader;

/**
 * @brief Load from custom binary format
 * 
 * Fast, embedded-friendly format. Convert .onnx using:
 *   python scripts/convert_onnx_to_binary.py model.onnx model.bin
 * 
 * @param graph Pointer to graph to populate
 * @param data Pointer to binary data
 * @param size Size of data
 * @return STATUS_OK on success
 */
Status ONNX_LoadCustomBinary(ONNX_Graph* graph,
                              const uint8_t* data,
                              uint64_t size);

/* ------------------------------------------------------------------ */
/*  Protobuf Format Parser (Minimal)                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Load from standard ONNX protobuf format
 * 
 * Implements a minimal protobuf parser for ONNX models.
 * Does NOT use the full protobuf library - only parses
 * the specific fields needed for inference.
 * 
 * @param graph Pointer to graph to populate
 * @param protobuf_data Pointer to .onnx file data
 * @param size Size of protobuf data
 * @return STATUS_OK on success
 */
Status ONNX_LoadProtobuf(ONNX_Graph* graph,
                          const uint8_t* protobuf_data,
                          uint64_t size);

/* ------------------------------------------------------------------ */
/*  Conversion Utilities                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Export graph to custom binary format
 * 
 * Useful for saving modified graphs or creating optimized models.
 * 
 * @param graph Pointer to graph
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param bytes_written Output: actual bytes written
 * @return STATUS_OK on success
 */
Status ONNX_ExportCustomBinary(const ONNX_Graph* graph,
                                 uint8_t* buffer,
                                 uint64_t buffer_size,
                                 uint64_t* bytes_written);

/* ------------------------------------------------------------------ */
/*  Helper: Print Model Info                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Print information about a serialized model
 * 
 * Useful for debugging - prints model metadata without
 * fully loading it.
 * 
 * @param data Model data
 * @param size Model size
 * @param format Model format
 */
void ONNX_PrintModelInfo(const uint8_t* data,
                          uint64_t size,
                          ONNX_Format format);

#endif /* ONNX_LOADER_H */
