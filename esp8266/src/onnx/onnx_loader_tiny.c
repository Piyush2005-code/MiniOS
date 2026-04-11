/**
 * @file onnx_loader_tiny.c
 * @brief Embedded ONNX Model Loader for MiniOS-ESP8266
 *
 * Loads pre-quantized int8 models from compiled-in C header arrays.
 * No filesystem access, no Protobuf parsing — models live in flash.
 *
 * Supported models (selected via model name string):
 *   "tiny_mlp"  — 4→8→4 MLP, ReLU hidden, Softmax output
 *   "tiny_add"  — simple [Add] test model (2 inputs → 1 output)
 *
 * Graph built at load time into the caller-provided ONNX_Graph struct.
 */

#include "onnx/onnx_loader.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "types.h"
#include "models/tiny_mlp_model.h"

/* ------------------------------------------------------------------ */
/*  Internal: string compare                                          */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == *b);
}

/* ------------------------------------------------------------------ */
/*  Internal: zero-fill                                               */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR void mem_zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

/* ------------------------------------------------------------------ */
/*  Static model data buffers (weight data lives in flash via const)  */
/* Static activation buffers for intermediate tensors                 */
/* ------------------------------------------------------------------ */

static float  g_input_buf[TINY_MLP_INPUT_SIZE];
static float  g_output_buf[TINY_MLP_OUTPUT_SIZE];

/* int8 intermediate activations: hidden layer */
static int8_t g_hidden_buf[TINY_MLP_HIDDEN_SIZE];

/* Float buffer for softmax output */
static float  g_pre_softmax_buf[TINY_MLP_OUTPUT_SIZE];

/* ------------------------------------------------------------------ */
/*  ONNX_LoadEmbedded — load a named model into graph                 */
/* ------------------------------------------------------------------ */

Status ICACHE_FLASH_ATTR ONNX_LoadEmbedded(ONNX_Graph *graph, const char *model_name)
{
    if (!graph || !model_name) return STATUS_ERROR_INVALID_ARGUMENT;

    mem_zero(graph, sizeof(ONNX_Graph));

    /* ---- tiny_mlp: 4→Dense(8,ReLU)→Dense(4,Softmax)→4 ---- */
    if (str_eq(model_name, "tiny_mlp")) {
        const TinyMLP_Descriptor *m = &TINY_MLP_MODEL;

        /*
         * Graph layout:
         *   tensor[0] = input    [1×4, float32] — bound at inference time
         *   tensor[1] = W1       [8×4, int8]    — flash constant
         *   tensor[2] = b1       [8,   int8]    — flash constant
         *   tensor[3] = hidden   [1×8, int8]    — g_hidden_buf
         *   tensor[4] = W2       [4×8, int8]    — flash constant
         *   tensor[5] = b2       [4,   int8]    — flash constant
         *   tensor[6] = pre_soft [1×4, float32] — g_pre_softmax_buf
         *   tensor[7] = output   [1×4, float32] — bound at inference time
         *
         * Nodes:
         *   node[0] = GEMM(input[0], W1[1], b1[2]) → hidden[3]  + ReLU
         *   node[1] = GEMM(hidden[3], W2[4], b2[5]) → pre_soft[6]
         *   node[2] = Softmax(pre_soft[6]) → output[7]
         */

        /* tensor[0]: input */
        graph->tensors[0].dtype = ONNX_DTYPE_FLOAT32;
        graph->tensors[0].shape.ndim = 1;
        graph->tensors[0].shape.dims[0] = m->input_size;
        graph->tensors[0].shape.total_elements = m->input_size;
        graph->tensors[0].data_size = (uint16_t)(m->input_size * 4);
        graph->tensors[0].data = g_input_buf;

        /* tensor[1]: W1 (flash const) */
        graph->tensors[1].dtype = ONNX_DTYPE_INT8;
        graph->tensors[1].shape.ndim = 2;
        graph->tensors[1].shape.dims[0] = m->hidden_size;
        graph->tensors[1].shape.dims[1] = m->input_size;
        graph->tensors[1].shape.total_elements = (uint16_t)(m->hidden_size * m->input_size);
        graph->tensors[1].data = (void *)m->W1;
        graph->tensors[1].data_size = (uint16_t)(m->hidden_size * m->input_size);
        graph->tensors[1].is_initializer = 1;

        /* tensor[2]: b1 (flash const) */
        graph->tensors[2].dtype = ONNX_DTYPE_INT8;
        graph->tensors[2].shape.ndim = 1;
        graph->tensors[2].shape.dims[0] = m->hidden_size;
        graph->tensors[2].shape.total_elements = m->hidden_size;
        graph->tensors[2].data = (void *)m->b1;
        graph->tensors[2].data_size = m->hidden_size;
        graph->tensors[2].is_initializer = 1;

        /* tensor[3]: hidden activations */
        graph->tensors[3].dtype = ONNX_DTYPE_INT8;
        graph->tensors[3].shape.ndim = 1;
        graph->tensors[3].shape.dims[0] = m->hidden_size;
        graph->tensors[3].shape.total_elements = m->hidden_size;
        graph->tensors[3].data = g_hidden_buf;
        graph->tensors[3].data_size = m->hidden_size;

        /* tensor[4]: W2 (flash const) */
        graph->tensors[4].dtype = ONNX_DTYPE_INT8;
        graph->tensors[4].shape.ndim = 2;
        graph->tensors[4].shape.dims[0] = m->output_size;
        graph->tensors[4].shape.dims[1] = m->hidden_size;
        graph->tensors[4].shape.total_elements = (uint16_t)(m->output_size * m->hidden_size);
        graph->tensors[4].data = (void *)m->W2;
        graph->tensors[4].data_size = (uint16_t)(m->output_size * m->hidden_size);
        graph->tensors[4].is_initializer = 1;

        /* tensor[5]: b2 (flash const) */
        graph->tensors[5].dtype = ONNX_DTYPE_INT8;
        graph->tensors[5].shape.ndim = 1;
        graph->tensors[5].shape.dims[0] = m->output_size;
        graph->tensors[5].shape.total_elements = m->output_size;
        graph->tensors[5].data = (void *)m->b2;
        graph->tensors[5].data_size = m->output_size;
        graph->tensors[5].is_initializer = 1;

        /* tensor[6]: pre-softmax buffer */
        graph->tensors[6].dtype = ONNX_DTYPE_FLOAT32;
        graph->tensors[6].shape.ndim = 1;
        graph->tensors[6].shape.dims[0] = m->output_size;
        graph->tensors[6].shape.total_elements = m->output_size;
        graph->tensors[6].data = g_pre_softmax_buf;
        graph->tensors[6].data_size = (uint16_t)(m->output_size * 4);

        /* tensor[7]: output */
        graph->tensors[7].dtype = ONNX_DTYPE_FLOAT32;
        graph->tensors[7].shape.ndim = 1;
        graph->tensors[7].shape.dims[0] = m->output_size;
        graph->tensors[7].shape.total_elements = m->output_size;
        graph->tensors[7].data = g_output_buf;
        graph->tensors[7].data_size = (uint16_t)(m->output_size * 4);

        graph->num_tensors = 8;

        /* node[0]: GEMM(W1, input, b1) + ReLU */
        graph->nodes[0].op_type       = ONNX_OP_GEMM;
        graph->nodes[0].num_inputs    = 3;
        graph->nodes[0].num_outputs   = 1;
        graph->nodes[0].input_idx[0]  = 0; /* input */
        graph->nodes[0].input_idx[1]  = 1; /* W1 */
        graph->nodes[0].input_idx[2]  = 2; /* b1 */
        graph->nodes[0].output_idx[0] = 3; /* hidden */
        graph->nodes[0].attrs.alpha   = 1.0f;
        graph->nodes[0].attrs.beta    = 1.0f;
        graph->nodes[0].attrs.transB  = 1;

        /* node[1]: GEMM(W2, hidden, b2) */
        graph->nodes[1].op_type       = ONNX_OP_GEMM;
        graph->nodes[1].num_inputs    = 3;
        graph->nodes[1].num_outputs   = 1;
        graph->nodes[1].input_idx[0]  = 3; /* hidden */
        graph->nodes[1].input_idx[1]  = 4; /* W2 */
        graph->nodes[1].input_idx[2]  = 5; /* b2 */
        graph->nodes[1].output_idx[0] = 6; /* pre_softmax */
        graph->nodes[1].attrs.alpha   = 1.0f;
        graph->nodes[1].attrs.beta    = 1.0f;
        graph->nodes[1].attrs.transB  = 1;

        /* node[2]: Softmax */
        graph->nodes[2].op_type       = ONNX_OP_SOFTMAX;
        graph->nodes[2].num_inputs    = 1;
        graph->nodes[2].num_outputs   = 1;
        graph->nodes[2].input_idx[0]  = 6; /* pre_softmax */
        graph->nodes[2].output_idx[0] = 7; /* output */

        graph->num_nodes = 3;

        /* Schedule: execute in order 0→1→2 */
        graph->schedule[0]    = 0;
        graph->schedule[1]    = 1;
        graph->schedule[2]    = 2;
        graph->schedule_len   = 3;

        /* Graph I/O */
        graph->input_idx[0]   = 0; /* tensor[0] = input */
        graph->output_idx[0]  = 7; /* tensor[7] = output */
        graph->num_inputs     = 1;
        graph->num_outputs    = 1;

        HAL_UART_PutString("[ONNX] loaded: tiny_mlp (4→8→4, int8)\n");
        return STATUS_OK;
    }

    HAL_UART_PutString("[ONNX] unknown model: ");
    HAL_UART_PutString(model_name);
    HAL_UART_PutString("\n");
    return STATUS_ERROR_INVALID_ARGUMENT;
}
