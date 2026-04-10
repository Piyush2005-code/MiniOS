/**
 * @file onnx_types_tiny.h
 * @brief Miniaturized ONNX data structures for MiniOS-ESP8266
 *
 * The original ONNX_Graph struct is ~3 MB (2048 nodes × 4096 tensors).
 * This redesign fits in ~2 KB — a 1,500× reduction — making it viable
 * within the ESP8266's 80 KB dRAM constraint.
 *
 * Key changes from original onnx_types.h:
 *   - Max nodes:    2048 → 16
 *   - Max tensors:  4096 → 32
 *   - Max dims:     8    → 4
 *   - dim type:     uint64_t → uint16_t
 *   - Node uses tensor indices (uint8_t) not pointers
 *   - Removed scheduling metadata, dependency tracking, per-node profiling
 *   - Attributes reduced to only what tiny MLPs need
 */

#ifndef MINIOS_ESP8266_ONNX_TYPES_TINY_H
#define MINIOS_ESP8266_ONNX_TYPES_TINY_H

#include "types.h"
#include "status.h"
#include "../../user_config.h"

/* ------------------------------------------------------------------ */
/*  Capacity limits (fit within ESP8266 RAM)                         */
/* ------------------------------------------------------------------ */

#ifndef ONNX_TINY_MAX_NODES
#  define ONNX_TINY_MAX_NODES    16
#endif

#ifndef ONNX_TINY_MAX_TENSORS
#  define ONNX_TINY_MAX_TENSORS  32
#endif

#define ONNX_TINY_MAX_DIMS       4
#define ONNX_TINY_MAX_IO         4     /* max graph inputs/outputs */
#define ONNX_TINY_MAX_NODE_IO    4     /* max inputs/outputs per node */
#define ONNX_TINY_MAX_NAME       16    /* tensor/node name length */
#define ONNX_TINY_INVALID_IDX    0xFF  /* sentinel for unused slots */

/* ------------------------------------------------------------------ */
/*  Data Types                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    ONNX_DTYPE_UNDEFINED = 0,
    ONNX_DTYPE_FLOAT32   = 1,
    ONNX_DTYPE_UINT8     = 2,
    ONNX_DTYPE_INT8      = 3,
    ONNX_DTYPE_INT32     = 6,
} ONNX_DataType;

/* ------------------------------------------------------------------ */
/*  Operator Types (only those implemented in onnx_runtime_tiny.c)   */
/* ------------------------------------------------------------------ */

typedef enum {
    ONNX_OP_UNDEFINED  = 0,
    ONNX_OP_ADD        = 1,
    ONNX_OP_SUB        = 2,
    ONNX_OP_MUL        = 3,
    ONNX_OP_MATMUL     = 4,
    ONNX_OP_RELU       = 5,
    ONNX_OP_SIGMOID    = 6,
    ONNX_OP_SOFTMAX    = 7,
    ONNX_OP_GEMM       = 8,   /* General Matrix Multiply */
    ONNX_OP_RESHAPE    = 9,
    ONNX_OP_FLATTEN    = 10,
    ONNX_OP_IDENTITY   = 11,
    ONNX_OP_MAX_VALUE
} ONNX_OperatorType;

/* ------------------------------------------------------------------ */
/*  Tensor Shape — 12 bytes (was 76 bytes)                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  ndim;
    uint16_t dims[ONNX_TINY_MAX_DIMS];  /* uint16_t: max dim = 65535 */
    uint16_t total_elements;
} ONNX_TensorShape;  /* ~12 bytes */

/* ------------------------------------------------------------------ */
/*  Tensor — 36 bytes (was 161 bytes)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char           name[ONNX_TINY_MAX_NAME];
    ONNX_DataType  dtype;          /* uint8_t effectively */
    ONNX_TensorShape shape;
    void          *data;           /* pointer into data buffer */
    uint16_t       data_size;      /* bytes */
    uint8_t        is_initializer; /* 1 if constant weight/bias */
} ONNX_Tensor;  /* ~36 bytes */

/* ------------------------------------------------------------------ */
/*  Minimal Node Attributes (only add/alpha for supported ops)        */
/* ------------------------------------------------------------------ */

typedef struct {
    float   alpha;    /* LeakyReLU slope, GEMM alpha */
    float   beta;     /* GEMM beta */
    int8_t  axis;     /* Softmax axis, Concat axis */
    uint8_t transA;   /* GEMM: transpose A */
    uint8_t transB;   /* GEMM: transpose B */
} ONNX_TinyAttributes;  /* 12 bytes */

/* ------------------------------------------------------------------ */
/*  Node — 44 bytes (was 1182 bytes)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char                name[ONNX_TINY_MAX_NAME];
    ONNX_OperatorType   op_type;
    uint8_t             num_inputs;
    uint8_t             num_outputs;
    /* Tensor indices into ONNX_Graph.tensors[] — not pointers */
    uint8_t             input_idx[ONNX_TINY_MAX_NODE_IO];
    uint8_t             output_idx[ONNX_TINY_MAX_NODE_IO];
    ONNX_TinyAttributes attrs;
} ONNX_Node;  /* ~44 bytes */

/* ------------------------------------------------------------------ */
/*  Graph — ~1868 bytes (was 3,000,000 bytes)                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t      num_nodes;
    uint8_t      num_tensors;
    uint8_t      num_inputs;
    uint8_t      num_outputs;

    ONNX_Node    nodes[ONNX_TINY_MAX_NODES];      /* 16 × 44 = 704 bytes */
    ONNX_Tensor  tensors[ONNX_TINY_MAX_TENSORS];  /* 32 × 36 = 1152 bytes */

    /* Indices of graph-level I/O tensors */
    uint8_t      input_idx[ONNX_TINY_MAX_IO];
    uint8_t      output_idx[ONNX_TINY_MAX_IO];

    /* Topological execution order (indices into nodes[]) */
    uint8_t      schedule[ONNX_TINY_MAX_NODES];
    uint8_t      schedule_len;

    /* Pointer to external data buffer (model weights + activations) */
    uint8_t     *data_buf;
    uint16_t     data_buf_size;
    uint16_t     data_buf_used;
} ONNX_Graph;  /* ~1,868 bytes */

/* ------------------------------------------------------------------ */
/*  Inference Context                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    ONNX_Graph *graph;
    uint32_t    total_inferences;
    uint32_t    total_time_us;
} ONNX_InferenceContext;  /* 12 bytes */

/* ------------------------------------------------------------------ */
/*  Helper inlines                                                    */
/* ------------------------------------------------------------------ */

static inline uint8_t ONNX_GetDataTypeSize(ONNX_DataType dtype)
{
    switch (dtype) {
        case ONNX_DTYPE_FLOAT32: return 4;
        case ONNX_DTYPE_INT8:    return 1;
        case ONNX_DTYPE_UINT8:   return 1;
        case ONNX_DTYPE_INT32:   return 4;
        default:                 return 0;
    }
}

static inline uint16_t ONNX_GetShapeElements(const ONNX_TensorShape *s)
{
    if (s->ndim == 0) return 0;
    uint16_t n = 1;
    for (uint8_t i = 0; i < s->ndim; i++) n = (uint16_t)(n * s->dims[i]);
    return n;
}

const char *ONNX_GetOperatorName(ONNX_OperatorType op);

#endif /* MINIOS_ESP8266_ONNX_TYPES_TINY_H */
