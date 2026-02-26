/**
 * @file onnx_types.h
 * @brief ONNX data types and structures for MiniOS
 *
 * Defines the core data structures for representing ONNX computation
 * graphs in the kernel. This includes tensors, operators, and graphs.
 */

#ifndef ONNX_TYPES_H
#define ONNX_TYPES_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  ONNX Data Types                                                   */
/* ------------------------------------------------------------------ */

typedef enum {
    ONNX_DTYPE_UNDEFINED = 0,
    ONNX_DTYPE_FLOAT32   = 1,
    ONNX_DTYPE_UINT8     = 2,
    ONNX_DTYPE_INT8      = 3,
    ONNX_DTYPE_UINT16    = 4,
    ONNX_DTYPE_INT16     = 5,
    ONNX_DTYPE_INT32     = 6,
    ONNX_DTYPE_INT64     = 7,
    ONNX_DTYPE_FLOAT64   = 11,
} ONNX_DataType;

/* ------------------------------------------------------------------ */
/*  ONNX Operator Types                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    ONNX_OP_UNDEFINED = 0,
    
    /* Arithmetic Operations */
    ONNX_OP_ADD,
    ONNX_OP_SUB,
    ONNX_OP_MUL,
    ONNX_OP_DIV,
    ONNX_OP_MATMUL,
    
    /* Activation Functions */
    ONNX_OP_RELU,
    ONNX_OP_SIGMOID,
    ONNX_OP_TANH,
    ONNX_OP_SOFTMAX,
    
    /* Convolution & Pooling */
    ONNX_OP_CONV,
    ONNX_OP_MAXPOOL,
    ONNX_OP_AVGPOOL,
    
    /* Shape Operations */
    ONNX_OP_RESHAPE,
    ONNX_OP_TRANSPOSE,
    ONNX_OP_FLATTEN,
    
    /* Normalization */
    ONNX_OP_BATCHNORM,
    
    /* Other */
    ONNX_OP_GEMM,
    ONNX_OP_CONCAT,
    
    ONNX_OP_MAX_VALUE
} ONNX_OperatorType;

/* ------------------------------------------------------------------ */
/*  Tensor Shape Information                                          */
/* ------------------------------------------------------------------ */

#define ONNX_MAX_DIMS 8
#define ONNX_MAX_NAME_LEN 64  /* Maximum length for tensor/node names */

typedef struct {
    uint32_t ndim;                  /* Number of dimensions */
    uint64_t dims[ONNX_MAX_DIMS];   /* Size of each dimension */
    uint64_t total_elements;        /* Product of all dimensions */
} ONNX_TensorShape;

/* ------------------------------------------------------------------ */
/*  Tensor Structure                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[64];                  /* Tensor name */
    ONNX_DataType dtype;            /* Data type */
    ONNX_TensorShape shape;         /* Shape information */
    void* data;                     /* Pointer to actual data */
    uint64_t data_size;             /* Size in bytes */
    bool is_initializer;            /* Is this a constant tensor? */
} ONNX_Tensor;

/* ------------------------------------------------------------------ */
/*  Operator Attributes (for parameterized ops)                       */
/* ------------------------------------------------------------------ */

#define ONNX_MAX_ATTR_INTS 16

typedef struct {
    /* Conv/Pool attributes */
    int64_t kernel_shape[ONNX_MAX_ATTR_INTS];
    uint32_t kernel_shape_len;
    
    int64_t strides[ONNX_MAX_ATTR_INTS];
    uint32_t strides_len;
    
    int64_t pads[ONNX_MAX_ATTR_INTS];
    uint32_t pads_len;
    
    int64_t dilations[ONNX_MAX_ATTR_INTS];
    uint32_t dilations_len;
    
    /* Axis attributes */
    int64_t axis;
    
    /* Group attribute (Conv) */
    int64_t group;
    
    /* Activation attributes */
    float alpha;
    float beta;
    
    /* Other */
    int64_t keepdims;
} ONNX_Attributes;

/* ------------------------------------------------------------------ */
/*  Operator/Node Structure                                           */
/* ------------------------------------------------------------------ */

#define ONNX_MAX_INPUTS  16
#define ONNX_MAX_OUTPUTS 16

typedef struct ONNX_Node_s {
    char name[64];                      /* Node name */
    ONNX_OperatorType op_type;          /* Operator type */
    
    /* Input/Output tensors */
    uint32_t num_inputs;
    ONNX_Tensor* inputs[ONNX_MAX_INPUTS];
    
    uint32_t num_outputs;
    ONNX_Tensor* outputs[ONNX_MAX_OUTPUTS];
    
    /* Operator attributes */
    ONNX_Attributes attributes;
    
    /* Scheduling metadata */
    uint32_t exec_order;                /* Execution order (for scheduling) */
    uint32_t exec_priority;             /* Priority for custom scheduling */
    bool is_scheduled;                  /* Has this node been scheduled? */
    
    /* Dependency tracking */
    uint32_t num_dependencies;          /* Number of nodes that must execute before this */
    struct ONNX_Node_s* dependencies[ONNX_MAX_INPUTS];
    
    /* Performance metadata */
    uint64_t exec_time_us;              /* Execution time in microseconds */
    uint64_t exec_count;                /* Number of times executed */
    
} ONNX_Node;

/* ------------------------------------------------------------------ */
/*  Computation Graph Structure                                       */
/* ------------------------------------------------------------------ */

#define ONNX_MAX_NODES      256
#define ONNX_MAX_TENSORS    512
#define ONNX_MAX_INPUTS     16
#define ONNX_MAX_OUTPUTS    16

typedef struct {
    char name[128];                     /* Graph name */
    uint32_t ir_version;                /* ONNX IR version */
    
    /* Nodes (operators) */
    uint32_t num_nodes;
    ONNX_Node nodes[ONNX_MAX_NODES];
    
    /* Tensors (intermediate values) */
    uint32_t num_tensors;
    ONNX_Tensor tensors[ONNX_MAX_TENSORS];
    
    /* Graph inputs */
    uint32_t num_inputs;
    ONNX_Tensor* inputs[ONNX_MAX_INPUTS];
    
    /* Graph outputs */
    uint32_t num_outputs;
    ONNX_Tensor* outputs[ONNX_MAX_OUTPUTS];
    
    /* Initializers (constant weights/biases) */
    uint32_t num_initializers;
    ONNX_Tensor* initializers[ONNX_MAX_TENSORS];
    
    /* Scheduling information */
    ONNX_Node* exec_schedule[ONNX_MAX_NODES];  /* Ordered list for execution */
    uint32_t schedule_length;
    
    /* Memory pool for tensors */
    void* tensor_memory_pool;
    uint64_t tensor_memory_size;
    uint64_t tensor_memory_used;
    
} ONNX_Graph;

/* ------------------------------------------------------------------ */
/*  Inference Context                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    ONNX_Graph* graph;
    void* workspace;                    /* Temporary workspace for computations */
    uint64_t workspace_size;
    
    /* Execution statistics */
    uint64_t total_inferences;
    uint64_t total_time_us;
    
} ONNX_InferenceContext;

/* ------------------------------------------------------------------ */
/*  Helper Functions                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Get size in bytes for a given data type
 */
static inline uint32_t ONNX_GetDataTypeSize(ONNX_DataType dtype)
{
    switch (dtype) {
        case ONNX_DTYPE_FLOAT32:  return 4;
        case ONNX_DTYPE_FLOAT64:  return 8;
        case ONNX_DTYPE_INT8:     return 1;
        case ONNX_DTYPE_UINT8:    return 1;
        case ONNX_DTYPE_INT16:    return 2;
        case ONNX_DTYPE_UINT16:   return 2;
        case ONNX_DTYPE_INT32:    return 4;
        case ONNX_DTYPE_INT64:    return 8;
        default:                  return 0;
    }
}

/**
 * @brief Calculate total number of elements in a shape
 */
static inline uint64_t ONNX_GetShapeElements(const ONNX_TensorShape* shape)
{
    if (shape->ndim == 0) return 0;
    
    uint64_t elements = 1;
    for (uint32_t i = 0; i < shape->ndim; i++) {
        elements *= shape->dims[i];
    }
    return elements;
}

/**
 * @brief Get the name of an operator type
 */
const char* ONNX_GetOperatorName(ONNX_OperatorType op_type);

/**
 * @brief Get the name of a data type
 */
const char* ONNX_GetDataTypeName(ONNX_DataType dtype);

#endif /* ONNX_TYPES_H */
