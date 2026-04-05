/**
 * @file onnx_runtime.h
 * @brief ONNX inference runtime API
 *
 * Provides high-level API for running inference on ONNX models.
 * Supports both single-shot inference and streaming inference.
 */

#ifndef ONNX_RUNTIME_H
#define ONNX_RUNTIME_H

#include "onnx_types.h"
#include "onnx_graph.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Runtime Initialization                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize ONNX runtime context
 * 
 * @param ctx Pointer to inference context
 * @param graph Pointer to loaded graph
 * @param workspace_size Size of temporary workspace (bytes)
 * @return STATUS_OK on success
 */
Status ONNX_Runtime_Init(ONNX_InferenceContext* ctx,
                          ONNX_Graph* graph,
                          uint64_t workspace_size);

/**
 * @brief Cleanup runtime context
 * 
 * @param ctx Pointer to inference context
 */
void ONNX_Runtime_Cleanup(ONNX_InferenceContext* ctx);

/* ------------------------------------------------------------------ */
/*  Inference Execution                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Run inference with provided input tensors
 * 
 * Executes the entire computation graph according to the schedule.
 * 
 * @param ctx Pointer to inference context
 * @param inputs Array of input tensors (must match graph inputs)
 * @param num_inputs Number of input tensors
 * @param outputs Array to receive output tensors
 * @param num_outputs Number of output tensors
 * @return STATUS_OK on success
 */
Status ONNX_Runtime_Inference(ONNX_InferenceContext* ctx,
                                ONNX_Tensor** inputs,
                                uint32_t num_inputs,
                                ONNX_Tensor** outputs,
                                uint32_t num_outputs);

/**
 * @brief Run inference with input data buffers
 * 
 * Convenience function that takes raw data pointers instead of tensors.
 * 
 * @param ctx Pointer to inference context
 * @param input_data Array of input data pointers
 * @param input_sizes Array of input data sizes
 * @param num_inputs Number of inputs
 * @param output_data Array to receive output data pointers
 * @param output_sizes Array to receive output data sizes
 * @param num_outputs Number of outputs
 * @return STATUS_OK on success
 */
Status ONNX_Runtime_InferenceSimple(ONNX_InferenceContext* ctx,
                                      const void** input_data,
                                      uint64_t* input_sizes,
                                      uint32_t num_inputs,
                                      void** output_data,
                                      uint64_t* output_sizes,
                                      uint32_t num_outputs);

/**
 * @brief Execute a single node in the graph
 * 
 * Runs only one operator. Useful for step-by-step execution or debugging.
 * 
 * @param ctx Pointer to inference context
 * @param node Pointer to node to execute
 * @return STATUS_OK on success
 */
Status ONNX_Runtime_ExecuteNode(ONNX_InferenceContext* ctx, ONNX_Node* node);

/**
 * @brief Execute nodes up to a specific node
 * 
 * Runs the graph partially up to (and including) the specified node.
 * 
 * @param ctx Pointer to inference context
 * @param node_name Name of node to stop at
 * @return STATUS_OK on success
 */
Status ONNX_Runtime_ExecuteUpTo(ONNX_InferenceContext* ctx, const char* node_name);

/* ------------------------------------------------------------------ */
/*  Operator Execution (Internal)                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Execute arithmetic operators (Add, Mul, etc.)
 */
Status ONNX_Execute_Arithmetic(ONNX_Node* node, ONNX_InferenceContext* ctx);

/**
 * @brief Execute MatMul operator
 */
Status ONNX_Execute_MatMul(ONNX_Node* node, ONNX_InferenceContext* ctx);

/**
 * @brief Execute ReLU activation
 */
Status ONNX_Execute_ReLU(ONNX_Node* node, ONNX_InferenceContext* ctx);

/**
 * @brief Execute Convolution operator
 */
Status ONNX_Execute_Conv(ONNX_Node* node, ONNX_InferenceContext* ctx);

/**
 * @brief Execute Pooling operators
 */
Status ONNX_Execute_Pool(ONNX_Node* node, ONNX_InferenceContext* ctx);

/* ------------------------------------------------------------------ */
/*  Performance & Profiling                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Get inference statistics
 * 
 * @param ctx Pointer to inference context
 * @param total_inferences Output: total number of inferences run
 * @param avg_time_us Output: average inference time in microseconds
 */
void ONNX_Runtime_GetStats(ONNX_InferenceContext* ctx,
                            uint64_t* total_inferences,
                            uint64_t* avg_time_us);

/**
 * @brief Reset performance counters
 * 
 * @param ctx Pointer to inference context
 */
void ONNX_Runtime_ResetStats(ONNX_InferenceContext* ctx);

/**
 * @brief Print performance report to UART
 * 
 * @param ctx Pointer to inference context
 */
void ONNX_Runtime_PrintProfile(ONNX_InferenceContext* ctx);

#endif /* ONNX_RUNTIME_H */
