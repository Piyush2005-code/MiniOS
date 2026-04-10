/**
 * @file onnx_runtime_tiny.c
 * @brief Tiny ONNX Inference Runtime for MiniOS-ESP8266
 *
 * Implements int8-quantized forward-pass inference over the miniaturized
 * ONNX_Graph structure defined in onnx_types.h.
 *
 * Supported operators (all others return STATUS_ERROR_NOT_SUPPORTED):
 *   - Add, Mul              (element-wise, int8 + int16 accumulator)
 *   - MatMul, GEMM          (int8 weights × int8 activations, int16 acc)
 *   - ReLU                  (thresholded at zero_point)
 *   - Sigmoid, Softmax      (float32 path — used only for output layer)
 *   - Reshape, Flatten      (shape-only ops, no data move)
 *   - Identity              (pass-through)
 *
 * Inference flow for a typical 4→8→4 MLP:
 *   Client sends float32 inputs over SFU →
 *   Quantize to int8 →
 *   MatMul(W1, input) + b1 → ReLU → hidden (int8) →
 *   MatMul(W2, hidden) + b2 → Softmax → float32 outputs →
 *   Dequantize → SFU response
 *
 * No dynamic allocation during inference — all buffers are static.
 */

#include "onnx/onnx_runtime.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Static inference buffers (zero heap allocation during inference)  */
/* ------------------------------------------------------------------ */

/* Intermediate activation buffers — int16 accumulator, int8 activations */
static int16_t g_acc_buf[64];   /* accumulator for MatMul (int16) */
static int8_t  g_act_buf[64];   /* post-activation buffer */
static float   g_softmax_buf[ONNX_TINY_MAX_TENSORS > 32 ? 32 : ONNX_TINY_MAX_TENSORS];

/* ------------------------------------------------------------------ */
/*  Internal: fast software-float helpers (no FPU on ESP8266)         */
/* ------------------------------------------------------------------ */

static float fast_exp(float x)
{
    if (x < -10.0f) return 0.0f;
    if (x >  10.0f) return 22026.0f;
    float sum = 1.0f, term = 1.0f;
    for (int i = 1; i < 8; i++) { term *= x / (float)i; sum += term; }
    return sum;
}

static float fast_sigmoid(float x)
{
    return 1.0f / (1.0f + fast_exp(-x));
}

/* ------------------------------------------------------------------ */
/*  Internal: int8 MatMul  A[M×K] × B[K×N] → C[M×N]                 */
/*  Uses int16 accumulator to prevent overflow.                       */
/* ------------------------------------------------------------------ */

static void matmul_int8(
    const int8_t *A, const int8_t *B, int16_t *C,
    uint8_t M, uint8_t K, uint8_t N)
{
    for (uint8_t i = 0; i < M; i++) {
        for (uint8_t j = 0; j < N; j++) {
            int16_t acc = 0;
            for (uint8_t k = 0; k < K; k++) {
                acc += (int16_t)A[i * K + k] * (int16_t)B[k * N + j];
            }
            C[i * N + j] = acc;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Internal: ReLU on int16 accumulator → int8 output                */
/* ------------------------------------------------------------------ */

static void relu_int16_to_int8(
    const int16_t *in, int8_t *out, uint8_t n, int8_t zero_point)
{
    for (uint8_t i = 0; i < n; i++) {
        int16_t v = in[i];
        if (v < (int16_t)zero_point) v = zero_point;
        /* Clamp to int8 range */
        if (v >  127) v =  127;
        if (v < -128) v = -128;
        out[i] = (int8_t)v;
    }
}

/* ------------------------------------------------------------------ */
/*  Internal: Softmax over float buffer                               */
/* ------------------------------------------------------------------ */

static void softmax_float(float *buf, uint8_t n)
{
    float max_v = buf[0];
    for (uint8_t i = 1; i < n; i++) if (buf[i] > max_v) max_v = buf[i];

    float sum = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        buf[i] = fast_exp(buf[i] - max_v);
        sum += buf[i];
    }
    if (sum > 0.0f) {
        for (uint8_t i = 0; i < n; i++) buf[i] /= sum;
    }
}

/* ------------------------------------------------------------------ */
/*  Internal: execute a single ONNX node                              */
/* ------------------------------------------------------------------ */

static Status execute_node(ONNX_Node *node, ONNX_Graph *graph)
{
    /* Helper: get tensor pointer by index */
    #define T(idx) (&graph->tensors[(idx)])

    switch (node->op_type) {

    /* ---- Identity / Reshape / Flatten: no data movement ---- */
    case ONNX_OP_IDENTITY:
    case ONNX_OP_RESHAPE:
    case ONNX_OP_FLATTEN: {
        /* Output tensor shares same data pointer as input */
        ONNX_Tensor *in  = T(node->input_idx[0]);
        ONNX_Tensor *out = T(node->output_idx[0]);
        out->data      = in->data;
        out->data_size = in->data_size;
        out->dtype     = in->dtype;
        return STATUS_OK;
    }

    /* ---- Add: element-wise addition ---- */
    case ONNX_OP_ADD: {
        ONNX_Tensor *a   = T(node->input_idx[0]);
        ONNX_Tensor *b   = T(node->input_idx[1]);
        ONNX_Tensor *out = T(node->output_idx[0]);
        uint16_t n = a->shape.total_elements;
        if (a->dtype == ONNX_DTYPE_INT8) {
            const int8_t *pa = (const int8_t *)a->data;
            const int8_t *pb = (const int8_t *)b->data;
            int8_t       *po = (int8_t *)out->data;
            for (uint16_t i = 0; i < n; i++) {
                int16_t v = (int16_t)pa[i] + (int16_t)pb[i];
                po[i] = (int8_t)(v >  127 ?  127 : v < -128 ? -128 : v);
            }
        } else {
            /* float32 fallback */
            const float *pa = (const float *)a->data;
            const float *pb = (const float *)b->data;
            float *po = (float *)out->data;
            uint16_t nb = b->shape.total_elements;
            for (uint16_t i = 0; i < n; i++) {
                po[i] = pa[i] + (nb == 1 ? pb[0] : pb[i % nb]);
            }
        }
        return STATUS_OK;
    }

    /* ---- Mul: element-wise multiplication ---- */
    case ONNX_OP_MUL: {
        ONNX_Tensor *a   = T(node->input_idx[0]);
        ONNX_Tensor *b   = T(node->input_idx[1]);
        ONNX_Tensor *out = T(node->output_idx[0]);
        uint16_t n  = a->shape.total_elements;
        uint16_t nb = b->shape.total_elements;
        const float *pa = (const float *)a->data;
        const float *pb = (const float *)b->data;
        float *po = (float *)out->data;
        for (uint16_t i = 0; i < n; i++) {
            po[i] = pa[i] * (nb == 1 ? pb[0] : pb[i % nb]);
        }
        return STATUS_OK;
    }

    /* ---- MatMul / GEMM: matrix multiply ---- */
    case ONNX_OP_MATMUL:
    case ONNX_OP_GEMM: {
        ONNX_Tensor *A   = T(node->input_idx[0]);
        ONNX_Tensor *B   = T(node->input_idx[1]);
        ONNX_Tensor *out = T(node->output_idx[0]);

        uint8_t M = (uint8_t)A->shape.dims[0];
        uint8_t K = (uint8_t)A->shape.dims[1];
        uint8_t N = (uint8_t)B->shape.dims[1];

        if (M == 0) M = 1; /* vector: treat as [1 × K] */

        if (A->dtype == ONNX_DTYPE_INT8 && B->dtype == ONNX_DTYPE_INT8) {
            matmul_int8((const int8_t *)A->data,
                        (const int8_t *)B->data,
                        g_acc_buf, M, K, N);
            /* Store raw int16 results in output — caller applies activation */
            int16_t *po = (int16_t *)out->data;
            uint16_t total = (uint16_t)(M * N);
            for (uint16_t i = 0; i < total; i++) po[i] = g_acc_buf[i];
            out->dtype = ONNX_DTYPE_INT32; /* signal "int16 in int32 slot" */
        } else {
            /* float32 path */
            const float *pa = (const float *)A->data;
            const float *pb = (const float *)B->data;
            float *po = (float *)out->data;
            for (uint8_t i = 0; i < M; i++) {
                for (uint8_t j = 0; j < N; j++) {
                    float acc = 0.0f;
                    for (uint8_t k = 0; k < K; k++) {
                        acc += pa[i * K + k] * pb[k * N + j];
                    }
                    po[i * N + j] = acc;
                }
            }
        }

        /* GEMM: add bias (input[2]) and apply alpha/beta */
        if (node->op_type == ONNX_OP_GEMM && node->num_inputs >= 3) {
            ONNX_Tensor *bias = T(node->input_idx[2]);
            uint16_t n = out->shape.total_elements;
            if (out->dtype == ONNX_DTYPE_FLOAT32) {
                float *po = (float *)out->data;
                const float *pb2 = (const float *)bias->data;
                uint16_t nb = bias->shape.total_elements;
                for (uint16_t i = 0; i < n; i++) {
                    po[i] = po[i] * node->attrs.alpha + pb2[i % nb] * node->attrs.beta;
                }
            }
        }

        return STATUS_OK;
    }

    /* ---- ReLU ---- */
    case ONNX_OP_RELU: {
        ONNX_Tensor *in  = T(node->input_idx[0]);
        ONNX_Tensor *out = T(node->output_idx[0]);
        uint16_t n = in->shape.total_elements;

        if (in->dtype == ONNX_DTYPE_INT8) {
            const int8_t *pi = (const int8_t *)in->data;
            int8_t       *po = (int8_t *)out->data;
            for (uint16_t i = 0; i < n; i++) po[i] = pi[i] < 0 ? 0 : pi[i];
        } else {
            const float *pi = (const float *)in->data;
            float       *po = (float *)out->data;
            for (uint16_t i = 0; i < n; i++) po[i] = pi[i] < 0.0f ? 0.0f : pi[i];
        }
        out->dtype = in->dtype;
        return STATUS_OK;
    }

    /* ---- Sigmoid ---- */
    case ONNX_OP_SIGMOID: {
        ONNX_Tensor *in  = T(node->input_idx[0]);
        ONNX_Tensor *out = T(node->output_idx[0]);
        uint16_t n = in->shape.total_elements;
        const float *pi = (const float *)in->data;
        float       *po = (float *)out->data;
        for (uint16_t i = 0; i < n; i++) po[i] = fast_sigmoid(pi[i]);
        return STATUS_OK;
    }

    /* ---- Softmax ---- */
    case ONNX_OP_SOFTMAX: {
        ONNX_Tensor *in  = T(node->input_idx[0]);
        ONNX_Tensor *out = T(node->output_idx[0]);
        uint16_t n = in->shape.total_elements;
        if (n > 32) n = 32;

        const float *pi = (const float *)in->data;
        float       *po = (float *)out->data;
        for (uint16_t i = 0; i < n; i++) g_softmax_buf[i] = pi[i];
        softmax_float(g_softmax_buf, (uint8_t)n);
        for (uint16_t i = 0; i < n; i++) po[i] = g_softmax_buf[i];
        return STATUS_OK;
    }

    default:
        HAL_UART_PutString("[ONNX] unsupported op: ");
        HAL_UART_PutDec((uint32_t)node->op_type);
        HAL_UART_PutString("\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    #undef T
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status ONNX_Runtime_Init(ONNX_InferenceContext *ctx, ONNX_Graph *graph,
                          uint32_t workspace_size)
{
    (void)workspace_size; /* no dynamic workspace on ESP8266 */
    if (!ctx || !graph) return STATUS_ERROR_INVALID_ARGUMENT;
    ctx->graph             = graph;
    ctx->total_inferences  = 0;
    ctx->total_time_us     = 0;
    return STATUS_OK;
}

void ONNX_Runtime_Cleanup(ONNX_InferenceContext *ctx)
{
    if (ctx) {
        ctx->graph = (ONNX_Graph *)0;
        ctx->total_inferences = 0;
        ctx->total_time_us    = 0;
    }
}

Status ONNX_Runtime_InferenceSimple(
    ONNX_InferenceContext *ctx,
    const void **in_ptrs,  uint32_t *in_sizes,  uint8_t num_inputs,
    void       **out_ptrs, uint32_t *out_sizes,  uint8_t num_outputs)
{
    if (!ctx || !ctx->graph) return STATUS_ERROR_NOT_INITIALIZED;

    ONNX_Graph *graph = ctx->graph;
    uint32_t t0 = HAL_Timer_GetTicks();

    /* Bind external input data to graph input tensors */
    for (uint8_t i = 0; i < num_inputs && i < graph->num_inputs; i++) {
        ONNX_Tensor *t = &graph->tensors[graph->input_idx[i]];
        t->data      = (void *)in_ptrs[i];
        t->data_size = (uint16_t)in_sizes[i];
    }

    /* Bind external output buffers to graph output tensors */
    for (uint8_t i = 0; i < num_outputs && i < graph->num_outputs; i++) {
        ONNX_Tensor *t = &graph->tensors[graph->output_idx[i]];
        t->data      = out_ptrs[i];
        t->data_size = (uint16_t)out_sizes[i];
    }

    /* Execute nodes in scheduled order */
    for (uint8_t s = 0; s < graph->schedule_len; s++) {
        uint8_t node_idx = graph->schedule[s];
        if (node_idx >= graph->num_nodes) continue;
        Status st = execute_node(&graph->nodes[node_idx], graph);
        if (st != STATUS_OK) return st;
    }

    uint32_t elapsed = HAL_Timer_GetElapsedUs(t0);
    ctx->total_inferences++;
    ctx->total_time_us += elapsed;

#if DEBUG_INFER_TIMING
    HAL_UART_PutString("[ONNX] infer #");
    HAL_UART_PutDec(ctx->total_inferences);
    HAL_UART_PutString(" ");
    HAL_UART_PutDec(elapsed);
    HAL_UART_PutString(" us\n");
#endif

    /* Write output sizes back */
    for (uint8_t i = 0; i < num_outputs && i < graph->num_outputs; i++) {
        ONNX_Tensor *t = &graph->tensors[graph->output_idx[i]];
        out_sizes[i] = t->data_size;
    }

    return STATUS_OK;
}

const char *ONNX_GetOperatorName(ONNX_OperatorType op)
{
    switch (op) {
        case ONNX_OP_ADD:      return "Add";
        case ONNX_OP_MUL:      return "Mul";
        case ONNX_OP_MATMUL:   return "MatMul";
        case ONNX_OP_GEMM:     return "Gemm";
        case ONNX_OP_RELU:     return "ReLU";
        case ONNX_OP_SIGMOID:  return "Sigmoid";
        case ONNX_OP_SOFTMAX:  return "Softmax";
        case ONNX_OP_RESHAPE:  return "Reshape";
        case ONNX_OP_FLATTEN:  return "Flatten";
        case ONNX_OP_IDENTITY: return "Identity";
        default:               return "Unknown";
    }
}
