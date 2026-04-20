/**
 * @file onnx_runtime.c
 * @brief ONNX inference runtime implementation
 */

#include "onnx/onnx_runtime.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "kernel/kmem.h"
#include "kernel/thread.h"
#include "lib/string.h"
#include "status.h"

static volatile bool g_runtime_verbose = true;
static volatile bool g_runtime_yield_between_nodes = true;
static volatile bool g_runtime_node_profiling = true;
static volatile bool g_runtime_prepare_node_outputs = true;
static volatile ONNX_Node* g_runtime_current_node = NULL;
static volatile uint32_t g_runtime_copy_warn_count = 0;

static bool runtime_copy_range_invalid(const void* ptr, uint64_t size)
{
    if (!ptr) {
        return true;
    }

    if (size == 0) {
        return false;
    }

    uint64_t start = (uint64_t)(uintptr_t)ptr;
    uint64_t end = start + size - 1;

    if (end < start) {
        return true;
    }

    if (start < 0x40000000ULL || end >= 0x80000000ULL) {
        return true;
    }

    return false;
}

/* Simple memory operations */
static void mem_zero(void* ptr, uint64_t size)
{
    if (!ptr || size == 0) {
        return;
    }
    memset(ptr, 0, (size_t)size);
}

static void mem_copy(void* dst, const void* src, uint64_t size)
{
    if (dst == src || size == 0) {
        return;
    }

    if (runtime_copy_range_invalid(dst, size) || runtime_copy_range_invalid(src, size)) {
        if (g_runtime_copy_warn_count < 8) {
            HAL_UART_PutString("[ONNX mem_copy] invalid range: dst=");
            HAL_UART_PutHex((uint64_t)(uintptr_t)dst);
            HAL_UART_PutString(" src=");
            HAL_UART_PutHex((uint64_t)(uintptr_t)src);
            HAL_UART_PutString(" size=");
            HAL_UART_PutDec((uint32_t)size);
            if (g_runtime_current_node) {
                const ONNX_Node* node = (const ONNX_Node*)g_runtime_current_node;
                HAL_UART_PutString(" node='");
                HAL_UART_PutString(node->name);
                HAL_UART_PutString("' op=");
                HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
            }
            HAL_UART_PutString("\n");
            g_runtime_copy_warn_count++;
        }
        return;
    }

    memcpy(dst, src, (size_t)size);
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

void ONNX_Runtime_SetVerbose(bool enable)
{
    g_runtime_verbose = enable;
}

bool ONNX_Runtime_GetVerbose(void)
{
    return g_runtime_verbose;
}

void ONNX_Runtime_SetYieldBetweenNodes(bool enable)
{
    g_runtime_yield_between_nodes = enable;
}

bool ONNX_Runtime_GetYieldBetweenNodes(void)
{
    return g_runtime_yield_between_nodes;
}

void ONNX_Runtime_SetNodeProfiling(bool enable)
{
    g_runtime_node_profiling = enable;
}

bool ONNX_Runtime_GetNodeProfiling(void)
{
    return g_runtime_node_profiling;
}

void ONNX_Runtime_SetPrepareNodeOutputs(bool enable)
{
    g_runtime_prepare_node_outputs = enable;
}

bool ONNX_Runtime_GetPrepareNodeOutputs(void)
{
    return g_runtime_prepare_node_outputs;
}

const ONNX_Node* ONNX_Runtime_GetCurrentNode(void)
{
    return (const ONNX_Node*)g_runtime_current_node;
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

static float fast_rsqrt(float x)
{
    if (x <= 0.0f) return 0.0f;

    union {
        float f;
        uint32_t i;
    } conv;

    conv.f = x;
    conv.i = 0x5f3759dfU - (conv.i >> 1);

    float y = conv.f;
    float half_x = 0.5f * x;

    /* Two NR iterations provide enough accuracy for BN normalization. */
    y = y * (1.5f - half_x * y * y);
    y = y * (1.5f - half_x * y * y);
    return y;
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
    /* Fold x to [-pi, pi] */
    const float twopi = 6.283185307179586f;
    const float invtwopi = 0.159154943091895f;
    float k = x * invtwopi;
    int n = (int)(k > 0.0f ? k + 0.5f : k - 0.5f);
    x = x - n * twopi;

    /* Minimax polynomial for sin(x) */
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.166666666f + x2 * (0.008333333f - x2 * 0.000198412f)));
}

static float fast_cos(float x)
{
    const float pi_half = 1.5707963267948966f;
    return fast_sin(x + pi_half); /* cos(x) = sin(x + pi/2) */
}

static float fast_exp(float x)
{
    if (x < -88.0f) return 0.0f;
    if (x > 88.0f) return 3.402823466e+38f;
    
    /* Range reduction: x = n * ln(2) + r */
    float k = x * 1.4426950408889634f; /* x / ln(2) */
    int n = (int)(k > 0.0f ? k + 0.5f : k - 0.5f);
    float r = x - n * 0.6931471805599453f;

    /* Horner polynomial degree 3 for e^r */
    float p = 1.0f + r * (1.0f + r * (0.5f + r * 0.1666666667f));

    /* Scale by 2^n via IEEE 754 exponent field manipulation */
    union { float f; uint32_t i; } u;
    u.i = (uint32_t)(n + 127) << 23;
    return p * u.f;
}

static float fast_tanh(float x)
{
    /* tanh(x) = (e^x - e^-x) / (e^x + e^-x) */
    if (x >  5.0f) return  1.0f;
    if (x < -5.0f) return -1.0f;

    float e_x  = fast_exp(x);
    float e_nx = fast_exp(-x);
    return (e_x - e_nx) / (e_x + e_nx);
}

/* ------------------------------------------------------------------ */
/*  Operator Implementations                                          */
/* ------------------------------------------------------------------ */

Status ONNX_Execute_Add(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Identify scalar constants that we don't handle well in broadcasting yet.
       If the node has 1 input and we are in an Add block, that shouldn't happen.
       Let's catch 0 or 1 inputs and print details to figure out why */
    if (node->num_inputs != 2 || node->num_outputs != 1) {
        HAL_UART_PutString("[ONNX] Execute_Add Error: num_inputs=");
        HAL_UART_PutDec(node->num_inputs);
        HAL_UART_PutString(" num_outputs=");
        HAL_UART_PutDec(node->num_outputs);
        HAL_UART_PutString(" node_name=");
        HAL_UART_PutString(node->name);
        HAL_UART_PutString("\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];

    /* Only support float32 for now */
    if (a->dtype != ONNX_DTYPE_FLOAT32 || b->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint64_t a_n = a->shape.total_elements;
    uint64_t b_n = b->shape.total_elements;
    uint64_t n = (a_n > b_n) ? a_n : b_n;
    if (n == 0) n = 1; /* In case both are 0, prevent loop issue, but usually min 1 */

    /* Update output shape if not already set by broadcasting rules.
     * Simple hack: just copy the shape of the larger tensor.
     */
    if (out->shape.total_elements <= 1 && n > 1) {
        if (a_n > b_n) {
            out->shape = a->shape;
        } else {
            out->shape = b->shape;
        }
        out->shape.total_elements = n;
        out->data_size = n * ONNX_GetDataTypeSize(ONNX_DTYPE_FLOAT32);
        ONNX_Graph_AllocateTensor(ctx->graph, out);
    }

    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    if (!a_data || !b_data || !out_data) return STATUS_ERROR_OUT_OF_MEMORY;

    /* Fast paths for common broadcasting patterns. */
    if (a_n == b_n) {
        for (uint64_t i = 0; i < a_n; i++) {
            out_data[i] = a_data[i] + b_data[i];
        }
        return STATUS_OK;
    }

    if (b_n == 1) {
        float b0 = b_data[0];
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] = a_data[i] + b0;
        }
        return STATUS_OK;
    }

    if (a_n == 1) {
        float a0 = a_data[0];
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] = a0 + b_data[i];
        }
        return STATUS_OK;
    }

    /* Proper broadcasting:
     * If one tensor is size 1, it's a scalar add.
     * If both are same size, element-wise add.
     * If one is larger, use simple repeating.
     */
    for (uint64_t i = 0; i < n; i++) {
        float a_val = (a_n <= 1) ? (a_data ? a_data[0] : 0.0f) : a_data[i % a_n];
        float b_val = (b_n <= 1) ? (b_data ? b_data[0] : 0.0f) : b_data[i % b_n];
        out_data[i] = a_val + b_val;
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

    uint64_t a_n = a->shape.total_elements;
    uint64_t b_n = b->shape.total_elements;
    uint64_t n = (a_n > b_n) ? a_n : b_n;

    if (out->shape.total_elements <= 1 && n > 1) {
        if (a_n > b_n) {
            out->shape = a->shape;
        } else {
            out->shape = b->shape;
        }
        out->shape.total_elements = n;
        out->data_size = n * ONNX_GetDataTypeSize(ONNX_DTYPE_FLOAT32);
        ONNX_Graph_AllocateTensor(ctx->graph, out);
    }

    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    if (!a_data || !b_data || !out_data) return STATUS_ERROR_OUT_OF_MEMORY;

    if (a_n == b_n) {
        for (uint64_t i = 0; i < a_n; i++) {
            out_data[i] = a_data[i] - b_data[i];
        }
        return STATUS_OK;
    }

    if (b_n == 1) {
        float b0 = b_data[0];
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] = a_data[i] - b0;
        }
        return STATUS_OK;
    }

    if (a_n == 1) {
        float a0 = a_data[0];
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] = a0 - b_data[i];
        }
        return STATUS_OK;
    }

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = a_data[i % a_n] - b_data[i % b_n];
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

    uint64_t a_n = a->shape.total_elements;
    uint64_t b_n = b->shape.total_elements;
    uint64_t n = (a_n > b_n) ? a_n : b_n;

    if (out->shape.total_elements <= 1 && n > 1) {
        if (a_n > b_n) {
            out->shape = a->shape;
        } else {
            out->shape = b->shape;
        }
        out->shape.total_elements = n;
        out->data_size = n * ONNX_GetDataTypeSize(ONNX_DTYPE_FLOAT32);
        ONNX_Graph_AllocateTensor(ctx->graph, out);
    }

    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    if (!a_data || !b_data || !out_data) return STATUS_ERROR_OUT_OF_MEMORY;

    if (a_n == b_n) {
        for (uint64_t i = 0; i < a_n; i++) {
            out_data[i] = a_data[i] * b_data[i];
        }
        return STATUS_OK;
    }

    if (b_n == 1) {
        float b0 = b_data[0];
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] = a_data[i] * b0;
        }
        return STATUS_OK;
    }

    if (a_n == 1) {
        float a0 = a_data[0];
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] = a0 * b_data[i];
        }
        return STATUS_OK;
    }

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = a_data[i % a_n] * b_data[i % b_n];
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

    uint64_t a_n = a->shape.total_elements;
    uint64_t b_n = b->shape.total_elements;
    uint64_t n = (a_n > b_n) ? a_n : b_n;

    if (out->shape.total_elements <= 1 && n > 1) {
        if (a_n > b_n) {
            out->shape = a->shape;
        } else {
            out->shape = b->shape;
        }
        out->shape.total_elements = n;
        out->data_size = n * ONNX_GetDataTypeSize(ONNX_DTYPE_FLOAT32);
        ONNX_Graph_AllocateTensor(ctx->graph, out);
    }

    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    if (!a_data || !b_data || !out_data) return STATUS_ERROR_OUT_OF_MEMORY;

    if (a_n == b_n) {
        for (uint64_t i = 0; i < a_n; i++) {
            float bv = b_data[i];
            out_data[i] = (bv != 0.0f) ? (a_data[i] / bv) : 0.0f;
        }
        return STATUS_OK;
    }

    if (b_n == 1) {
        float b0 = b_data[0];
        if (b0 == 0.0f) {
            for (uint64_t i = 0; i < n; i++) {
                out_data[i] = 0.0f;
            }
        } else {
            for (uint64_t i = 0; i < n; i++) {
                out_data[i] = a_data[i] / b0;
            }
        }
        return STATUS_OK;
    }

    if (a_n == 1) {
        float a0 = a_data[0];
        for (uint64_t i = 0; i < n; i++) {
            float bv = b_data[i];
            out_data[i] = (bv != 0.0f) ? (a0 / bv) : 0.0f;
        }
        return STATUS_OK;
    }

    for (uint64_t i = 0; i < n; i++) {
        float bv = b_data[i % b_n];
        out_data[i] = (bv != 0.0f) ? (a_data[i % a_n] / bv) : 0.0f;
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
    uint64_t M, K, N;

    if (a->shape.ndim == 2 && b->shape.ndim == 2) {
        M = a->shape.dims[0];
        K = a->shape.dims[1];
        N = b->shape.dims[1];

        if (b->shape.dims[0] != K) {
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
    } else if (a->shape.ndim > 2 && b->shape.ndim == 2) {
        /* Broadcasting: flatten leading dims into M */
        M = 1;
        for (uint32_t i = 0; i < a->shape.ndim - 1; i++) {
            M *= a->shape.dims[i];
        }
        K = a->shape.dims[a->shape.ndim - 1];
        N = b->shape.dims[1];

        if (b->shape.dims[0] != K) {
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
    } else {
        /* Other broadcast cases not supported yet */
        return STATUS_ERROR_SHAPE_MISMATCH;
    }

    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    /* Set output shape correctly (flattening leading dims for output, or preserving them)
     * For simplicity, if we broadcasted, we just output a 2D tensor [M, N] here
     * since the original code only assumed 2D anyway. If we want to preserve shape:
     */
    if (a->shape.ndim > 2) {
        out->shape.ndim = a->shape.ndim;
        for (uint32_t i = 0; i < a->shape.ndim - 1; i++) {
            out->shape.dims[i] = a->shape.dims[i];
        }
        out->shape.dims[a->shape.ndim - 1] = N;
    } else {
        out->shape.ndim = 2;
        out->shape.dims[0] = M;
        out->shape.dims[1] = N;
    }
    out->shape.total_elements = M * N;
    
    /* Cache-friendly IKJ matmul: contiguous reads from B rows and output rows. */
    for (uint64_t i = 0; i < M; i++) {
        const float* a_row = a_data + (i * K);
        float* out_row = out_data + (i * N);

        for (uint64_t j = 0; j < N; j++) {
            out_row[j] = 0.0f;
        }

        for (uint64_t k = 0; k < K; k++) {
            float a_val = a_row[k];
            const float* b_row = b_data + (k * N);

            uint64_t j = 0;
            for (; j + 3 < N; j += 4) {
                out_row[j + 0] += a_val * b_row[j + 0];
                out_row[j + 1] += a_val * b_row[j + 1];
                out_row[j + 2] += a_val * b_row[j + 2];
                out_row[j + 3] += a_val * b_row[j + 3];
            }
            for (; j < N; j++) {
                out_row[j] += a_val * b_row[j];
            }
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
    ONNX_Tensor* shape = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];

    if (data->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    /* Update shape based on the second input */
    if (shape && shape->data) {
        uint32_t ndim = shape->shape.total_elements;
        if (ndim <= ONNX_MAX_DIMS) {
            out->shape.ndim = ndim;
            uint64_t total = 1;
            int unknown_dim_idx = -1;

            for (uint32_t i = 0; i < ndim; i++) {
                int64_t dim_i = 0;
                if (shape->dtype == ONNX_DTYPE_INT64) {
                    dim_i = ((int64_t*)shape->data)[i];
                } else if (shape->dtype == ONNX_DTYPE_INT32) {
                    dim_i = ((int32_t*)shape->data)[i];
                } else {
                    /* Unsupported shape tensor dtype: preserve input shape. */
                    out->shape = data->shape;
                    out->data_size = data->data_size;
                    break;
                }

                if (dim_i == -1) {
                    unknown_dim_idx = (int)i;
                    out->shape.dims[i] = 1; /* temporary */
                } else if (dim_i == 0) {
                    out->shape.dims[i] = data->shape.dims[i];
                    total *= out->shape.dims[i];
                } else {
                    out->shape.dims[i] = (uint64_t)dim_i;
                    total *= out->shape.dims[i];
                }
            }

            /* Resolve unknown dimension if any */
            if (unknown_dim_idx != -1) {
                if (total > 0) {
                    out->shape.dims[unknown_dim_idx] = data->shape.total_elements / total;
                    total *= out->shape.dims[unknown_dim_idx];
                }
            }

            out->shape.total_elements = total;
            out->data_size = total * ONNX_GetDataTypeSize(out->dtype);

            if (g_runtime_verbose) {
                HAL_UART_PutString("[ONNX Reshape] ");
                HAL_UART_PutString(node->name);
                HAL_UART_PutString(" shape_dtype=");
                HAL_UART_PutDec((uint32_t)shape->dtype);
                HAL_UART_PutString(" shape_n=");
                HAL_UART_PutDec((uint32_t)shape->shape.total_elements);
                HAL_UART_PutString(" out_dims=");
                for (uint32_t d = 0; d < out->shape.ndim; d++) {
                    HAL_UART_PutDec((uint32_t)out->shape.dims[d]);
                    HAL_UART_PutString(d + 1 < out->shape.ndim ? "x" : "");
                }
                HAL_UART_PutString("\n");
            }
        }
    } else {
        out->shape = data->shape;
    }

    /* Reshape is a view op: keep zero-copy alias to input buffer. */
    out->dtype = data->dtype;
    out->data_size = data->data_size;
    out->data = data->data;

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

    if (data->dtype != ONNX_DTYPE_FLOAT32) {
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    /* Update shape for flatten */
    int axis = node->attributes.axis;
    if (axis == 0) { /* Default axis for Flatten is 1 in newer ONNX versions, but we'll respect attributes */
        /* If no attribute is set, axis defaults to 1. But our structs zero-initialize it.
         * Let's assume axis 1 if it's 0 and ndim > 1 for safety, or strictly follow ONNX default.
         * ONNX Default is axis=1. */
        axis = 1;
    }

    if (axis < 0) axis += data->shape.ndim;
    if (axis < 0 || (uint32_t)axis > data->shape.ndim) axis = data->shape.ndim;

    uint64_t dim1 = 1;
    for (int i = 0; i < axis; i++) {
        dim1 *= data->shape.dims[i];
    }

    uint64_t dim2 = 1;
    for (uint32_t i = axis; i < data->shape.ndim; i++) {
        dim2 *= data->shape.dims[i];
    }

    out->shape.ndim = 2;
    out->shape.dims[0] = dim1;
    out->shape.dims[1] = dim2;
    out->shape.total_elements = dim1 * dim2;

    /* Flatten is a view op: keep zero-copy alias to input buffer. */
    out->dtype = data->dtype;
    out->data_size = data->data_size;
    out->data = data->data;

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

    /* Get permutation array from attributes or default to reverse */
    uint32_t ndim = in->shape.ndim;

    if (ndim > ONNX_MAX_DIMS) {
        HAL_UART_PutString("[ONNX Transpose] ndim > MAX_DIMS\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    int64_t perm[ONNX_MAX_DIMS];

    if (node->attributes.perm_len == ndim) {
        for (uint32_t i = 0; i < ndim; i++) perm[i] = node->attributes.perm[i];
    } else {
        /* Default permutation is reverse order */
        for (uint32_t i = 0; i < ndim; i++) perm[i] = ndim - 1 - i;
    }

    /* Calculate output shape */
    out->shape.ndim = ndim;
    uint64_t total_elements = 1;
    for (uint32_t i = 0; i < ndim; i++) {
        if (perm[i] < ONNX_MAX_DIMS && perm[i] < ndim) {
            out->shape.dims[i] = in->shape.dims[perm[i]];
        } else {
            out->shape.dims[i] = 1; /* Fallback if invalid perm */
        }
        total_elements *= out->shape.dims[i];
    }
    out->shape.total_elements = total_elements;

    /* If allocating dynamic output, this ensures size is correct */
    out->data_size = total_elements * sizeof(float);

    /* Ensure the output tensor is allocated */
    if (out->data == NULL) {
        Status s = ONNX_Graph_AllocateTensor(ctx->graph, out);
        if (s != STATUS_OK) return s;
    }

    if (!in->data || !out->data) return STATUS_ERROR_OUT_OF_MEMORY;

    /* Debug log for Transpose */
    if (g_runtime_verbose) {
        HAL_UART_PutString("[ONNX Transpose] in_data=");
        HAL_UART_PutHex((uint64_t)in->data);
        HAL_UART_PutString(" out_data=");
        HAL_UART_PutHex((uint64_t)out->data);
        HAL_UART_PutString(" in_shape=");
        for (uint32_t d = 0; d < ndim; d++) {
            HAL_UART_PutDec(in->shape.dims[d]);
            HAL_UART_PutString(",");
        }
        HAL_UART_PutString(" out_shape=");
        for (uint32_t d = 0; d < ndim; d++) {
            HAL_UART_PutDec(out->shape.dims[d]);
            HAL_UART_PutString(",");
        }
        HAL_UART_PutString(" perm=");
        for (uint32_t d = 0; d < ndim; d++) {
            HAL_UART_PutDec(perm[d]);
            HAL_UART_PutString(",");
        }
        HAL_UART_PutString("\n");
    }

    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    /* Handle 1D or 0D trivial cases early */
    if (ndim <= 1) {
        if (in_data != out_data) {
            mem_copy(out_data, in_data, out->data_size);
        }
        return STATUS_OK;
    }

    /* Fast path for channel shuffle transpose used in ShuffleNet:
     * perm [0,2,1,3,4] on [N,G,C,H,W] -> [N,C,G,H,W].
     * Copy contiguous H*W blocks instead of per-element index math. */
    if (ndim == 5 &&
        perm[0] == 0 && perm[1] == 2 && perm[2] == 1 && perm[3] == 3 && perm[4] == 4 &&
        in_data != out_data) {
        uint64_t d0 = in->shape.dims[0];
        uint64_t d1 = in->shape.dims[1];
        uint64_t d2 = in->shape.dims[2];
        uint64_t d3 = in->shape.dims[3];
        uint64_t d4 = in->shape.dims[4];

        uint32_t zero_count = 0;
        uint32_t zero_idx = 0;
        uint64_t prod_nonzero = 1;
        uint64_t dims_local[5] = { d0, d1, d2, d3, d4 };

        for (uint32_t i = 0; i < 5; i++) {
            if (dims_local[i] == 0) {
                zero_count++;
                zero_idx = i;
            } else {
                prod_nonzero *= dims_local[i];
            }
        }

        /* Recover one placeholder zero dim from known total size. */
        if (zero_count == 1 && prod_nonzero > 0 && in->shape.total_elements > 0 &&
            (in->shape.total_elements % prod_nonzero) == 0) {
            dims_local[zero_idx] = in->shape.total_elements / prod_nonzero;
        }

        uint64_t N = dims_local[0];
        uint64_t G = dims_local[1];
        uint64_t C = dims_local[2];
        uint64_t H = dims_local[3];
        uint64_t W = dims_local[4];

        if (N == 0 || G == 0 || C == 0 || H == 0 || W == 0 ||
            N > 4096 || G > 4096 || C > 4096 || H > 4096 || W > 4096) {
            HAL_UART_PutString("[ONNX Transpose] invalid shuffle dims at node '");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString("': ");
            HAL_UART_PutDec((uint32_t)N); HAL_UART_PutString("x");
            HAL_UART_PutDec((uint32_t)G); HAL_UART_PutString("x");
            HAL_UART_PutDec((uint32_t)C); HAL_UART_PutString("x");
            HAL_UART_PutDec((uint32_t)H); HAL_UART_PutString("x");
            HAL_UART_PutDec((uint32_t)W);
            HAL_UART_PutString(" total=");
            HAL_UART_PutDec((uint32_t)in->shape.total_elements);
            HAL_UART_PutString(" in_ptr=");
            HAL_UART_PutHex((uint64_t)(uintptr_t)in);
            HAL_UART_PutString(" in_data=");
            HAL_UART_PutHex((uint64_t)(uintptr_t)in->data);
            HAL_UART_PutString(" in_dtype=");
            HAL_UART_PutDec((uint32_t)in->dtype);
            HAL_UART_PutString(" in_name='");
            HAL_UART_PutString(in->name);
            HAL_UART_PutString("'");
            HAL_UART_PutString("\n");
            return STATUS_ERROR_SHAPE_MISMATCH;
        }

        uint64_t hw = H * W;
        uint64_t in_total = N * G;
        in_total *= C;
        in_total *= hw;

        if (in_total != in->shape.total_elements || in_total != out->shape.total_elements) {
            HAL_UART_PutString("[ONNX Transpose] shuffle total mismatch at node '");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString("': in_total=");
            HAL_UART_PutDec((uint32_t)in_total);
            HAL_UART_PutString(" in_shape_total=");
            HAL_UART_PutDec((uint32_t)in->shape.total_elements);
            HAL_UART_PutString(" out_shape_total=");
            HAL_UART_PutDec((uint32_t)out->shape.total_elements);
            HAL_UART_PutString("\n");
            return STATUS_ERROR_SHAPE_MISMATCH;
        }

        uint64_t block_bytes = hw * sizeof(float);

        for (uint64_t n = 0; n < N; n++) {
            uint64_t n_in_off = (uint64_t)n * G * C * hw;
            uint64_t n_out_off = (uint64_t)n * C * G * hw;

            for (uint64_t c = 0; c < C; c++) {
                for (uint64_t g = 0; g < G; g++) {
                    uint64_t src = n_in_off + (((uint64_t)g * C + c) * hw);
                    uint64_t dst = n_out_off + (((uint64_t)c * G + g) * hw);
                    mem_copy(&out_data[dst], &in_data[src], block_bytes);
                }
            }
        }

        return STATUS_OK;
    }

    /* Pre-calculate strides for input and output */
    uint64_t in_strides[ONNX_MAX_DIMS];
    uint64_t out_strides[ONNX_MAX_DIMS];

    in_strides[ndim - 1] = 1;
    out_strides[ndim - 1] = 1;
    for (int32_t i = ndim - 2; i >= 0; i--) {
        in_strides[i] = in_strides[i + 1] * in->shape.dims[i + 1];
        out_strides[i] = out_strides[i + 1] * out->shape.dims[i + 1];
    }

    /* Iterate through all elements using a flattened index */
    for (uint64_t i = 0; i < total_elements; i++) {
        /* Convert flat output index `i` back to N-D coordinates */
        uint64_t out_coords[ONNX_MAX_DIMS];
        uint64_t temp = i;
        for (uint32_t d = 0; d < ndim; d++) {
            out_coords[d] = temp / out_strides[d];
            temp %= out_strides[d];
        }

        /* Map output coordinates to input coordinates using inverse perm */
        uint64_t in_coords[ONNX_MAX_DIMS];
        for (uint32_t d = 0; d < ndim; d++) {
            in_coords[d] = 0;
        }
        for (uint32_t d = 0; d < ndim; d++) {
            if (perm[d] < ONNX_MAX_DIMS && perm[d] < ndim) {
                in_coords[perm[d]] = out_coords[d];
            }
        }

        /* Compute flat input index */
        uint64_t in_idx = 0;
        for (uint32_t d = 0; d < ndim; d++) {
            in_idx += in_coords[d] * in_strides[d];
        }

        out_data[i] = in_data[in_idx];
    }

    return STATUS_OK;
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
        uint32_t n_off = n * C * spatial;

        for (uint32_t c = 0; c < C; c++) {
            uint32_t c_off = n_off + c * spatial;

            float mul = scale_data[c] * fast_rsqrt(var_data[c] + epsilon);
            float add = b_data[c] - (mean_data[c] * mul);

            for (uint32_t sp = 0; sp < spatial; sp++) {
                uint32_t idx = c_off + sp;
                y_data[idx] = x_data[idx] * mul + add;
            }
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_GEMM(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* A, B, C -> Y.  Y = alpha * op(A) * op(B) + beta * C
     * ONNX GEMM attributes:
     *   transA (field group – stored in keepdims by our minimal parser)
     *   transB (field group – stored in keepdims by our minimal parser)
     * AlexNet FC layers use transB=1 (weights stored transposed).
     * We detect transB via the B shape: if B is stored as [K,N] (transB=1)
     * then B->shape.dims[0]==K and B->shape.dims[1]==N.
     * We infer transB from shape heuristics:
     *   if M == A.dims[0] and B.dims[0] == A.dims[1] -> no transpose (standard)
     *   otherwise assume transB=1 (B stored as [N,K], access B[j*K+k])
     */
    if (node->num_inputs < 2 || node->num_outputs != 1) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    ONNX_Tensor* A = node->inputs[0];
    ONNX_Tensor* B = node->inputs[1];
    ONNX_Tensor* C = (node->num_inputs > 2) ? node->inputs[2] : NULL;
    ONNX_Tensor* Y = node->outputs[0];

    float alpha = (node->attributes.alpha != 0.0f) ? node->attributes.alpha : 1.0f;
    float beta  = (node->attributes.beta  != 0.0f) ? node->attributes.beta  : 1.0f;

    uint32_t M = (uint32_t)A->shape.dims[0];
    uint32_t K = (uint32_t)A->shape.dims[1];

    /* Determine if B is transposed.
     * Standard (transB=0): B is [K, N], so B.dims[0]==K.
     * Transposed (transB=1): B is [N, K], so B.dims[0]!=K (unless square). */
    bool transB = ((uint32_t)B->shape.dims[0] != K);
    uint32_t N_out;
    if (!transB) {
        N_out = (uint32_t)B->shape.dims[1];
    } else {
        N_out = (uint32_t)B->shape.dims[0];
        /* Verify inner dim matches */
        if (B->shape.ndim >= 2 && (uint32_t)B->shape.dims[1] != K) {
            HAL_UART_PutString("[ONNX] GEMM: shape mismatch\n");
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
    }

    float* a_data = (float*)A->data;
    float* b_data = (float*)B->data;
    float* c_data = C ? (float*)C->data : NULL;
    float* y_data = (float*)Y->data;

    /* Update output shape */
    Y->shape.ndim = 2;
    Y->shape.dims[0] = M;
    Y->shape.dims[1] = N_out;
    Y->shape.total_elements = (uint64_t)M * N_out;

    if (!transB) {
        for (uint32_t i = 0; i < M; i++) {
            const float* a_row = a_data + ((uint64_t)i * K);
            float* y_row = y_data + ((uint64_t)i * N_out);

            if (c_data) {
                for (uint32_t j = 0; j < N_out; j++) {
                    y_row[j] = beta * c_data[j];
                }
            } else {
                for (uint32_t j = 0; j < N_out; j++) {
                    y_row[j] = 0.0f;
                }
            }

            for (uint32_t k = 0; k < K; k++) {
                float a_scaled = alpha * a_row[k];
                const float* b_row = b_data + ((uint64_t)k * N_out);

                uint32_t j = 0;
                for (; j + 3 < N_out; j += 4) {
                    y_row[j + 0] += a_scaled * b_row[j + 0];
                    y_row[j + 1] += a_scaled * b_row[j + 1];
                    y_row[j + 2] += a_scaled * b_row[j + 2];
                    y_row[j + 3] += a_scaled * b_row[j + 3];
                }
                for (; j < N_out; j++) {
                    y_row[j] += a_scaled * b_row[j];
                }
            }
        }
    } else {
        for (uint32_t i = 0; i < M; i++) {
            const float* a_row = a_data + ((uint64_t)i * K);
            float* y_row = y_data + ((uint64_t)i * N_out);

            for (uint32_t j = 0; j < N_out; j++) {
                const float* b_row = b_data + ((uint64_t)j * K); /* B[j,k] */
                float sum = 0.0f;

                uint32_t k = 0;
                for (; k + 3 < K; k += 4) {
                    sum += a_row[k + 0] * b_row[k + 0];
                    sum += a_row[k + 1] * b_row[k + 1];
                    sum += a_row[k + 2] * b_row[k + 2];
                    sum += a_row[k + 3] * b_row[k + 3];
                }
                for (; k < K; k++) {
                    sum += a_row[k] * b_row[k];
                }

                y_row[j] = alpha * sum + (c_data ? (beta * c_data[j]) : 0.0f);
            }
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
    if (axis < 0) axis += (int)out->shape.ndim;

    float* out_data = (float*)out->data;

    /* Fast path: axis=0 is a trivial memcpy append */
    if (axis == 0) {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < node->num_inputs; i++) {
            ONNX_Tensor* in = node->inputs[i];
            mem_copy(out_data + offset, in->data, in->data_size);
            offset += in->shape.total_elements;
        }
        return STATUS_OK;
    }

    /* General N-dimensional Concat along arbitrary axis.
     *
     * In row-major layout, concatenating along axis K means we copy
     * "outer blocks" (dimensions 0..K-1) one at a time, and within each
     * block we append every input's "inner slice" (dimensions K..ndim-1).
     *
     *   outer_size = product of dims[0..axis-1]   (or 1 if axis==0)
     *   inner_size = product of dims[axis..ndim-1]
     *
     * For each outer block o:
     *   for each input i:
     *     copy input[i][o * inner_i .. (o+1)*inner_i]
     *     to  output [o * inner_out + offset_i .. + inner_i]
     */
    uint32_t outer_size = 1;
    for (int d = 0; d < axis; d++)
        outer_size *= out->shape.dims[d];

    /* Sum of inner sizes across inputs = output inner size */
    uint32_t out_inner = 1;
    for (uint32_t d = (uint32_t)axis; d < out->shape.ndim; d++)
        out_inner *= out->shape.dims[d];

    /* Pre-compute each input's inner size */
    uint32_t in_inner[16]; /* max 16 inputs */
    if (node->num_inputs > 16) return STATUS_ERROR_NOT_SUPPORTED;
    for (uint32_t i = 0; i < node->num_inputs; i++) {
        in_inner[i] = 1;
        ONNX_Tensor* in = node->inputs[i];
        for (uint32_t d = (uint32_t)axis; d < in->shape.ndim; d++)
            in_inner[i] *= in->shape.dims[d];
    }

    for (uint32_t o = 0; o < outer_size; o++) {
        uint32_t out_off = o * out_inner;
        for (uint32_t i = 0; i < node->num_inputs; i++) {
            ONNX_Tensor* in = node->inputs[i];
            float* src = ((float*)in->data) + o * in_inner[i];
            mem_copy(out_data + out_off, src, in_inner[i] * sizeof(float));
            out_off += in_inner[i];
        }
    }

    return STATUS_OK;
}

Status ONNX_Execute_Split(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs < 1 || node->num_outputs < 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    int axis = (int)node->attributes.axis;
    if (axis < 0) axis += (int)in->shape.ndim;
    if (axis < 0 || (uint32_t)axis >= in->shape.ndim) return STATUS_ERROR_INVALID_ARGUMENT;

    uint64_t axis_dim = in->shape.dims[axis];
    uint64_t split_sizes[ONNX_MAX_OUTPUTS] = {0};
    uint32_t split_count = node->num_outputs;

    if (node->attributes.kernel_shape_len > 0 && node->attributes.kernel_shape_len == node->num_outputs) {
        split_count = node->attributes.kernel_shape_len;
        for (uint32_t i = 0; i < split_count; i++) split_sizes[i] = (uint64_t)node->attributes.kernel_shape[i];
    } else if (node->num_inputs >= 2 && node->inputs[1] && node->inputs[1]->data) {
        ONNX_Tensor* split_t = node->inputs[1];
        if (split_t->shape.total_elements != node->num_outputs) {
            HAL_UART_PutString("[ONNX Split] split tensor len mismatch at node '");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString("': split_len=");
            HAL_UART_PutDec((uint32_t)split_t->shape.total_elements);
            HAL_UART_PutString(" outputs=");
            HAL_UART_PutDec(node->num_outputs);
            HAL_UART_PutString("\n");
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
        split_count = (uint32_t)split_t->shape.total_elements;
        for (uint32_t i = 0; i < split_count; i++) {
            if (split_t->dtype == ONNX_DTYPE_INT64) {
                split_sizes[i] = (uint64_t)((int64_t*)split_t->data)[i];
            } else if (split_t->dtype == ONNX_DTYPE_INT32) {
                split_sizes[i] = (uint64_t)((int32_t*)split_t->data)[i];
            } else {
                return STATUS_ERROR_NOT_SUPPORTED;
            }
        }
    } else {
        if ((axis_dim % node->num_outputs) == 0) {
            uint64_t each = axis_dim / node->num_outputs;
            split_count = node->num_outputs;
            for (uint32_t i = 0; i < node->num_outputs; i++) split_sizes[i] = each;
        } else {
            /* Some models (e.g. channel-shuffle variants) require uneven split.
             * If split sizes are not provided explicitly, infer from output shapes. */
            uint64_t inferred_sum = 0;
            bool inferred_ok = true;
            split_count = node->num_outputs;

            for (uint32_t i = 0; i < split_count; i++) {
                ONNX_Tensor* out = node->outputs[i];
                if (!out || out->shape.ndim <= (uint32_t)axis || out->shape.dims[axis] == 0) {
                    inferred_ok = false;
                    break;
                }
                split_sizes[i] = out->shape.dims[axis];
                inferred_sum += split_sizes[i];
            }

            if (!inferred_ok || inferred_sum != axis_dim) {
                HAL_UART_PutString("[ONNX Split] uneven default split at node '");
                HAL_UART_PutString(node->name);
                HAL_UART_PutString("': axis_dim=");
                HAL_UART_PutDec((uint32_t)axis_dim);
                HAL_UART_PutString(" axis=");
                HAL_UART_PutDec((uint32_t)axis);
                HAL_UART_PutString(" in_shape=");
                for (uint32_t d = 0; d < in->shape.ndim; d++) {
                    HAL_UART_PutDec((uint32_t)in->shape.dims[d]);
                    HAL_UART_PutString(d + 1 < in->shape.ndim ? "x" : "");
                }
                HAL_UART_PutString(" outputs=");
                HAL_UART_PutDec(node->num_outputs);
                HAL_UART_PutString(" inferred_sum=");
                HAL_UART_PutDec((uint32_t)inferred_sum);
                HAL_UART_PutString(" klen=");
                HAL_UART_PutDec(node->attributes.kernel_shape_len);
                HAL_UART_PutString(" inputs=");
                HAL_UART_PutDec(node->num_inputs);
                if (node->num_inputs >= 2 && node->inputs[1]) {
                    HAL_UART_PutString(" in1_n=");
                    HAL_UART_PutDec((uint32_t)node->inputs[1]->shape.total_elements);
                    HAL_UART_PutString(" in1_dtype=");
                    HAL_UART_PutDec((uint32_t)node->inputs[1]->dtype);
                    HAL_UART_PutString(" in1_data=");
                    HAL_UART_PutHex((uint64_t)(uintptr_t)node->inputs[1]->data);
                }
                HAL_UART_PutString("\n");
                return STATUS_ERROR_SHAPE_MISMATCH;
            }
        }
    }

    uint64_t split_sum = 0;
    for (uint32_t i = 0; i < split_count; i++) split_sum += split_sizes[i];
    if (split_sum != axis_dim) {
        HAL_UART_PutString("[ONNX Split] split sum mismatch at node '");
        HAL_UART_PutString(node->name);
        HAL_UART_PutString("': split_sum=");
        HAL_UART_PutDec((uint32_t)split_sum);
        HAL_UART_PutString(" axis_dim=");
        HAL_UART_PutDec((uint32_t)axis_dim);
        HAL_UART_PutString(" split_count=");
        HAL_UART_PutDec(split_count);
        HAL_UART_PutString("\n");
        return STATUS_ERROR_SHAPE_MISMATCH;
    }

    uint64_t outer = 1;
    for (int d = 0; d < axis; d++) outer *= in->shape.dims[d];
    uint64_t inner = 1;
    for (uint32_t d = (uint32_t)axis + 1; d < in->shape.ndim; d++) inner *= in->shape.dims[d];

    float* in_data = (float*)in->data;
    if (!in_data) {
        HAL_UART_PutString("[ONNX Split] null input data at node '");
        HAL_UART_PutString(node->name);
        HAL_UART_PutString("'\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }

    uint64_t in_total = in->shape.total_elements;
    uint64_t axis_offset = 0;
    for (uint32_t o = 0; o < split_count; o++) {
        ONNX_Tensor* out = node->outputs[o];
        out->shape = in->shape;
        out->shape.dims[axis] = split_sizes[o];
        out->shape.total_elements = 1;
        for (uint32_t d = 0; d < out->shape.ndim; d++) out->shape.total_elements *= out->shape.dims[d];
        out->data_size = out->shape.total_elements * sizeof(float);
        if (!out->data) {
            Status s = ONNX_Graph_AllocateTensor(ctx->graph, out);
            if (s != STATUS_OK) return s;
        }

        float* out_data = (float*)out->data;
        if (!out_data) {
            HAL_UART_PutString("[ONNX Split] null output data at node '");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString("'\n");
            return STATUS_ERROR_OUT_OF_MEMORY;
        }

        uint64_t out_total = out->shape.total_elements;
        uint64_t chunk = split_sizes[o] * inner;
        for (uint64_t ob = 0; ob < outer; ob++) {
            uint64_t src_off = (ob * axis_dim + axis_offset) * inner;
            uint64_t dst_off = ob * chunk;

            if (src_off + chunk > in_total || dst_off + chunk > out_total) {
                HAL_UART_PutString("[ONNX Split] copy bounds mismatch at node '");
                HAL_UART_PutString(node->name);
                HAL_UART_PutString("': src_off=");
                HAL_UART_PutDec((uint32_t)src_off);
                HAL_UART_PutString(" chunk=");
                HAL_UART_PutDec((uint32_t)chunk);
                HAL_UART_PutString(" in_total=");
                HAL_UART_PutDec((uint32_t)in_total);
                HAL_UART_PutString(" dst_off=");
                HAL_UART_PutDec((uint32_t)dst_off);
                HAL_UART_PutString(" out_total=");
                HAL_UART_PutDec((uint32_t)out_total);
                HAL_UART_PutString("\n");
                return STATUS_ERROR_SHAPE_MISMATCH;
            }

            mem_copy(out_data + dst_off, in_data + src_off, chunk * sizeof(float));
        }
        axis_offset += split_sizes[o];
    }

    return STATUS_OK;
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
    out->dtype = data->dtype;
    out->data_size = data->data_size;
    out->data = data->data;

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
    out->dtype = data->dtype;
    out->data_size = data->data_size;
    out->data = data->data;

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
        out->shape = in->shape;
        out->data_size = in->data_size;
        out->data = in->data;
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] Cast currently only supports float->float\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

static Status ONNX_Execute_Abs(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_abs(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Neg(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = -in_data[i];
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Exp(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_exp(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Log(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_log(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Sqrt(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_sqrt(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Ceil(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_ceil(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Floor(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_floor(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Sin(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_sin(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Cos(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_cos(in_data[i]);
    }
    return STATUS_OK;
}

static Status ONNX_Execute_Constant(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* out = node->outputs[0];
    if (!out) return STATUS_ERROR_INVALID_GRAPH;

    if ((ONNX_DataType)node->attributes.group == ONNX_DTYPE_INT64 &&
        node->attributes.kernel_shape_len > 0) {
        uint32_t n = node->attributes.kernel_shape_len;
        out->dtype = ONNX_DTYPE_INT64;
        out->shape.ndim = 1;
        out->shape.dims[0] = n;
        out->shape.total_elements = n;
        out->data_size = (uint64_t)n * sizeof(int64_t);

        /* Constant outputs may be pre-allocated with placeholder sizes.
         * Allocate a fresh correctly-sized buffer and repoint the tensor. */
        out->data = KMEM_ArenaAlloc(ctx->graph->tensor_arena, out->data_size, KMEM_TENSOR_ALIGN);
        if (!out->data) return STATUS_ERROR_OUT_OF_MEMORY;
        ctx->graph->tensor_memory_used = KMEM_ArenaGetUsed(ctx->graph->tensor_arena);

        int64_t* out_i64 = (int64_t*)out->data;
        for (uint32_t i = 0; i < n; i++) out_i64[i] = node->attributes.kernel_shape[i];

        if (g_runtime_verbose) {
            HAL_UART_PutString("[ONNX Constant] ");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString(" len=");
            HAL_UART_PutDec(n);
            HAL_UART_PutString(" vals=");
            uint32_t lim = (n < 8U) ? n : 8U;
            for (uint32_t i = 0; i < lim; i++) {
                HAL_UART_PutDec((uint32_t)out_i64[i]);
                HAL_UART_PutString(i + 1 < lim ? "," : "");
            }
            HAL_UART_PutString("\n");
        }

        return STATUS_OK;
    }

    return STATUS_ERROR_NOT_SUPPORTED;
}

static Status ONNX_Execute_Identity(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;

    /* A Constant node mapped to Identity might have 0 inputs and 1 output.
     * Real Identity has 1 input. */
    if (node->num_inputs == 0 && node->num_outputs == 1) {
        return STATUS_OK; /* Can't copy anything, value should have been in attribute */
    }

    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    out->dtype = in->dtype;
    out->shape = in->shape;
    out->data_size = in->data_size;
    out->data = in->data;
    return STATUS_OK;
}

/**
 * @brief Local Response Normalization (LRN)
 * ONNX spec: y[n,c,h,w] = x[n,c,h,w] / (bias + alpha/size * sum_{j=max(0,c-floor(size/2))}^{min(C-1,c+floor(size/2))} x[n,j,h,w]^2)^beta
 * AlexNet uses default parameters: size=5, alpha=0.0001, beta=0.75, bias=1.0
 */
static Status ONNX_Execute_LRN(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in  = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;
    if (in->shape.ndim != 4) return STATUS_ERROR_NOT_SUPPORTED;

    /* LRN attributes — use ONNX defaults if not parsed */
    float  alpha = (node->attributes.alpha != 0.0f) ? node->attributes.alpha : 0.0001f;
    float  beta  = (node->attributes.beta  != 0.0f) ? node->attributes.beta  : 0.75f;
    /* size and bias are stored in axis/keepdims temporarily */
    int64_t lrn_size = (node->attributes.axis > 0) ? node->attributes.axis : 5;
    /* bias defaults to 1.0 */
    float  bias = 1.0f;

    uint32_t N  = (uint32_t)in->shape.dims[0];
    uint32_t C  = (uint32_t)in->shape.dims[1];
    uint32_t H  = (uint32_t)in->shape.dims[2];
    uint32_t W  = (uint32_t)in->shape.dims[3];

    float* in_data  = (float*)in->data;
    float* out_data = (float*)out->data;

    int32_t half = (int32_t)(lrn_size / 2);

    for (uint32_t n = 0; n < N; n++) {
        for (uint32_t c = 0; c < C; c++) {
            for (uint32_t h = 0; h < H; h++) {
                for (uint32_t w = 0; w < W; w++) {
                    /* Compute sum of squares over the local channel window */
                    float sq_sum = 0.0f;
                    int32_t c_start = (int32_t)c - half;
                    int32_t c_end   = (int32_t)c + half;
                    if (c_start < 0) c_start = 0;
                    if (c_end >= (int32_t)C) c_end = (int32_t)C - 1;

                    for (int32_t j = c_start; j <= c_end; j++) {
                        float v = in_data[n * C * H * W + (uint32_t)j * H * W + h * W + w];
                        sq_sum += v * v;
                    }

                    /* Normalise */
                    float denom = bias + alpha / (float)lrn_size * sq_sum;
                    /* denom^beta approximated as exp(beta * log(denom)) */
                    /* Use fast_sqrt for beta=0.75 special-case if possible */
                    float denom_pow;
                    if (beta == 0.75f) {
                        /* denom^0.75 = sqrt(denom) * denom^0.25 = sqrt(sqrt(denom)*denom) */
                        float sd = fast_sqrt(denom);
                        denom_pow = fast_sqrt(sd * denom);
                    } else {
                        /* General: e^(beta * ln(denom)) */
                        float ln_d = fast_log(denom);
                        denom_pow = fast_exp(beta * ln_d);
                    }

                    uint32_t idx = n * C * H * W + c * H * W + h * W + w;
                    out_data[idx] = (denom_pow > 0.0f) ? (in_data[idx] / denom_pow) : 0.0f;
                }
            }
        }
    }

    /* Output shape is identical to input */
    out->shape = in->shape;
    return STATUS_OK;
}

static Status ONNX_Execute_Clip(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs < 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    float min_val = -1e38f; // approx -inf
    float max_val = 1e38f;  // approx inf

    if (node->num_inputs > 1 && node->inputs[1] && node->inputs[1]->data) {
        min_val = ((float*)node->inputs[1]->data)[0];
    }
    if (node->num_inputs > 2 && node->inputs[2] && node->inputs[2]->data) {
        max_val = ((float*)node->inputs[2]->data)[0];
    }

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    for (uint64_t i = 0; i < n; i++) {
        float v = in_data[i];
        if (v < min_val) v = min_val;
        if (v > max_val) v = max_val;
        out_data[i] = v;
    }
    return STATUS_OK;
}

static Status ONNX_Execute_ReduceSum(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    int axis = node->attributes.axis;
    if (axis == -1 || in->shape.ndim == 1) {
        float sum = 0.0f;
        uint64_t n = in->shape.total_elements;
        float* in_data = (float*)in->data;
        for (uint64_t i = 0; i < n; i++) sum += in_data[i];
        ((float*)out->data)[0] = sum;
        return STATUS_OK;
    }

    /* Naive support only for reducing full tensor so far, extend as needed */
    HAL_UART_PutString("[ONNX] ReduceSum axis != -1 not fully implemented\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

static Status ONNX_Execute_ReduceMean(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    int axis = node->attributes.axis;
    if (axis == -1 || in->shape.ndim == 1) {
        float sum = 0.0f;
        uint64_t n = in->shape.total_elements;
        float* in_data = (float*)in->data;
        for (uint64_t i = 0; i < n; i++) sum += in_data[i];
        ((float*)out->data)[0] = n > 0 ? (sum / n) : 0.0f;
        return STATUS_OK;
    }

    /* Common CNN case: ReduceMean over H,W axes of NCHW tensor. */
    if (in->shape.ndim == 4 &&
        ((node->attributes.perm_len == 2 &&
          ((node->attributes.perm[0] == 2 && node->attributes.perm[1] == 3) ||
           (node->attributes.perm[0] == 3 && node->attributes.perm[1] == 2))) ||
         (node->attributes.perm_len == 0 && axis == 0))) {
        uint32_t N = (uint32_t)in->shape.dims[0];
        uint32_t C = (uint32_t)in->shape.dims[1];
        uint32_t H = (uint32_t)in->shape.dims[2];
        uint32_t W = (uint32_t)in->shape.dims[3];
        uint64_t hw = (uint64_t)H * W;

        if (node->attributes.keepdims != 0) {
            out->shape.ndim = 4;
            out->shape.dims[0] = N;
            out->shape.dims[1] = C;
            out->shape.dims[2] = 1;
            out->shape.dims[3] = 1;
        } else {
            out->shape.ndim = 2;
            out->shape.dims[0] = N;
            out->shape.dims[1] = C;
        }
        out->shape.total_elements = (uint64_t)N * C;

        float* in_data = (float*)in->data;
        float* out_data = (float*)out->data;
        for (uint32_t n = 0; n < N; n++) {
            for (uint32_t c = 0; c < C; c++) {
                float sum = 0.0f;
                uint64_t base = ((uint64_t)n * C + c) * hw;
                for (uint64_t i = 0; i < hw; i++) sum += in_data[base + i];
                out_data[(uint64_t)n * C + c] = (hw > 0) ? (sum / (float)hw) : 0.0f;
            }
        }
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] ReduceMean axis != -1 not fully implemented\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

static Status ONNX_Execute_ReduceMax(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    int axis = node->attributes.axis;
    if (axis == -1 || in->shape.ndim == 1) {
        uint64_t n = in->shape.total_elements;
        if (n == 0) return STATUS_OK;
        float* in_data = (float*)in->data;
        float max_val = in_data[0];
        for (uint64_t i = 1; i < n; i++) {
            if (in_data[i] > max_val) max_val = in_data[i];
        }
        ((float*)out->data)[0] = max_val;
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] ReduceMax axis != -1 not fully implemented\n");
    return STATUS_ERROR_NOT_SUPPORTED;
}

static Status ONNX_Execute_ReduceMin(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    if (!node || !ctx) return STATUS_ERROR_INVALID_ARGUMENT;
    if (node->num_inputs != 1 || node->num_outputs != 1) return STATUS_ERROR_INVALID_GRAPH;

    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    if (in->dtype != ONNX_DTYPE_FLOAT32) return STATUS_ERROR_NOT_SUPPORTED;

    int axis = node->attributes.axis;
    if (axis == -1 || in->shape.ndim == 1) {
        uint64_t n = in->shape.total_elements;
        if (n == 0) return STATUS_OK;
        float* in_data = (float*)in->data;
        float min_val = in_data[0];
        for (uint64_t i = 1; i < n; i++) {
            if (in_data[i] < min_val) min_val = in_data[i];
        }
        ((float*)out->data)[0] = min_val;
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] ReduceMin axis != -1 not fully implemented\n");
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

static inline float conv_apply_relu(float v, bool fuse_relu)
{
    return (fuse_relu && v < 0.0f) ? 0.0f : v;
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

    /* 2D convolution (NCHW) with grouped-conv support:
     * X: [N, C_in, H_in, W_in]
     * W: [C_out, C_in/group, K_h, K_w]
     * Y: [N, C_out, H_out, W_out]
     */
    if (x->shape.ndim != 4 || w->shape.ndim != 4) {
        HAL_UART_PutString("[ONNX] Conv only supports 4D tensors currently\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    uint32_t batch_n = (uint32_t)x->shape.dims[0];
    uint32_t c_in = x->shape.dims[1];
    uint32_t h_in = x->shape.dims[2];
    uint32_t w_in = x->shape.dims[3];

    uint32_t c_out = w->shape.dims[0];
    uint32_t k_h = (node->attributes.kernel_shape_len >= 1) ? (uint32_t)node->attributes.kernel_shape[0] : (uint32_t)w->shape.dims[2];
    uint32_t k_w = (node->attributes.kernel_shape_len >= 2) ? (uint32_t)node->attributes.kernel_shape[1] : (uint32_t)w->shape.dims[3];

    uint32_t stride_h = (node->attributes.strides_len >= 1) ? (uint32_t)node->attributes.strides[0] : 1;
    uint32_t stride_w = (node->attributes.strides_len >= 2) ? (uint32_t)node->attributes.strides[1] : stride_h;

    uint32_t pad_top = (node->attributes.pads_len >= 1) ? (uint32_t)node->attributes.pads[0] : 0;
    uint32_t pad_left = (node->attributes.pads_len >= 2) ? (uint32_t)node->attributes.pads[1] : 0;
    uint32_t pad_bottom = (node->attributes.pads_len >= 3) ? (uint32_t)node->attributes.pads[2] : pad_top;
    uint32_t pad_right = (node->attributes.pads_len >= 4) ? (uint32_t)node->attributes.pads[3] : pad_left;

    uint32_t dilation_h = (node->attributes.dilations_len >= 1) ? (uint32_t)node->attributes.dilations[0] : 1;
    uint32_t dilation_w = (node->attributes.dilations_len >= 2) ? (uint32_t)node->attributes.dilations[1] : dilation_h;

    uint32_t group = (node->attributes.group > 0) ? (uint32_t)node->attributes.group : 1;
    if (group == 0 || c_in % group != 0 || c_out % group != 0) {
        return STATUS_ERROR_SHAPE_MISMATCH;
    }

    uint32_t c_in_per_group = c_in / group;
    uint32_t c_out_per_group = c_out / group;
    if ((uint32_t)w->shape.dims[1] != c_in_per_group) {
        return STATUS_ERROR_SHAPE_MISMATCH;
    }

    uint32_t effective_kh = (k_h - 1) * dilation_h + 1;
    uint32_t effective_kw = (k_w - 1) * dilation_w + 1;

    uint32_t h_out = (h_in + pad_top + pad_bottom - effective_kh) / stride_h + 1;
    uint32_t w_out = (w_in + pad_left + pad_right - effective_kw) / stride_w + 1;

    y->shape.ndim = 4;
    y->shape.dims[0] = x->shape.dims[0];
    y->shape.dims[1] = c_out;
    y->shape.dims[2] = h_out;
    y->shape.dims[3] = w_out;
    y->shape.total_elements = y->shape.dims[0] * y->shape.dims[1] * h_out * w_out;

    float* x_data = (float*)x->data;
    float* w_data = (float*)w->data;
    float* b_data = b ? (float*)b->data : NULL;
    float* y_data = (float*)y->data;

    uint64_t x_batch_stride = (uint64_t)c_in * h_in * w_in;
    uint64_t y_batch_stride = (uint64_t)c_out * h_out * w_out;
    uint64_t x_c_stride = (uint64_t)h_in * w_in;
    uint64_t y_c_stride = (uint64_t)h_out * w_out;
    uint64_t w_oc_stride = (uint64_t)c_in_per_group * k_h * k_w;
    bool fuse_relu = node->attributes.fuse_relu;

    /* Fast path: depthwise conv (c_in_per_group=1), common in ShuffleNet. */
    if (c_in_per_group == 1) {
        uint64_t w_dw_stride = (uint64_t)k_h * k_w;
        bool is_k3_d1 = (k_h == 3 && k_w == 3 && dilation_h == 1 && dilation_w == 1);

        for (uint32_t nb = 0; nb < batch_n; nb++) {
            float* y_nb = y_data + nb * y_batch_stride;
            const float* x_nb = x_data + nb * x_batch_stride;

            for (uint32_t g = 0; g < group; g++) {
                const float* x_ic = x_nb + ((uint64_t)g * x_c_stride);
                uint32_t oc_base = g * c_out_per_group;

                for (uint32_t ocg = 0; ocg < c_out_per_group; ocg++) {
                    uint32_t oc = oc_base + ocg;
                    float* y_oc = y_nb + ((uint64_t)oc * y_c_stride);
                    const float* w_oc = w_data + ((uint64_t)oc * w_dw_stride);
                    float w00 = 0.0f, w01 = 0.0f, w02 = 0.0f;
                    float w10 = 0.0f, w11 = 0.0f, w12 = 0.0f;
                    float w20 = 0.0f, w21 = 0.0f, w22 = 0.0f;
                    if (is_k3_d1) {
                        w00 = w_oc[0]; w01 = w_oc[1]; w02 = w_oc[2];
                        w10 = w_oc[3]; w11 = w_oc[4]; w12 = w_oc[5];
                        w20 = w_oc[6]; w21 = w_oc[7]; w22 = w_oc[8];
                    }

                    for (uint32_t oh = 0; oh < h_out; oh++) {
                        int32_t ih_base = (int32_t)(oh * stride_h) - (int32_t)pad_top;
                        for (uint32_t ow = 0; ow < w_out; ow++) {
                            int32_t iw_base = (int32_t)(ow * stride_w) - (int32_t)pad_left;
                            float sum = b_data ? b_data[oc] : 0.0f;

                            if (is_k3_d1 &&
                                ih_base >= 0 && iw_base >= 0 &&
                                (uint32_t)(ih_base + 2) < h_in &&
                                (uint32_t)(iw_base + 2) < w_in) {
                                uint64_t row0 = (uint64_t)ih_base * w_in + (uint32_t)iw_base;
                                uint64_t row1 = row0 + w_in;
                                uint64_t row2 = row1 + w_in;

                                sum += x_ic[row0 + 0] * w00;
                                sum += x_ic[row0 + 1] * w01;
                                sum += x_ic[row0 + 2] * w02;
                                sum += x_ic[row1 + 0] * w10;
                                sum += x_ic[row1 + 1] * w11;
                                sum += x_ic[row1 + 2] * w12;
                                sum += x_ic[row2 + 0] * w20;
                                sum += x_ic[row2 + 1] * w21;
                                sum += x_ic[row2 + 2] * w22;
                            } else {
                                for (uint32_t kh = 0; kh < k_h; kh++) {
                                    int32_t ih = ih_base + (int32_t)(kh * dilation_h);
                                    if (ih < 0 || ih >= (int32_t)h_in) continue;

                                    uint64_t x_row = (uint64_t)ih * w_in;
                                    uint64_t w_row = (uint64_t)kh * k_w;

                                    for (uint32_t kw = 0; kw < k_w; kw++) {
                                        int32_t iw = iw_base + (int32_t)(kw * dilation_w);
                                        if (iw >= 0 && iw < (int32_t)w_in) {
                                            sum += x_ic[x_row + (uint32_t)iw] *
                                                   w_oc[w_row + kw];
                                        }
                                    }
                                }
                            }

                            y_oc[(uint64_t)oh * w_out + ow] = conv_apply_relu(sum, fuse_relu);
                        }
                    }
                }
            }
        }
        return STATUS_OK;
    }

    /* Fast path: 1x1 grouped conv without padding/dilation */
    if (k_h == 1 && k_w == 1 &&
        dilation_h == 1 && dilation_w == 1 &&
        pad_top == 0 && pad_left == 0 && pad_bottom == 0 && pad_right == 0) {
        for (uint32_t nb = 0; nb < batch_n; nb++) {
            float* y_nb = y_data + nb * y_batch_stride;
            const float* x_nb = x_data + nb * x_batch_stride;

            for (uint32_t g = 0; g < group; g++) {
                uint32_t ic_base = g * c_in_per_group;
                uint32_t oc_base = g * c_out_per_group;

                for (uint32_t oh = 0; oh < h_out; oh++) {
                    uint32_t ih = oh * stride_h;
                    uint64_t x_row = (uint64_t)ih * w_in;

                    for (uint32_t ow = 0; ow < w_out; ow++) {
                        uint32_t iw = ow * stride_w;
                        uint64_t x_idx = x_row + iw;
                        uint64_t out_off = (uint64_t)oh * w_out + ow;

                        uint32_t ocg = 0;
                        for (; ocg + 3 < c_out_per_group; ocg += 4) {
                            uint32_t oc0 = oc_base + ocg + 0;
                            uint32_t oc1 = oc_base + ocg + 1;
                            uint32_t oc2 = oc_base + ocg + 2;
                            uint32_t oc3 = oc_base + ocg + 3;

                            const float* w0 = w_data + ((uint64_t)oc0 * w_oc_stride);
                            const float* w1 = w_data + ((uint64_t)oc1 * w_oc_stride);
                            const float* w2 = w_data + ((uint64_t)oc2 * w_oc_stride);
                            const float* w3 = w_data + ((uint64_t)oc3 * w_oc_stride);

                            float sum0 = b_data ? b_data[oc0] : 0.0f;
                            float sum1 = b_data ? b_data[oc1] : 0.0f;
                            float sum2 = b_data ? b_data[oc2] : 0.0f;
                            float sum3 = b_data ? b_data[oc3] : 0.0f;

                            uint32_t icg = 0;
                            for (; icg + 3 < c_in_per_group; icg += 4) {
                                const float* x0 = x_nb + ((uint64_t)(ic_base + icg + 0) * x_c_stride);
                                const float* x1 = x_nb + ((uint64_t)(ic_base + icg + 1) * x_c_stride);
                                const float* x2 = x_nb + ((uint64_t)(ic_base + icg + 2) * x_c_stride);
                                const float* x3 = x_nb + ((uint64_t)(ic_base + icg + 3) * x_c_stride);

                                float xv0 = x0[x_idx];
                                float xv1 = x1[x_idx];
                                float xv2 = x2[x_idx];
                                float xv3 = x3[x_idx];

                                sum0 += xv0 * w0[icg + 0] + xv1 * w0[icg + 1] + xv2 * w0[icg + 2] + xv3 * w0[icg + 3];
                                sum1 += xv0 * w1[icg + 0] + xv1 * w1[icg + 1] + xv2 * w1[icg + 2] + xv3 * w1[icg + 3];
                                sum2 += xv0 * w2[icg + 0] + xv1 * w2[icg + 1] + xv2 * w2[icg + 2] + xv3 * w2[icg + 3];
                                sum3 += xv0 * w3[icg + 0] + xv1 * w3[icg + 1] + xv2 * w3[icg + 2] + xv3 * w3[icg + 3];
                            }

                            for (; icg < c_in_per_group; icg++) {
                                const float* x_ic = x_nb + ((uint64_t)(ic_base + icg) * x_c_stride);
                                float xv = x_ic[x_idx];
                                sum0 += xv * w0[icg];
                                sum1 += xv * w1[icg];
                                sum2 += xv * w2[icg];
                                sum3 += xv * w3[icg];
                            }

                            y_nb[(uint64_t)oc0 * y_c_stride + out_off] = conv_apply_relu(sum0, fuse_relu);
                            y_nb[(uint64_t)oc1 * y_c_stride + out_off] = conv_apply_relu(sum1, fuse_relu);
                            y_nb[(uint64_t)oc2 * y_c_stride + out_off] = conv_apply_relu(sum2, fuse_relu);
                            y_nb[(uint64_t)oc3 * y_c_stride + out_off] = conv_apply_relu(sum3, fuse_relu);
                        }

                        for (; ocg < c_out_per_group; ocg++) {
                            uint32_t oc = oc_base + ocg;
                            const float* w_oc = w_data + ((uint64_t)oc * w_oc_stride);
                            float sum = b_data ? b_data[oc] : 0.0f;

                            uint32_t icg = 0;
                            for (; icg + 3 < c_in_per_group; icg += 4) {
                                const float* x0 = x_nb + ((uint64_t)(ic_base + icg + 0) * x_c_stride);
                                const float* x1 = x_nb + ((uint64_t)(ic_base + icg + 1) * x_c_stride);
                                const float* x2 = x_nb + ((uint64_t)(ic_base + icg + 2) * x_c_stride);
                                const float* x3 = x_nb + ((uint64_t)(ic_base + icg + 3) * x_c_stride);
                                sum += x0[x_idx] * w_oc[icg + 0];
                                sum += x1[x_idx] * w_oc[icg + 1];
                                sum += x2[x_idx] * w_oc[icg + 2];
                                sum += x3[x_idx] * w_oc[icg + 3];
                            }
                            for (; icg < c_in_per_group; icg++) {
                                const float* x_ic = x_nb + ((uint64_t)(ic_base + icg) * x_c_stride);
                                sum += x_ic[x_idx] * w_oc[icg];
                            }

                            y_nb[(uint64_t)oc * y_c_stride + out_off] = conv_apply_relu(sum, fuse_relu);
                        }
                    }
                }
            }
        }
        return STATUS_OK;
    }

    /* Fast path: regular 3x3 conv (group=1) with channel blocking over outputs. */
    if (group == 1 &&
        k_h == 3 && k_w == 3 &&
        stride_h == 1 && stride_w == 1 &&
        dilation_h == 1 && dilation_w == 1) {
        for (uint32_t nb = 0; nb < batch_n; nb++) {
            float* y_nb = y_data + nb * y_batch_stride;
            const float* x_nb = x_data + nb * x_batch_stride;

            for (uint32_t oh = 0; oh < h_out; oh++) {
                int32_t ih_base = (int32_t)oh - (int32_t)pad_top;
                for (uint32_t ow = 0; ow < w_out; ow++) {
                    int32_t iw_base = (int32_t)ow - (int32_t)pad_left;
                    uint64_t out_off = (uint64_t)oh * w_out + ow;

                    bool inside = (ih_base >= 0 && iw_base >= 0 &&
                                   (uint32_t)(ih_base + 2) < h_in &&
                                   (uint32_t)(iw_base + 2) < w_in);

                    uint32_t oc = 0;
                    for (; oc + 3 < c_out; oc += 4) {
                        const float* w0 = w_data + ((uint64_t)(oc + 0) * w_oc_stride);
                        const float* w1 = w_data + ((uint64_t)(oc + 1) * w_oc_stride);
                        const float* w2 = w_data + ((uint64_t)(oc + 2) * w_oc_stride);
                        const float* w3 = w_data + ((uint64_t)(oc + 3) * w_oc_stride);

                        float sum0 = b_data ? b_data[oc + 0] : 0.0f;
                        float sum1 = b_data ? b_data[oc + 1] : 0.0f;
                        float sum2 = b_data ? b_data[oc + 2] : 0.0f;
                        float sum3 = b_data ? b_data[oc + 3] : 0.0f;

                        for (uint32_t ic = 0; ic < c_in; ic++) {
                            const float* x_ic = x_nb + ((uint64_t)ic * x_c_stride);
                            const float* w0_ic = w0 + ((uint64_t)ic * 9);
                            const float* w1_ic = w1 + ((uint64_t)ic * 9);
                            const float* w2_ic = w2 + ((uint64_t)ic * 9);
                            const float* w3_ic = w3 + ((uint64_t)ic * 9);

                            if (inside) {
                                uint64_t row0 = (uint64_t)(uint32_t)ih_base * w_in + (uint32_t)iw_base;
                                uint64_t row1 = row0 + w_in;
                                uint64_t row2 = row1 + w_in;

                                float x00 = x_ic[row0 + 0], x01 = x_ic[row0 + 1], x02 = x_ic[row0 + 2];
                                float x10 = x_ic[row1 + 0], x11 = x_ic[row1 + 1], x12 = x_ic[row1 + 2];
                                float x20 = x_ic[row2 + 0], x21 = x_ic[row2 + 1], x22 = x_ic[row2 + 2];

                                sum0 += x00 * w0_ic[0] + x01 * w0_ic[1] + x02 * w0_ic[2]
                                     +  x10 * w0_ic[3] + x11 * w0_ic[4] + x12 * w0_ic[5]
                                     +  x20 * w0_ic[6] + x21 * w0_ic[7] + x22 * w0_ic[8];
                                sum1 += x00 * w1_ic[0] + x01 * w1_ic[1] + x02 * w1_ic[2]
                                     +  x10 * w1_ic[3] + x11 * w1_ic[4] + x12 * w1_ic[5]
                                     +  x20 * w1_ic[6] + x21 * w1_ic[7] + x22 * w1_ic[8];
                                sum2 += x00 * w2_ic[0] + x01 * w2_ic[1] + x02 * w2_ic[2]
                                     +  x10 * w2_ic[3] + x11 * w2_ic[4] + x12 * w2_ic[5]
                                     +  x20 * w2_ic[6] + x21 * w2_ic[7] + x22 * w2_ic[8];
                                sum3 += x00 * w3_ic[0] + x01 * w3_ic[1] + x02 * w3_ic[2]
                                     +  x10 * w3_ic[3] + x11 * w3_ic[4] + x12 * w3_ic[5]
                                     +  x20 * w3_ic[6] + x21 * w3_ic[7] + x22 * w3_ic[8];
                            } else {
                                for (uint32_t kh = 0; kh < 3; kh++) {
                                    int32_t ih = ih_base + (int32_t)kh;
                                    if (ih < 0 || ih >= (int32_t)h_in) continue;

                                    uint64_t x_row = (uint64_t)(uint32_t)ih * w_in;
                                    uint64_t w_row = (uint64_t)kh * 3;
                                    for (uint32_t kw = 0; kw < 3; kw++) {
                                        int32_t iw = iw_base + (int32_t)kw;
                                        if (iw < 0 || iw >= (int32_t)w_in) continue;

                                        float xv = x_ic[x_row + (uint32_t)iw];
                                        sum0 += xv * w0_ic[w_row + kw];
                                        sum1 += xv * w1_ic[w_row + kw];
                                        sum2 += xv * w2_ic[w_row + kw];
                                        sum3 += xv * w3_ic[w_row + kw];
                                    }
                                }
                            }
                        }

                        y_nb[(uint64_t)(oc + 0) * y_c_stride + out_off] = conv_apply_relu(sum0, fuse_relu);
                        y_nb[(uint64_t)(oc + 1) * y_c_stride + out_off] = conv_apply_relu(sum1, fuse_relu);
                        y_nb[(uint64_t)(oc + 2) * y_c_stride + out_off] = conv_apply_relu(sum2, fuse_relu);
                        y_nb[(uint64_t)(oc + 3) * y_c_stride + out_off] = conv_apply_relu(sum3, fuse_relu);
                    }

                    for (; oc < c_out; oc++) {
                        const float* w_oc = w_data + ((uint64_t)oc * w_oc_stride);
                        float sum = b_data ? b_data[oc] : 0.0f;

                        for (uint32_t ic = 0; ic < c_in; ic++) {
                            const float* x_ic = x_nb + ((uint64_t)ic * x_c_stride);
                            const float* w_ic = w_oc + ((uint64_t)ic * 9);

                            if (inside) {
                                uint64_t row0 = (uint64_t)(uint32_t)ih_base * w_in + (uint32_t)iw_base;
                                uint64_t row1 = row0 + w_in;
                                uint64_t row2 = row1 + w_in;

                                sum += x_ic[row0 + 0] * w_ic[0] + x_ic[row0 + 1] * w_ic[1] + x_ic[row0 + 2] * w_ic[2]
                                     + x_ic[row1 + 0] * w_ic[3] + x_ic[row1 + 1] * w_ic[4] + x_ic[row1 + 2] * w_ic[5]
                                     + x_ic[row2 + 0] * w_ic[6] + x_ic[row2 + 1] * w_ic[7] + x_ic[row2 + 2] * w_ic[8];
                            } else {
                                for (uint32_t kh = 0; kh < 3; kh++) {
                                    int32_t ih = ih_base + (int32_t)kh;
                                    if (ih < 0 || ih >= (int32_t)h_in) continue;

                                    uint64_t x_row = (uint64_t)(uint32_t)ih * w_in;
                                    uint64_t w_row = (uint64_t)kh * 3;
                                    for (uint32_t kw = 0; kw < 3; kw++) {
                                        int32_t iw = iw_base + (int32_t)kw;
                                        if (iw < 0 || iw >= (int32_t)w_in) continue;
                                        sum += x_ic[x_row + (uint32_t)iw] * w_ic[w_row + kw];
                                    }
                                }
                            }
                        }

                        y_nb[(uint64_t)oc * y_c_stride + out_off] = conv_apply_relu(sum, fuse_relu);
                    }
                }
            }
        }
        return STATUS_OK;
    }

    for (uint32_t nb = 0; nb < batch_n; nb++) {
        float* y_nb = y_data + nb * y_batch_stride;
        const float* x_nb = x_data + nb * x_batch_stride;

        for (uint32_t g = 0; g < group; g++) {
            uint32_t ic_base = g * c_in_per_group;
            uint32_t oc_base = g * c_out_per_group;

            for (uint32_t ocg = 0; ocg < c_out_per_group; ocg++) {
                uint32_t oc = oc_base + ocg;
                float* y_oc = y_nb + oc * y_c_stride;
                const float* w_oc = w_data + oc * w_oc_stride;

                for (uint32_t oh = 0; oh < h_out; oh++) {
                    int32_t ih_base = (int32_t)(oh * stride_h) - (int32_t)pad_top;
                    for (uint32_t ow = 0; ow < w_out; ow++) {
                        int32_t iw_base = (int32_t)(ow * stride_w) - (int32_t)pad_left;
                        float sum = b_data ? b_data[oc] : 0.0f;

                        for (uint32_t icg = 0; icg < c_in_per_group; icg++) {
                            const float* x_ic = x_nb + ((uint64_t)(ic_base + icg) * x_c_stride);
                            const float* w_ic = w_oc + ((uint64_t)icg * k_h * k_w);

                            for (uint32_t kh = 0; kh < k_h; kh++) {
                                int32_t ih = ih_base + (int32_t)(kh * dilation_h);
                                if (ih < 0 || ih >= (int32_t)h_in) continue;

                                uint64_t x_row = (uint64_t)ih * w_in;
                                uint64_t w_row = (uint64_t)kh * k_w;

                                for (uint32_t kw = 0; kw < k_w; kw++) {
                                    int32_t iw = iw_base + (int32_t)(kw * dilation_w);
                                    if (iw >= 0 && iw < (int32_t)w_in) {
                                        sum += x_ic[x_row + (uint32_t)iw] *
                                               w_ic[w_row + kw];
                                    }
                                }
                            }
                        }
                        y_oc[(uint64_t)oh * w_out + ow] = conv_apply_relu(sum, fuse_relu);
                    }
                }
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

    uint32_t k_h = (node->attributes.kernel_shape_len >= 1) ? (uint32_t)node->attributes.kernel_shape[0] : 2;
    uint32_t k_w = (node->attributes.kernel_shape_len >= 2) ? (uint32_t)node->attributes.kernel_shape[1] : 2;

    uint32_t stride_h = (node->attributes.strides_len >= 1) ? (uint32_t)node->attributes.strides[0] : 2;
    uint32_t stride_w = (node->attributes.strides_len >= 2) ? (uint32_t)node->attributes.strides[1] : stride_h;

    uint32_t pad_h = (node->attributes.pads_len >= 1) ? (uint32_t)node->attributes.pads[0] : 0;
    uint32_t pad_w = (node->attributes.pads_len >= 2) ? (uint32_t)node->attributes.pads[1] : 0;

    uint32_t h_out = (h_in + 2 * pad_h - k_h) / stride_h + 1;
    uint32_t w_out = (w_in + 2 * pad_w - k_w) / stride_w + 1;

    y->shape.ndim = 4;
    y->shape.dims[0] = x->shape.dims[0];
    y->shape.dims[1] = c;
    y->shape.dims[2] = h_out;
    y->shape.dims[3] = w_out;
    y->shape.total_elements = y->shape.dims[0] * y->shape.dims[1] * h_out * w_out;

    float* x_data = (float*)x->data;
    float* y_data = (float*)y->data;

    uint32_t batch_n = (uint32_t)x->shape.dims[0];
    for (uint32_t nb = 0; nb < batch_n; nb++) {
        for (uint32_t ic = 0; ic < c; ic++) {
            for (uint32_t oh = 0; oh < h_out; oh++) {
                for (uint32_t ow = 0; ow < w_out; ow++) {
                    float max_val = -1e38f;

                    for (uint32_t kh = 0; kh < k_h; kh++) {
                        for (uint32_t kw = 0; kw < k_w; kw++) {
                            int32_t ih = (int32_t)(oh * stride_h + kh) - (int32_t)pad_h;
                            int32_t iw = (int32_t)(ow * stride_w + kw) - (int32_t)pad_w;

                            if (ih >= 0 && ih < (int32_t)h_in && iw >= 0 && iw < (int32_t)w_in) {
                                float val = x_data[nb * c * h_in * w_in +
                                                    ic * h_in * w_in +
                                                    (uint32_t)ih * w_in +
                                                    (uint32_t)iw];
                                if (val > max_val) max_val = val;
                            }
                        }
                    }
                    y_data[nb * c * h_out * w_out +
                           ic * h_out * w_out +
                           oh * w_out + ow] = max_val;
                }
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

    uint32_t k_h = (node->attributes.kernel_shape_len >= 1) ? (uint32_t)node->attributes.kernel_shape[0] : 2;
    uint32_t k_w = (node->attributes.kernel_shape_len >= 2) ? (uint32_t)node->attributes.kernel_shape[1] : 2;

    uint32_t stride_h = (node->attributes.strides_len >= 1) ? (uint32_t)node->attributes.strides[0] : 2;
    uint32_t stride_w = (node->attributes.strides_len >= 2) ? (uint32_t)node->attributes.strides[1] : stride_h;

    uint32_t pad_h = (node->attributes.pads_len >= 1) ? (uint32_t)node->attributes.pads[0] : 0;
    uint32_t pad_w = (node->attributes.pads_len >= 2) ? (uint32_t)node->attributes.pads[1] : 0;

    uint32_t h_out = (h_in + 2 * pad_h - k_h) / stride_h + 1;
    uint32_t w_out = (w_in + 2 * pad_w - k_w) / stride_w + 1;

    y->shape.ndim = 4;
    y->shape.dims[0] = x->shape.dims[0];
    y->shape.dims[1] = c;
    y->shape.dims[2] = h_out;
    y->shape.dims[3] = w_out;
    y->shape.total_elements = y->shape.dims[0] * y->shape.dims[1] * h_out * w_out;

    float* x_data = (float*)x->data;
    float* y_data = (float*)y->data;

    uint32_t batch_n = x->shape.dims[0];
    for (uint32_t nb = 0; nb < batch_n; nb++) {
        for (uint32_t ic = 0; ic < c; ic++) {
            for (uint32_t oh = 0; oh < h_out; oh++) {
                for (uint32_t ow = 0; ow < w_out; ow++) {
                    float sum = 0.0f;
                    int count = 0;

                    for (uint32_t kh = 0; kh < k_h; kh++) {
                        for (uint32_t kw = 0; kw < k_w; kw++) {
                            int32_t ih = oh * stride_h + kh - pad_h;
                            int32_t iw = ow * stride_w + kw - pad_w;

                            if (ih >= 0 && ih < (int32_t)h_in && iw >= 0 && iw < (int32_t)w_in) {
                                sum += x_data[nb * c * h_in * w_in + ic * h_in * w_in + ih * w_in + iw];
                                count++;
                            }
                        }
                    }
                    y_data[nb * c * h_out * w_out + ic * h_out * w_out + oh * w_out + ow] = count > 0 ? (sum / count) : 0.0f;
                }
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

    uint64_t _t0 = 0;
    if (g_runtime_node_profiling) {
        _t0 = HAL_Timer_GetTicks();
    }

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
        case ONNX_OP_SPLIT:
            status = ONNX_Execute_Split(node, ctx);
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

        case ONNX_OP_ABS:
            status = ONNX_Execute_Abs(node, ctx);
            break;

        case ONNX_OP_NEG:
            status = ONNX_Execute_Neg(node, ctx);
            break;

        case ONNX_OP_EXP:
            status = ONNX_Execute_Exp(node, ctx);
            break;

        case ONNX_OP_LOG:
            status = ONNX_Execute_Log(node, ctx);
            break;

        case ONNX_OP_SQRT:
            status = ONNX_Execute_Sqrt(node, ctx);
            break;

        case ONNX_OP_CEIL:
            status = ONNX_Execute_Ceil(node, ctx);
            break;

        case ONNX_OP_FLOOR:
            status = ONNX_Execute_Floor(node, ctx);
            break;

        case ONNX_OP_SIN:
            status = ONNX_Execute_Sin(node, ctx);
            break;

        case ONNX_OP_COS:
            status = ONNX_Execute_Cos(node, ctx);
            break;

        case ONNX_OP_IDENTITY:
            status = ONNX_Execute_Identity(node, ctx);
            break;
        case ONNX_OP_CONSTANT:
            status = ONNX_Execute_Constant(node, ctx);
            break;

        case ONNX_OP_LRN:
            status = ONNX_Execute_LRN(node, ctx);
            break;

        case ONNX_OP_CLIP:
            status = ONNX_Execute_Clip(node, ctx);
            break;

        case ONNX_OP_REDUCESUM:
            status = ONNX_Execute_ReduceSum(node, ctx);
            break;

        case ONNX_OP_REDUCEMEAN:
            status = ONNX_Execute_ReduceMean(node, ctx);
            break;

        case ONNX_OP_REDUCEMAX:
            status = ONNX_Execute_ReduceMax(node, ctx);
            break;

        case ONNX_OP_REDUCEMIN:
            status = ONNX_Execute_ReduceMin(node, ctx);
            break;

        case ONNX_OP_DROPOUT:
            /* In inference mode, Dropout is a no-op (identity pass-through) */
            if (node->num_inputs >= 1 && node->num_outputs >= 1) {
                ONNX_Tensor* in = node->inputs[0];
                ONNX_Tensor* out = node->outputs[0];
                out->dtype = in->dtype;
                out->shape = in->shape;
                out->data_size = in->data_size;
                out->data = in->data;
                status = STATUS_OK;
            } else {
                status = STATUS_ERROR_INVALID_GRAPH;
            }
            break;

        default:
            HAL_UART_PutString("[ONNX] Error: Unsupported operator: ");
            HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
            HAL_UART_PutString("\n");
            return STATUS_ERROR_UNSUPPORTED_OPERATOR;
    }

    if (status == STATUS_OK && g_runtime_node_profiling) {
        node->exec_time_us += HAL_Timer_GetElapsedUs(_t0);
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
    
    if (inputs) {
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

            if (!graph_input || !user_input || !graph_input->data || !user_input->data) {
                return STATUS_ERROR_INVALID_ARGUMENT;
            }

            if (graph_input == user_input || graph_input->data == user_input->data) {
                continue;
            }
            
            if (graph_input->data_size != user_input->data_size) {
                return STATUS_ERROR_SHAPE_MISMATCH;
            }
            
            mem_copy(graph_input->data, user_input->data, user_input->data_size);
        }
    }
    
    /* Execute all nodes in scheduled order */
    if (g_runtime_verbose) {
        HAL_UART_PutString("[ONNX] Starting inference...\n");
    }
    
    for (uint32_t i = 0; i < graph->schedule_length; i++) {
        ONNX_Node* node = graph->exec_schedule[i];
        g_runtime_current_node = node;
        
        if (g_runtime_verbose) {
            HAL_UART_PutString("  Executing: ");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString(" (");
            HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
            HAL_UART_PutString(")\n");
        }
        
        if (g_runtime_prepare_node_outputs) {
            /* Allocate output tensors for this node if not yet allocated.
             * This handles intermediate tensors that are neither graph inputs
             * (allocated by the caller) nor initializers (pre-populated by loader).
             * Also infer shape from the first non-init input if the output is still
             * a placeholder with total_elements <= 1. */
            for (uint32_t j = 0; j < node->num_outputs; j++) {
                ONNX_Tensor* out_t = node->outputs[j];
                if (!out_t) continue;
                /* Infer shape from first non-initializer input if still a placeholder */
                if (out_t->shape.total_elements <= 1 && node->num_inputs > 0) {
                    for (uint32_t k = 0; k < node->num_inputs; k++) {
                        ONNX_Tensor* in_t = node->inputs[k];
                        if (in_t && !in_t->is_initializer && in_t->shape.total_elements > 1) {
                            out_t->shape = in_t->shape;
                            out_t->data_size = in_t->data_size;
                            break;
                        }
                    }
                }
                if (out_t->data == NULL && out_t->data_size > 0) {
                    ONNX_Graph_AllocateTensor(graph, out_t);
                }
            }
        }

        Status status = ONNX_Runtime_ExecuteNode(ctx, node);

        if (status != STATUS_OK) {
            HAL_UART_PutString("[ONNX] Error: Node execution failed: ");
            HAL_UART_PutString(STATUS_ToString(status));
            HAL_UART_PutString(" at node '");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString("' (");
            HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
            HAL_UART_PutString(")");
            HAL_UART_PutString("\n");
            return status;
        }

        if (g_runtime_yield_between_nodes) {
            THREAD_Yield();
        }
    }

    g_runtime_current_node = NULL;
    
    if (outputs) {
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
    }
    
    ctx->total_inferences++;
    
    if (g_runtime_verbose) {
        HAL_UART_PutString("[ONNX] Inference complete!\n");
    }
    
    return STATUS_OK;
}

Status ONNX_Runtime_InferenceSimple(ONNX_InferenceContext* ctx,
                                      const void** input_data,
                                      uint64_t* input_sizes,
                                      uint32_t num_inputs,
                                      void** output_data,
                                      uint64_t* output_sizes,
                                      uint32_t num_outputs)
{
    if (!ctx || !ctx->graph || !input_data || !input_sizes || !output_data || !output_sizes) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    ONNX_Graph* graph = ctx->graph;

    if (num_inputs != graph->num_inputs) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* Copy input data to graph input tensors */
    for (uint32_t i = 0; i < num_inputs; i++) {
        ONNX_Tensor* in_t = graph->inputs[i];
        if (input_sizes[i] > in_t->data_size) {
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
        mem_copy(in_t->data, input_data[i], input_sizes[i]);
    }

    /* Run full inference */
    Status status = ONNX_Runtime_Inference(ctx, NULL, 0, NULL, 0);
    /* 
     * Note: We pass NULL/0/NULL/0 to ONNX_Runtime_Inference because it 
     * already has a path that copies inputs if they are provided, but 
     * we've already done the copy above to the graph's internal tensors.
     * We need to modify ONNX_Runtime_Inference slightly to allow this 
     * or handle the copy there. 
     * Currently ONNX_Runtime_Inference (line 1742) checks num_inputs.
     */

    if (status != STATUS_OK) return status;

    /* Copy output data from graph output tensors */
    uint32_t n_out = (num_outputs < graph->num_outputs) ? num_outputs : graph->num_outputs;
    for (uint32_t i = 0; i < n_out; i++) {
        ONNX_Tensor* out_t = graph->outputs[i];
        uint64_t copy_size = (output_sizes[i] < out_t->data_size) ? output_sizes[i] : out_t->data_size;
        mem_copy(output_data[i], out_t->data, copy_size);
        output_sizes[i] = copy_size;
    }

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
