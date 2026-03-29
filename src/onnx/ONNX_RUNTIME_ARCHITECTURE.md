# MiniOS Machine Learning Runtime Architecture

## 1. Executive Summary

This document provides a formal architectural overview and detailed implementation guide for the Machine Learning (ML) Inference Runtime within the MiniOS bare-metal unikernel. The primary objective of this runtime is to execute Open Neural Network Exchange (ONNX) computation graphs natively on ARM64 hardware without relying on an external operating system, standard C libraries (`libc`), or dynamic heap allocations during the hot path of inference.

This documentation formalizes the data structures, the dispatcher architecture, and the mathematical implementation of the neural network operators that comprise the runtime engine.

---

## 2. Core Architectural Design

The MiniOS ML Runtime operates on a strictly defined Directed Acyclic Graph (DAG) architecture. The execution pipeline is divided into three distinct phases:

1. **Graph Construction & Parsing**: Translating serialized Protobuf data into statically allocated memory structures.
2. **Topological Scheduling**: Analyzing in-degrees of computation nodes using Kahn's Algorithm to generate a linear execution array (`exec_schedule`).
3. **Inference Dispatch (The Runtime)**: Traversing the pre-computed schedule, mapping input/output tensors, and dispatching computational kernels to the CPU.

By decoupling the topological scheduling from the execution engine, the ML runtime achieves O(1) scheduling overhead during real-time inference. The runtime strictly reads from the `exec_schedule` and applies the designated operator algorithms in sequence.

---

## 3. Fundamental Data Structures

The mathematical and logical state of the neural network is encapsulated in a highly optimized set of C structures. These structures avoid pointers to dynamically sized arrays wherever possible, opting instead for fixed-size boundaries (`ONNX_MAX_DIMS`, `ONNX_MAX_INPUTS`) to ensure deterministic memory footprints.

### 3.1. Tensor Representation

The `ONNX_Tensor` structure models an n-dimensional array. It acts as the fundamental unit of data exchange between computation nodes.

```c
typedef struct {
    uint32_t ndim;                  /* Number of active dimensions */
    uint64_t dims[ONNX_MAX_DIMS];   /* Dimensionality sizes */
    uint64_t total_elements;        /* Pre-computed Cartesian product of dims */
} ONNX_TensorShape;

typedef struct {
    char name[64];                  /* Unique tensor identifier */
    ONNX_DataType dtype;            /* Floating-point or integer type */
    ONNX_TensorShape shape;         /* Geometric boundary data */
    void* data;                     /* Void pointer to the mapped Arena memory */
    uint64_t data_size;             /* Total byte size of the tensor */
    bool is_initializer;            /* True if tensor contains static weights */
} ONNX_Tensor;
```

### 3.2. Computation Nodes

Each operator in the neural network is represented as an `ONNX_Node`. The node maintains arrays of pointers to its dependent input and output tensors.

```c
typedef struct ONNX_Node_s {
    char name[64];                      /* Node identifier */
    ONNX_OperatorType op_type;          /* Enum determining the dispatch path */

    uint32_t num_inputs;
    ONNX_Tensor* inputs[ONNX_MAX_INPUTS];

    uint32_t num_outputs;
    ONNX_Tensor* outputs[ONNX_MAX_OUTPUTS];

    ONNX_Attributes attributes;         /* Hyperparameters (e.g., strides, alpha) */

    uint32_t exec_order;                /* Position in the topological schedule */
    uint64_t exec_time_us;              /* Profiling: microsecond execution time */
    uint64_t exec_count;                /* Profiling: historical call count */
} ONNX_Node;
```

### 3.3. The Inference Context

The `ONNX_InferenceContext` structure acts as the isolated state machine for a given inference request, safely storing telemetry and workspace pointers separate from the statically immutable `ONNX_Graph`.

```c
typedef struct {
    ONNX_Graph* graph;                  /* Pointer to the immutable topological DAG */
    void* workspace;                    /* Temporary scratch space for transformations */
    uint64_t workspace_size;

    uint64_t total_inferences;          /* Lifecycle metric */
    uint64_t total_time_us;             /* Performance metric */
} ONNX_InferenceContext;
```

---

## 4. Runtime Lifecycle and Context Initialization

Before any forward pass can occur, the inference context must be strictly zero-initialized and bound to a valid graph. The initialization sequence ensures that memory pointers are safely aligned and workspace bounds are mapped correctly.

### Implementation: `ONNX_Runtime_Init`

```c
Status ONNX_Runtime_Init(ONNX_InferenceContext* ctx,
                         ONNX_Graph* graph,
                         uint64_t workspace_size)
{
    if (!ctx || !graph) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* Enforce a clean state utilizing local memory zeroing */
    mem_zero(ctx, sizeof(ONNX_InferenceContext));

    ctx->graph = graph;
    ctx->workspace_size = workspace_size;

    /* Allocate volatile workspace from the kernel heap if requested */
    if (workspace_size > 0) {
        ctx->workspace = KMEM_Alloc(workspace_size, KMEM_MIN_ALIGN);
    }

    ctx->total_inferences = 0;
    ctx->total_time_us = 0;

    return STATUS_OK;
}
```

---

## 5. The Inference Execution Engine

The core routing mechanism of the ML runtime is governed by two functions: `ONNX_Runtime_Inference` (the pipeline manager) and `ONNX_Runtime_ExecuteNode` (the operator dispatcher).

### 5.1. Pipeline Manager: `ONNX_Runtime_Inference`

This function is responsible for safely transferring user-space input buffers into the graph's pre-allocated input tensors, sequentially firing the nodes defined by the topological schedule, and extracting the resultant tensors back to the user.

```c
Status ONNX_Runtime_Inference(ONNX_InferenceContext* ctx,
                              ONNX_Tensor** inputs,
                              uint32_t num_inputs,
                              ONNX_Tensor** outputs,
                              uint32_t num_outputs)
{
    ONNX_Graph* graph = ctx->graph;

    /* Step 1: Input Tensor Validation and Ingestion */
    for (uint32_t i = 0; i < num_inputs; i++) {
        ONNX_Tensor* graph_input = graph->inputs[i];
        ONNX_Tensor* user_input = inputs[i];

        if (graph_input->data_size != user_input->data_size) {
            return STATUS_ERROR_SHAPE_MISMATCH;
        }
        mem_copy(graph_input->data, user_input->data, user_input->data_size);
    }

    /* Step 2: Sequential Schedule Execution */
    for (uint32_t i = 0; i < graph->schedule_length; i++) {
        ONNX_Node* node = graph->exec_schedule[i];

        Status status = ONNX_Runtime_ExecuteNode(ctx, node);
        if (status != STATUS_OK) {
            return status;
        }
    }

    /* Step 3: Output Tensor Extraction */
    for (uint32_t i = 0; i < num_outputs; i++) {
        outputs[i] = graph->outputs[i];
    }

    ctx->total_inferences++;
    return STATUS_OK;
}
```

### 5.2. Operator Dispatcher: `ONNX_Runtime_ExecuteNode`

The dispatcher uses a highly optimized C `switch` block, routing execution to the designated numerical kernel based on the `op_type` enum. This avoids v-table lookups and virtual function overhead, adhering to bare-metal performance constraints.

```c
Status ONNX_Runtime_ExecuteNode(ONNX_InferenceContext* ctx, ONNX_Node* node)
{
    Status status = STATUS_OK;

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

        case ONNX_OP_SOFTMAX:
            status = ONNX_Execute_Softmax(node, ctx);
            break;

        case ONNX_OP_CONV:
            status = ONNX_Execute_Conv(node, ctx);
            break;

        /* ... additional routing ... */

        default:
            return STATUS_ERROR_UNSUPPORTED_OPERATOR;
    }

    if (status == STATUS_OK) {
        node->exec_count++;
    }
    return status;
}
```

---

## 6. Formalized Operator Implementations

The physical realization of mathematical models on a bare-metal architecture requires robust naive implementations, occasionally bypassing standard assumptions (such as the presence of `math.h`).

### 6.1. Linear Algebra: Matrix Multiplication (`MatMul`)

Matrix multiplication ($C = AB$) is the cornerstone of dense neural networks. Given input $A \in \mathbb{R}^{M \times K}$ and input $B \in \mathbb{R}^{K \times N}$, the current implementation leverages a naive $O(M \times N \times K)$ nested loop.

#### Implementation: `ONNX_Execute_MatMul`
```c
Status ONNX_Execute_MatMul(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    ONNX_Tensor* a = node->inputs[0];
    ONNX_Tensor* b = node->inputs[1];
    ONNX_Tensor* out = node->outputs[0];

    uint32_t M = a->shape.dims[0];
    uint32_t K = a->shape.dims[1];
    uint32_t N = b->shape.dims[1];

    float* a_data = (float*)a->data;
    float* b_data = (float*)b->data;
    float* out_data = (float*)out->data;

    /* Non-vectorized scalar dot-products */
    for (uint32_t i = 0; i < M; i++) {
        for (uint32_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < K; k++) {
                sum += a_data[i * K + k] * b_data[k * N + j];
            }
            out_data[i * N + j] = sum;
        }
    }

    return STATUS_OK;
}
```

### 6.2. Spatial Operations: 2D Convolution (`Conv`)

The convolution operator extracts localized spatial features. The mathematical representation for output element $Y_{o, h, w}$ without padding or striding is:
$$ Y_{o, h, w} = B_o + \sum_{c=0}^{C_{in}-1} \sum_{i=0}^{K_h-1} \sum_{j=0}^{K_w-1} X_{c, h+i, w+j} \cdot W_{o, c, i, j} $$

#### Implementation: `ONNX_Execute_Conv`
```c
Status ONNX_Execute_Conv(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    ONNX_Tensor* x = node->inputs[0];
    ONNX_Tensor* w = node->inputs[1];
    ONNX_Tensor* b = (node->num_inputs > 2) ? node->inputs[2] : NULL;
    ONNX_Tensor* y = node->outputs[0];

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

    for (uint32_t oc = 0; oc < c_out; oc++) {
        for (uint32_t oh = 0; oh < h_out; oh++) {
            for (uint32_t ow = 0; ow < w_out; ow++) {
                float sum = b_data ? b_data[oc] : 0.0f;

                for (uint32_t ic = 0; ic < c_in; ic++) {
                    for (uint32_t kh = 0; kh < k_h; kh++) {
                        for (uint32_t kw = 0; kw < k_w; kw++) {
                            uint32_t ih = oh + kh;
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
```

### 6.3. Bare-Metal Approximations: Exponential and Softmax

Because standard libc `<math.h>` is unavailable in the MiniOS unikernel, algorithms requiring transcendental functions must rely on hardware instructions or mathematical expansions.

The exponential function $e^x$ is approximated utilizing a computationally bounded Taylor Series:
$$ e^x \approx 1 + x + \frac{x^2}{2!} + \frac{x^3}{3!} + \dots + \frac{x^{10}}{10!} $$

#### Implementation: `fast_exp`
```c
static float fast_exp(float x)
{
    if (x < -10.0f) return 0.0f;
    if (x > 10.0f) return 22026.46579f; /* Pre-computed convergence bound */

    float sum = 1.0f;
    float term = 1.0f;
    for (int i = 1; i < 10; i++) {
        term = term * x / i;
        sum += term;
    }
    return sum;
}
```

This approximation heavily enables non-linearities such as `Softmax`. To prevent floating-point overflow during execution (since $e^{x}$ explodes rapidly), we employ numerical stabilization by shifting the inputs relative to the sequence maximum:
$$ Softmax(x_i) = \frac{e^{x_i - \max(x)}}{\sum_j e^{x_j - \max(x)}} $$

#### Implementation: `ONNX_Execute_Softmax`
```c
Status ONNX_Execute_Softmax(ONNX_Node* node, ONNX_InferenceContext* ctx)
{
    ONNX_Tensor* in = node->inputs[0];
    ONNX_Tensor* out = node->outputs[0];

    uint64_t n = in->shape.total_elements;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    /* Phase 1: Establish scalar stability baseline */
    float max_val = in_data[0];
    for (uint64_t i = 1; i < n; i++) {
        if (in_data[i] > max_val) max_val = in_data[i];
    }

    /* Phase 2: Compute bounded exponentials and geometric sum */
    float sum = 0.0f;
    for (uint64_t i = 0; i < n; i++) {
        out_data[i] = fast_exp(in_data[i] - max_val);
        sum += out_data[i];
    }

    /* Phase 3: Final normalization array walk */
    if (sum != 0.0f) {
        for (uint64_t i = 0; i < n; i++) {
            out_data[i] /= sum;
        }
    }

    return STATUS_OK;
}
```

---

## 7. Memory Layout & Constraints

The ML Runtime avoids runtime memory fragmentation entirely.

- **Pre-allocation Constraints**: Upon loading a model via `onnx_loader.c`, the structural sizes are pre-calculated.
- **Kernel Memory Arena**: The graph negotiates with the `KMEM_Arena` interface (implemented inside `kernel/kmem.c`) to acquire a contiguous bump-allocated block of memory.
- **Pointer Assignments**: Each tensor natively maintains a pointer into this contiguous slab. Shape transformations (e.g., `Reshape`, `Flatten`) typically execute as $O(1)$ pointer duplications or $O(N)$ contiguous memory copies via `mem_copy`.

By adhering to this rigid initialization pattern, the execution engine runs fully deterministically—guaranteeing that inference latency cannot suffer from unexpected OS context switches, paging faults, or garbage collection.

## 8. Summary of Extensibility

The ML runtime is strictly bounded by `switch` dispatching but is architected to be highly extensible. Future engineers need only to complete three steps to introduce new functionality:

1. **Extend `ONNX_OperatorType`**: Add the new operator enum (e.g., `ONNX_OP_LSTM`) into `include/onnx/onnx_types.h`. This guarantees the data structures understand the new node constraint.
2. **String Translation Map**: Insert the string matching condition inside the parser in `onnx_loader.c` to bind the ONNX textual definition (e.g., "LSTM") to the C enum generated in step 1.
3. **Dispatch Path**: Append the corresponding case inside the core executor `ONNX_Runtime_ExecuteNode` and write a custom $O(N)$ transformation kernel matching the inputs.

If these constraints are followed, adding advanced transformer operators (e.g., `ScaledDotProductAttention`, `LayerNorm`) or sequence models (e.g., `GRU`) scales linearly with algorithmic complexity, rather than framework integration complexity.

This formally concludes the architecture specification for the MiniOS ONNX ML Runtime implementation.

---
**End of Document**