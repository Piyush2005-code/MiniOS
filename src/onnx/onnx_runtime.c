/**
 * @file onnx_runtime.c
 * @brief ONNX inference runtime implementation
 */

#include "onnx/onnx_runtime.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "status.h"

/* Simple memory operations */
static void mem_zero(void* ptr, uint64_t size)
{
    uint8_t* p = (uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void mem_copy(void* dst, const void* src, uint64_t size)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

/* ------------------------------------------------------------------ */
/*  Runtime Initialization                                            */
/* ------------------------------------------------------------------ */

Status ONNX_Runtime_Init(ONNX_InferenceContext* ctx,
                          ONNX_Graph* graph,
                          uint64_t workspace_size)
{
    if (!ctx || !graph) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    mem_zero(ctx, sizeof(ONNX_InferenceContext));
    
    ctx->graph = graph;
    ctx->workspace_size = workspace_size;
    
    /* Allocate workspace if needed */
    if (workspace_size > 0) {
        /* In real implementation, use your memory allocator */
        /* For now, just set to NULL - workspace will be allocated separately */
        ctx->workspace = NULL;
    }
    
    ctx->total_inferences = 0;
    ctx->total_time_us = 0;
    
    return STATUS_OK;
}

void ONNX_Runtime_Cleanup(ONNX_InferenceContext* ctx)
{
    if (!ctx) return;
    
    if (ctx->workspace) {
        /* Free workspace */
        ctx->workspace = NULL;
    }
    
    mem_zero(ctx, sizeof(ONNX_InferenceContext));
}

/* ------------------------------------------------------------------ */
/*  Operator Implementations                                          */
/* ------------------------------------------------------------------ */

Status ONNX_Execute_Add(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    
    if (node->num_inputs != 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];
    
    /* Only support float32 for now */
    if (a->dtype != ONNX_DTYPE_FLOAT32 || b->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }
    
    uint64_t n = a->shape.total_elements;
    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;
    
    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = a_data[i] + b_data[i];
    }
    
    return STATUS_OK;
}

Status ONNX_Execute_MatMul(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    
    if (node->num_inputs != 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];
    
    /* Only support float32 for now */
    if (a->dtype != ONNX_DTYPE_FLOAT32 || b->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }
    
    /* Get dimensions: A[M, K] * B[K, N] = C[M, N] */
    if (a->shape.ndim != 2 || b->shape.ndim != 2) {
        return STATUS_ERROR_SHAPE_MISMATCH;
    }
    
    uint64_t M = a->shape.dims[0];
    uint64_t K = a->shape.dims[1];
    uint64_t N = b->shape.dims[1];
    
    if (b->shape.dims[0] != K) {
        return STATUS_ERROR_SHAPE_MISMATCH;
    }
    
    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;
    
    /* Simple matrix multiplication (not optimized) */
    for (uint64_t i = 0; i < M; i++) {
        for (uint64_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (uint64_t k = 0; k < K; k++) {
                sum += a_data[i * K + k] * b_data[k * N + j];
            }
            out_data[i * N + j] = sum;
        }
    }
    
    return STATUS_OK;
}

Status ONNX_Execute_ReLU(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    
    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];
    
    /* Only support float32 for now */
    if (in->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }
    
    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;
    
    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = (in_data[i] > 0.0f) ? in_data[i] : 0.0f;
    }
    
    return STATUS_OK;
}

Status ONNX_Execute_Arithmetic(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    switch (node->op_type) {
        case ONNX_OP_ADD:
            return ONNX_Execute_Add(node, ctx);
        default:
            return STATUS_ERROR_UNSUPPORTED_OPERATOR;
    }
}

Status ONNX_Execute_Conv(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    /* Convolution is complex - stub for now */
    (void)node;
    (void)ctx;
    HAL_UART_PutString("[ONNX] Warning: Conv not yet implemented\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

Status ONNX_Execute_Pool(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    /* Pooling is complex - stub for now */
    (void)node;
    (void)ctx;
    HAL_UART_PutString("[ONNX] Warning: Pooling not yet implemented\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ */
/*  Node Execution                                                    */
/* ------------------------------------------------------------------ */

Status ONNX_Runtime_ExecuteNode(ONNX_InferenceContext* ctx, ONNX_Node* node)
{
    if (!ctx || !node) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    Status status = STATUS_OK;
    
    /* Dispatch based on operator type */
    switch (node->op_type) {
        case ONNX_OP_ADD:
        case ONNX_OP_SUB:
        case ONNX_OP_MUL:
        case ONNX_OP_DIV:
            status = ONNX_Execute_Arithmetic(node, ctx);
            break;
            
        case ONNX_OP_MATMUL:
            status = ONNX_Execute_MatMul(node, ctx);
            break;
            
        case ONNX_OP_RELU:
            status = ONNX_Execute_ReLU(node, ctx);
            break;
            
        case ONNX_OP_CONV:
            status = ONNX_Execute_Conv(node, ctx);
            break;
            
        case ONNX_OP_MAXPOOL:
        case ONNX_OP_AVGPOOL:
            status = ONNX_Execute_Pool(node, ctx);
            break;
            
        default:
            HAL_UART_PutString("[ONNX] Error: Unsupported operator: ");
            HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
            HAL_UART_PutString("\n");
            return STATUS_ERROR_UNSUPPORTED_OPERATOR;
    }
    
    if (status == STATUS_OK) {
        node->exec_count++;
    }
    
    return status;
}

/* ------------------------------------------------------------------ */
/*  Inference Execution                                               */
/* ------------------------------------------------------------------ */

Status ONNX_Runtime_Inference(ONNX_InferenceContext* ctx,
                                ONNX_Tensor** inputs,
                                uint32_t num_inputs,
                                ONNX_Tensor** outputs,
                                uint32_t num_outputs)
{
    if (!ctx || !ctx->graph) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    ONNX_Graph* graph = ctx->graph;
    
    /* Validate inputs */
    if (num_inputs != graph->num_inputs) {
        HAL_UART_PutString("[ONNX] Error: Expected ");
        HAL_UART_PutDec(graph->num_inputs);
        HAL_UART_PutString(" inputs, got ");
        HAL_UART_PutDec(num_inputs);
        HAL_UART_PutString("\n");
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Copy input data to graph input tensors */
    for (uint32_t i = 0; i < num_inputs; i++) {
        ONNX_Tensor* graph_input = graph->inputs[i];
        ONNX_Tensor* user_input = inputs[i];
        
        if (graph_input->data_size != user_input->data_size) {
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
        
        mem_copy(graph_input->data, user_input->data, user_input->data_size);
    }
    
    /* Execute all nodes in scheduled order */
    HAL_UART_PutString("[ONNX] Starting inference...\n");
    
    for (uint32_t i = 0; i < graph->schedule_length; i++) {
        ONNX_Node* node = graph->exec_schedule[i];
        
        HAL_UART_PutString("  Executing: ");
        HAL_UART_PutString(node->name);
        HAL_UART_PutString(" (");
        HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
        HAL_UART_PutString(")\n");
        
        Status status = ONNX_Runtime_ExecuteNode(ctx, node);
        if (status != STATUS_OK) {
            HAL_UART_PutString("[ONNX] Error: Node execution failed: ");
            HAL_UART_PutString(STATUS_ToString(status));
            HAL_UART_PutString("\n");
            return status;
        }
    }
    
    /* Copy outputs */
    if (num_outputs != graph->num_outputs) {
        HAL_UART_PutString("[ONNX] Error: Expected ");
        HAL_UART_PutDec(graph->num_outputs);
        HAL_UART_PutString(" outputs, got ");
        HAL_UART_PutDec(num_outputs);
        HAL_UART_PutString("\n");
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    for (uint32_t i = 0; i < num_outputs; i++) {
        ONNX_Tensor* graph_output = graph->outputs[i];
        outputs[i] = graph_output;
    }
    
    ctx->total_inferences++;
    
    HAL_UART_PutString("[ONNX] Inference complete!\n");
    
    return STATUS_OK;
}

Status ONNX_Runtime_ExecuteUpTo(ONNX_InferenceContext* ctx, const char* node_name)
{
    if (!ctx || !ctx->graph || !node_name) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    ONNX_Graph* graph = ctx->graph;
    
    /* Find the target node */
    ONNX_Node* target_node = NULL;
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        if (graph->nodes[i].name[0] == node_name[0]) {  /* Simple string comparison */
            bool match = true;
            for (uint32_t j = 1; node_name[j] != '\0'; j++) {
                if (graph->nodes[i].name[j] != node_name[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                target_node = &graph->nodes[i];
                break;
            }
        }
    }
    
    if (!target_node) {
        HAL_UART_PutString("[ONNX] Error: Node not found: ");
        HAL_UART_PutString(node_name);
        HAL_UART_PutString("\n");
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Execute nodes up to and including target */
    for (uint32_t i = 0; i < graph->schedule_length; i++) {
        ONNX_Node* node = graph->exec_schedule[i];
        
        Status status = ONNX_Runtime_ExecuteNode(ctx, node);
        if (status != STATUS_OK) {
            return status;
        }
        
        if (node == target_node) {
            break;
        }
    }
    
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Performance & Profiling                                           */
/* ------------------------------------------------------------------ */

void ONNX_Runtime_GetStats(ONNX_InferenceContext* ctx,
                            uint64_t* total_inferences,
                            uint64_t* avg_time_us)
{
    if (!ctx) return;
    
    if (total_inferences) {
        *total_inferences = ctx->total_inferences;
    }
    
    if (avg_time_us) {
        if (ctx->total_inferences > 0) {
            *avg_time_us = ctx->total_time_us / ctx->total_inferences;
        } else {
            *avg_time_us = 0;
        }
    }
}

void ONNX_Runtime_ResetStats(ONNX_InferenceContext* ctx)
{
    if (!ctx) return;
    
    ctx->total_inferences = 0;
    ctx->total_time_us = 0;
    
    /* Reset per-node stats */
    if (ctx->graph) {
        for (uint32_t i = 0; i < ctx->graph->num_nodes; i++) {
            ctx->graph->nodes[i].exec_count = 0;
            ctx->graph->nodes[i].exec_time_us = 0;
        }
    }
}

void ONNX_Runtime_PrintProfile(ONNX_InferenceContext* ctx)
{
    if (!ctx || !ctx->graph) return;
    
    HAL_UART_PutString("\n========== Runtime Profile ==========\n");
    HAL_UART_PutString("Total inferences: ");
    HAL_UART_PutDec((uint32_t)ctx->total_inferences);
    HAL_UART_PutString("\n");
    
    if (ctx->total_inferences > 0) {
        HAL_UART_PutString("Average time: ");
        HAL_UART_PutDec((uint32_t)(ctx->total_time_us / ctx->total_inferences));
        HAL_UART_PutString(" us\n");
    }
    
    HAL_UART_PutString("\nPer-node statistics:\n");
    ONNX_Graph_PrintStats(ctx->graph);
}
