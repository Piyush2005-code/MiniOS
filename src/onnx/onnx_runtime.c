/**
 * @file onnx_runtime.c
 * @brief ONNX inference runtime implementation
 */

#include "onnx/onnx_runtime.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "kernel/kmem.h"
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
    
    /* Allocate workspace from kernel heap if needed */
    if (workspace_size > 0) {
        ctx->workspace = KMEM_Alloc(workspace_size, KMEM_MIN_ALIGN);
        /* workspace may be NULL on OOM — caller can check workspace_size */
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

/* Simple math helpers for bare-metal environment */
static float fast_abs(float x)
{
    return x < 0.0f ? -x : x;
}

static float fast_log(float x)
{
    /* Simple Newton-Raphson or Halley's method is too complex for here,
       using a very basic approximation or just returning 0 for now.
       A real bare-metal math lib would implement this properly. */
    if (x <= 0.0f) return -1000.0f; // -infinity approximation

    // Very rough approximation: log(x) ~ (x-1)/(x+1) * 2
    float y = (x - 1.0f) / (x + 1.0f);
    return 2.0f * y * (1.0f + (y*y)/3.0f + (y*y*y*y)/5.0f);
}

static float fast_sqrt(float x)
{
    if (x <= 0.0f) return 0.0f;
    float res = x;
    for (int i = 0; i < 10; i++) {
        res = 0.5f * (res + x / res);
    }
    return res;
}

static float fast_ceil(float x)
{
    int i = (int)x;
    return (x == (float)i) ? x : ((x > 0.0f) ? (float)(i + 1) : (float)i);
}

static float fast_floor(float x)
{
    int i = (int)x;
    return (x == (float)i) ? x : ((x > 0.0f) ? (float)i : (float)(i - 1));
}

static float fast_sin(float x)
{
    // Wrap to [-pi, pi]
    float pi = 3.1415926535f;
    while (x > pi) x -= 2.0f * pi;
    while (x < -pi) x += 2.0f * pi;

    // Taylor series: x - x^3/3! + x^5/5! - x^7/7!
    float x2 = x * x;
    return x * (1.0f - x2/6.0f + (x2*x2)/120.0f - (x2*x2*x2)/5040.0f);
}

static float fast_cos(float x)
{
    // Wrap to [-pi, pi]
    float pi = 3.1415926535f;
    while (x > pi) x -= 2.0f * pi;
    while (x < -pi) x += 2.0f * pi;

    // Taylor series: 1 - x^2/2! + x^4/4! - x^6/6!
    float x2 = x * x;
    return 1.0f - x2/2.0f + (x2*x2)/24.0f - (x2*x2*x2)/720.0f;
}

static float fast_exp(float x)
{
    /* Very basic Taylor series approximation for exp(x)
     * e^x = 1 + x + x^2/2! + x^3/3! + x^4/4! + ...
     */
    if (x < -10.0f) return 0.0f;
    if (x > 10.0f) return 22026.46579f; /* approx exp(10) */

    float sum = 1.0f;
    float term = 1.0f;
    for (int i = 1; i < 10; i++) {
        term = term * x / i;
        sum += term;
    }
    return sum;
}

static float fast_tanh(float x)
{
    /* tanh(x) = (e^x - e^-x) / (e^x + e^-x) */
    if (x > 5.0f) return 1.0f;
    if (x < -5.0f) return -1.0f;

    float e_x = fast_exp(x);
    float e_nx = fast_exp(-x);
    return (e_x - e_nx) / (e_x + e_nx);
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

Status ONNX_Execute_Sub(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];

    if (a->dtype != ONNX_DTYPE_FLOAT32 || b->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t n = a->shape.total_elements;
    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = a_data[i] - b_data[i];
    }

    return STATUS_OK;
}

Status ONNX_Execute_Mul(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];

    if (a->dtype != ONNX_DTYPE_FLOAT32 || b->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t n = a->shape.total_elements;
    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = a_data[i] * b_data[i];
    }

    return STATUS_OK;
}

Status ONNX_Execute_Div(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];

    if (a->dtype != ONNX_DTYPE_FLOAT32 || b->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t n = a->shape.total_elements;
    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        /* Avoid division by zero strictly, though floats handle it */
        out_data[i] = (b_data[i] != 0.0f) ? (a_data[i] / b_data[i]) : 0.0f;
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

Status ONNX_Execute_Sigmoid(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = 1.0f / (1.0f + fast_exp(-in_data[i]));
    }

    return STATUS_OK;
}

Status ONNX_Execute_Tanh(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_tanh(in_data[i]);
    }

    return STATUS_OK;
}

Status ONNX_Execute_Softmax(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    /* For simplicity, assume 1D flat tensor softmax here */

    /* 1. Find max for numerical stability */
    float max_val = in_data[0];
    for (uint64_t i = 1; i < n; i++) {
        if (in_data[i] > max_val) max_val = in_data[i];
    }

    /* 2. Compute exp(x - max) and sum */
    float sum = 0.0f;
    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_exp(in_data[i] - max_val);
        sum += out_data[i];
    }

    /* 3. Normalize */
    if (sum != 0.0f) {
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] /= sum;
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_Reshape(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Reshape typically takes 2 inputs: data and shape */
    if (node->num_inputs != 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* data = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    /* In a real implementation, we'd read the shape tensor and update out->shape.
     * Here we just copy the data if memory is distinct, or do nothing if same memory. */
    if (data->data != out->data) {
        mem_copy(out->data, data->data, data->data_size);
    }

    return STATUS_OK;
}

Status ONNX_Execute_Flatten(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* data = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    /* Flatten changes shape but leaves memory layout identical */
    if (data->data != out->data) {
        mem_copy(out->data, data->data, data->data_size);
    }

    return STATUS_OK;
}

Status ONNX_Execute_Transpose(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    /* Basic 2D transpose for now */
    if (in->shape.ndim == 2) {
        uint32_t rows = in->shape.dims[0];
        uint32_t cols = in->shape.dims[1];

        float* in_data = (float*)in->data;
        float* out_data = (float*)out->data;

        for (uint32_t i = 0; i < rows; i++) {
            for (uint32_t j = 0; j < cols; j++) {
                out_data[j * rows + i] = in_data[i * cols + j];
            }
        }
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] Transpose only supports 2D currently\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

Status ONNX_Execute_BatchNorm(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* X, scale, B, mean, var -> Y */
    if (node->num_inputs < 5 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* x = node->inputs[0];
    ONNX_Tensor* scale = node->inputs[1];
    ONNX_Tensor* b = node->inputs[2];
    ONNX_Tensor* mean = node->inputs[3];
    ONNX_Tensor* var = node->inputs[4];
    ONNX_Tensor* y = node->outputs[0];

    float* x_data = (float*)x->data;
    float* scale_data = (float*)scale->data;
    float* b_data = (float*)b->data;
    float* mean_data = (float*)mean->data;
    float* var_data = (float*)var->data;
    float* y_data = (float*)y->data;

    float epsilon = 1e-5f;

    /* Assuming shape [N, C, ...] */
    uint32_t N = x->shape.dims[0];
    uint32_t C = x->shape.dims[1];
    uint32_t spatial = 1;
    for(uint32_t i=2; i<x->shape.ndim; i++) spatial *= x->shape.dims[i];

    for (uint32_t n = 0; n < N; n++) {
        for (uint32_t c = 0; c < C; c++) {
            float s = scale_data[c];
            float bias = b_data[c];
            float m = mean_data[c];
            float v = var_data[c];

            /* Fast inverse square root is better, but this works */
            float inv_std = 1.0f; /* We need sqrt here, hack it roughly or just leave it naive */
            float root_v = v + epsilon;
            /* Newton-Raphson for sqrt if we really need it, or we just trust input var. */

            /* Actually, we need to implement a bare-metal sqrt */
            float x_guess = root_v;
            for(int k=0; k<10; k++) {
                if (x_guess == 0.0f) break;
                x_guess = 0.5f * (x_guess + root_v / x_guess);
            }
            inv_std = 1.0f / x_guess;

            for (uint32_t sp = 0; sp < spatial; sp++) {
                uint32_t idx = n * C * spatial + c * spatial + sp;
                y_data[idx] = s * (x_data[idx] - m) * inv_std + bias;
            }
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_GEMM(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* A, B, C -> Y.  Y = alpha * A * B + beta * C */
    if (node->num_inputs < 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* A = node->inputs[0];
    ONNX_Tensor* B = node->inputs[1];
    ONNX_Tensor* C = (node->num_inputs > 2) ? node->inputs[2] : NULL;
    ONNX_Tensor* Y = node->outputs[0];

    float alpha = node->attributes.alpha != 0.0f ? node->attributes.alpha : 1.0f;
    float beta = node->attributes.beta != 0.0f ? node->attributes.beta : 1.0f;

    /* Simplified, no transpose attribute handling yet */
    uint32_t M = A->shape.dims[0];
    uint32_t K = A->shape.dims[1];
    uint32_t N = B->shape.dims[1];

    float* a_data = (float*)A->data;
    float* b_data = (float*)B->data;
    float* c_data = C ? (float*)C->data : NULL;
    float* y_data = (float*)Y->data;

    for (uint32_t i = 0; i < M; i++) {
        for (uint32_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < K; k++) {
                sum += a_data[i * K + k] * b_data[k * N + j];
            }
            float c_val = c_data ? c_data[j] : 0.0f; // Assume 1D broadcast for C
            y_data[i * N + j] = alpha * sum + beta * c_val;
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_Concat(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs < 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* out = node->outputs[0];
    int axis = node->attributes.axis;
    if (axis < 0) axis += out->shape.ndim;

    float* out_data = (float*)out->data;
    uint32_t offset = 0;

    /* Very simplified concat: only handles axis=0 concatenation correctly for 1D/2D flat layouts.
     * Real concat requires multi-dimensional memory striding. */
    if (axis == 0) {
        for (uint32_t i = 0; i < node->num_inputs; i++) {
            ONNX_Tensor* in = node->inputs[i];
            mem_copy(out_data + offset, in->data, in->data_size);
            offset += in->shape.total_elements;
        }
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] Concat currently only supports axis=0\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

Status ONNX_Execute_LeakyRelu(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    float alpha = node->attributes.alpha != 0.0f ? node->attributes.alpha : 0.01f;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = (in_data[i] > 0.0f) ? in_data[i] : alpha * in_data[i];
    }

    return STATUS_OK;
}

Status ONNX_Execute_GlobalAveragePool(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->shape.ndim < 3) return STATUS_ERROR_NOT_SUPPORTED;

    uint32_t batch = in->shape.dims[0];
    uint32_t channels = in->shape.dims[1];

    uint32_t spatial_elements = 1;
    for (uint32_t i = 2; i < in->shape.ndim; i++) {
        spatial_elements *= in->shape.dims[i];
    }

    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint32_t b = 0; b < batch; b++) {
        for (uint32_t c = 0; c < channels; c++) {
            float sum = 0.0f;
            for (uint32_t s = 0; s < spatial_elements; s++) {
                sum += in_data[b * channels * spatial_elements + c * spatial_elements + s];
            }
            out_data[b * channels + c] = spatial_elements > 0 ? (sum / spatial_elements) : 0.0f;
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_Squeeze(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Typically data + optional axes */
    if (node->num_inputs < 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* data = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    /* In a full implementation we'd read axes and compute new shape.
     * Memory layout is unchanged. */
    if (data->data != out->data) {
        mem_copy(out->data, data->data, data->data_size);
    }

    return STATUS_OK;
}

Status ONNX_Execute_Unsqueeze(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Typically data + axes */
    if (node->num_inputs < 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* data = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    /* Memory layout unchanged. */
    if (data->data != out->data) {
        mem_copy(out->data, data->data, data->data_size);
    }

    return STATUS_OK;
}

Status ONNX_Execute_Cast(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    /* Trivial implementation: only handles Float to Float (no-op)
     * For full support we need proper type conversion loops */
    if (in->dtype == ONNX_DTYPE_FLOAT32 && out->dtype == ONNX_DTYPE_FLOAT32) {
        if (in->data != out->data) {
            mem_copy(out->data, in->data, in->data_size);
        }
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] Cast currently only supports float->float\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

Status ONNX_Execute_Arithmetic(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    switch (node->op_type) {
        case ONNX_OP_ADD:
            return ONNX_Execute_Add(node, ctx);
        case ONNX_OP_SUB:
            return ONNX_Execute_Sub(node, ctx);
        case ONNX_OP_MUL:
            return ONNX_Execute_Mul(node, ctx);
        case ONNX_OP_DIV:
            return ONNX_Execute_Div(node, ctx);
        default:
            return STATUS_ERROR_UNSUPPORTED_OPERATOR;
    }
}

Status ONNX_Execute_Conv(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Conv takes X, W, and optional B */
    if (node->num_inputs < 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* x = node->inputs[0];
    ONNX_Tensor* w = node->inputs[1];
    ONNX_Tensor* b = (node->num_inputs > 2) ? node->inputs[2] : NULL;
    ONNX_Tensor* y = node->outputs[0];

    /* Simplified 2D convolution for demo (N=1)
     * X: [1, C_in, H_in, W_in]
     * W: [C_out, C_in, K_h, K_w]
     * Y: [1, C_out, H_out, W_out]
     */
    if (x->shape.ndim != 4 || w->shape.ndim != 4) {
        HAL_UART_PutString("[ONNX] Conv only supports 4D tensors currently\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint32_t c_in = x->shape.dims[1];
    uint32_t h_in = x->shape.dims[2];
    uint32_t w_in = x->shape.dims[3];

    uint32_t c_out = w->shape.dims[0];
    uint32_t k_h = w->shape.dims[2];
    uint32_t k_w = w->shape.dims[3];

    uint32_t h_out = y->shape.dims[2];
    uint32_t w_out = y->shape.dims[3];

    float* x_data = (float*)x->data;
    float* w_data = (float*)w->data;
    float* b_data = b ? (float*)b->data : NULL;
    float* y_data = (float*)y->data;

    /* Naive convolution loops */
    for (uint32_t oc = 0; oc < c_out; oc++) {
        for (uint32_t oh = 0; oh < h_out; oh++) {
            for (uint32_t ow = 0; ow < w_out; ow++) {
                float sum = b_data ? b_data[oc] : 0.0f;

                for (uint32_t ic = 0; ic < c_in; ic++) {
                    for (uint32_t kh = 0; kh < k_h; kh++) {
                        for (uint32_t kw = 0; kw < k_w; kw++) {
                            uint32_t ih = oh + kh; // assuming stride=1, pad=0
                            uint32_t iw = ow + kw;

                            if (ih < h_in && iw < w_in) {
                                float val_x = x_data[ic * h_in * w_in + ih * w_in + iw];
                                float val_w = w_data[oc * c_in * k_h * k_w + ic * k_h * k_w + kh * k_w + kw];
                                sum += val_x * val_w;
                            }
                        }
                    }
                }
                y_data[oc * h_out * w_out + oh * w_out + ow] = sum;
            }
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_MaxPool(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* x = node->inputs[0];
    ONNX_Tensor* y = node->outputs[0];

    if (x->shape.ndim != 4) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint32_t c = x->shape.dims[1];
    uint32_t h_in = x->shape.dims[2];
    uint32_t w_in = x->shape.dims[3];

    uint32_t h_out = y->shape.dims[2];
    uint32_t w_out = y->shape.dims[3];

    /* Assume 2x2 pool for now */
    uint32_t k_h = 2, k_w = 2;
    uint32_t stride_h = 2, stride_w = 2;

    float* x_data = (float*)x->data;
    float* y_data = (float*)y->data;

    for (uint32_t ic = 0; ic < c; ic++) {
        for (uint32_t oh = 0; oh < h_out; oh++) {
            for (uint32_t ow = 0; ow < w_out; ow++) {
                float max_val = -1e9; // small float

                for (uint32_t kh = 0; kh < k_h; kh++) {
                    for (uint32_t kw = 0; kw < k_w; kw++) {
                        uint32_t ih = oh * stride_h + kh;
                        uint32_t iw = ow * stride_w + kw;

                        if (ih < h_in && iw < w_in) {
                            float val = x_data[ic * h_in * w_in + ih * w_in + iw];
                            if (val > max_val) max_val = val;
                        }
                    }
                }
                y_data[ic * h_out * w_out + oh * w_out + ow] = max_val;
            }
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_AvgPool(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    if (node->num_inputs != 1 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* x = node->inputs[0];
    ONNX_Tensor* y = node->outputs[0];

    if (x->shape.ndim != 4) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint32_t c = x->shape.dims[1];
    uint32_t h_in = x->shape.dims[2];
    uint32_t w_in = x->shape.dims[3];

    uint32_t h_out = y->shape.dims[2];
    uint32_t w_out = y->shape.dims[3];

    /* Assume 2x2 pool for now */
    uint32_t k_h = 2, k_w = 2;
    uint32_t stride_h = 2, stride_w = 2;

    float* x_data = (float*)x->data;
    float* y_data = (float*)y->data;

    for (uint32_t ic = 0; ic < c; ic++) {
        for (uint32_t oh = 0; oh < h_out; oh++) {
            for (uint32_t ow = 0; ow < w_out; ow++) {
                float sum = 0.0f;
                int count = 0;

                for (uint32_t kh = 0; kh < k_h; kh++) {
                    for (uint32_t kw = 0; kw < k_w; kw++) {
                        uint32_t ih = oh * stride_h + kh;
                        uint32_t iw = ow * stride_w + kw;

                        if (ih < h_in && iw < w_in) {
                            sum += x_data[ic * h_in * w_in + ih * w_in + iw];
                            count++;
                        }
                    }
                }
                y_data[ic * h_out * w_out + oh * w_out + ow] = count > 0 ? (sum / count) : 0.0f;
            }
        }
    }

    return STATUS_OK;
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

        case ONNX_OP_SIGMOID:
            status = ONNX_Execute_Sigmoid(node, ctx);
            break;

        case ONNX_OP_TANH:
            status = ONNX_Execute_Tanh(node, ctx);
            break;

        case ONNX_OP_SOFTMAX:
            status = ONNX_Execute_Softmax(node, ctx);
            break;

        case ONNX_OP_RESHAPE:
            status = ONNX_Execute_Reshape(node, ctx);
            break;

        case ONNX_OP_TRANSPOSE:
            status = ONNX_Execute_Transpose(node, ctx);
            break;

        case ONNX_OP_FLATTEN:
            status = ONNX_Execute_Flatten(node, ctx);
            break;

        case ONNX_OP_CONV:
            status = ONNX_Execute_Conv(node, ctx);
            break;
            
        case ONNX_OP_MAXPOOL:
            status = ONNX_Execute_MaxPool(node, ctx);
            break;

        case ONNX_OP_AVGPOOL:
            status = ONNX_Execute_AvgPool(node, ctx);
            break;

        case ONNX_OP_BATCHNORM:
            status = ONNX_Execute_BatchNorm(node, ctx);
            break;

        case ONNX_OP_GEMM:
            status = ONNX_Execute_GEMM(node, ctx);
            break;

        case ONNX_OP_CONCAT:
            status = ONNX_Execute_Concat(node, ctx);
            break;

        case ONNX_OP_LEAKYRELU:
            status = ONNX_Execute_LeakyRelu(node, ctx);
            break;

        case ONNX_OP_GLOBALAVERAGEPOOL:
            status = ONNX_Execute_GlobalAveragePool(node, ctx);
            break;

        case ONNX_OP_SQUEEZE:
            status = ONNX_Execute_Squeeze(node, ctx);
            break;

        case ONNX_OP_UNSQUEEZE:
            status = ONNX_Execute_Unsqueeze(node, ctx);
            break;

        case ONNX_OP_CAST:
            status = ONNX_Execute_Cast(node, ctx);
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
