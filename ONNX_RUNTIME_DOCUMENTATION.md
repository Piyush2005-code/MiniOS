# ONNX Runtime & Parser: A Comprehensive Guide

> An in-depth exploration of the MiniOS ONNX inference engine: from protobuf parsing to bare-metal operator execution

**Version:** 2.0
**Status:** Active Development — 9 models embedded, AlexNet-style LRN + GEMM transB supported
**Last Updated:** April 2026

---

## Table of Contents

1. [Executive Overview](#executive-overview)
2. [Architecture & Design Principles](#architecture--design-principles)
3. [Core Data Structures](#core-data-structures)
4. [Model Loading Pipeline](#model-loading-pipeline)
5. [Protobuf Parser Implementation](#protobuf-parser-implementation)
6. [Graph Construction & Memory Management](#graph-construction--memory-management)
7. [Shape Propagation](#shape-propagation)
8. [Scheduling & Dependency Analysis](#scheduling--dependency-analysis)
9. [Runtime Execution Engine](#runtime-execution-engine)
10. [Operator Implementation Deep Dive](#operator-implementation-deep-dive)
11. [Embedded Model Catalogue](#embedded-model-catalogue)
12. [Shell Commands Reference](#shell-commands-reference)
13. [Performance & Profiling](#performance--profiling)
14. [Known Limitations & Roadmap](#known-limitations--roadmap)
15. [Integration & Usage Examples](#integration--usage-examples)
16. [FAQ & Troubleshooting](#faq--troubleshooting)

---

## Executive Overview

The MiniOS ONNX Runtime is a **zero-dependency, bare-metal machine learning inference engine** built for
ARM64 microcontrollers and embedded systems (QEMU `virt` / Cortex-A53). It loads and executes ONNX
(Open Neural Network Exchange) models directly on hardware without an OS, standard C library, or external ML
frameworks.

### Key Capabilities (v2.0)

- ✅ **Custom Protobuf Parser** — hand-rolled, no external library required
- ✅ **42 Operators Implemented** — covers all operators needed by MLP, CNN, ResNet, AlexNet, VGG architectures
- ✅ **LRN Operator** — Local Response Normalization added for AlexNet-class models
- ✅ **GEMM transB** — auto-detected from weight tensor shape; FC layers with transposed weights work correctly
- ✅ **Correct Batch Dimension** — Conv2D and MaxPool properly index over batch N
- ✅ **Full Shape Propagation** — Conv, MaxPool, AvgPool, GEMM, Flatten, LRN, GlobalAveragePool, unary & binary ops
- ✅ **Arena-Backed Memory** — 128 MB tensor arena; zero runtime fragmentation
- ✅ **9 Models Embedded in InitFS** — available in the kernel binary from first boot, no flash setup needed
- ✅ **Per-Node Execution Profiling** — microsecond-accurate timing via hardware timer

### Current Limits

| Resource             | Limit         | Notes                                       |
|----------------------|---------------|---------------------------------------------|
| Model file buffer    | **8 MB**      | Raise `MAX_MODEL_SIZE` in `onnx_cmds.c`    |
| Tensor arena         | **128 MB**    | Raise `MAX_ARENA_SIZE` in `onnx_cmds.c`    |
| Nodes per graph      | **256**       | `ONNX_MAX_NODES` in `onnx_types.h`         |
| Tensors per graph    | **512**       | `ONNX_MAX_TENSORS` in `onnx_types.h`       |
| Max tensor name len  | **64 chars**  | `ONNX_MAX_NAME_LEN` in `onnx_types.h`      |
| Embed in initfs      | **≤ 4 MB**    | `MAX_EMBED_BYTES` in `embed_storage.py`    |
| Supported data type  | **float32**   | int8 quantization not yet implemented       |

---

## Architecture & Design Principles

### Three-Phase Execution Model

```
Phase 1: Load
  ULFS_Read(file) → protobuf buffer
         ↓
  ONNX_LoadProtobuf() → ONNX_Graph populated
         ↓
  Initializer tensors copied from buffer → arena

Phase 2: Prepare
  ONNX_Graph_BuildDependencies()  — who produces each input?
         ↓
  Shape propagation pass          — compute output tensor sizes
         ↓
  ONNX_Graph_GenerateSchedule()   — Kahn's topological sort
         ↓
  Tensor arena allocations (intermediate buffers)

Phase 3: Infer
  ONNX_Runtime_Inference()
         ↓
  For each node in exec_schedule[]:
    ONNX_Runtime_ExecuteNode() → operator kernel
         ↓
  Output tensors populated
```

This clean separation ensures:

- **Deterministic latency** — no dynamic allocation during inference hot path
- **One-time scheduling cost** — topology solved once, amortised over all invocations
- **Real-time suitability** — predictable per-inference timing

### Memory Layout

```
┌─────────────────────────────────────────────────────┐
│  Kernel BSS                                         │
│  ├─ g_model_buffer[8 MB]   — raw ONNX protobuf     │
│  ├─ g_graph                — ONNX_Graph metadata    │
│  └─ g_tensor_arena*        — pointer to arena       │
│                                                     │
│  Kernel Heap (KMEM Arena, 128 MB)                   │
│  ├─ Initializer data (model weights, copied once)   │
│  └─ Intermediate activation tensors                 │
│                                                     │
│  InitFS (embedded in .text)                         │
│  └─ 9 ONNX model files (≤4 MB each)                │
└─────────────────────────────────────────────────────┘
```

---

## Core Data Structures

### ONNX_Tensor

```c
typedef struct {
    char name[64];           // Unique identifier (from ONNX graph)
    ONNX_DataType dtype;     // ONNX_DTYPE_FLOAT32, INT32, INT64 …
    ONNX_TensorShape shape;  // {ndim, dims[8], total_elements}
    void* data;              // Arena pointer — NULL until allocated
    uint64_t data_size;      // Bytes = total_elements * sizeof(dtype)
    bool is_initializer;     // True → weight/bias constant
} ONNX_Tensor;
```

**Shape encoding** follows ONNX convention — NCHW for 4-D image tensors:

```
dims[0] = N  (batch)
dims[1] = C  (channels)
dims[2] = H  (height)
dims[3] = W  (width)
total_elements = N * C * H * W
data_size      = total_elements * 4   (for float32)
```

### ONNX_Attributes

Operator hyperparameters parsed from protobuf and stored inline per-node:

```c
typedef struct {
    int64_t  kernel_shape[16]; uint32_t kernel_shape_len;
    int64_t  strides[16];      uint32_t strides_len;
    int64_t  pads[16];         uint32_t pads_len;
    int64_t  dilations[16];    uint32_t dilations_len;
    int64_t  axis;             // Flatten axis, LRN size, Softmax axis …
    int64_t  group;            // Conv group (depthwise = group == C_in)
    float    alpha;            // GEMM alpha, LeakyReLU slope, LRN alpha
    float    beta;             // GEMM beta, LRN beta
    int64_t  keepdims;
} ONNX_Attributes;
```

### ONNX_Node

```c
typedef struct ONNX_Node_s {
    char              name[64];
    ONNX_OperatorType op_type;

    uint32_t  num_inputs;
    ONNX_Tensor* inputs[16];

    uint32_t  num_outputs;
    ONNX_Tensor* outputs[16];

    ONNX_Attributes attributes;

    // Scheduling
    uint32_t exec_order;
    uint32_t num_dependencies;
    struct ONNX_Node_s* dependencies[16];
    bool is_scheduled;

    // Profiling
    uint64_t exec_time_us;
    uint64_t exec_count;
} ONNX_Node;
```

### ONNX_Graph

```c
typedef struct {
    char     name[128];
    uint32_t ir_version;

    uint32_t  num_nodes;
    ONNX_Node nodes[256];          // Static array, no heap needed

    uint32_t    num_tensors;
    ONNX_Tensor tensors[512];      // Metadata for every tensor

    uint32_t     num_inputs;
    ONNX_Tensor* inputs[16];       // Graph-level inputs (user provides data)

    uint32_t     num_outputs;
    ONNX_Tensor* outputs[16];      // Graph-level outputs

    uint32_t     num_initializers;
    ONNX_Tensor* initializers[512];

    ONNX_Node*   exec_schedule[256];
    uint32_t     schedule_length;

    kmem_arena_t* tensor_arena;    // 128 MB arena for activation buffers
    uint64_t      tensor_memory_used;
} ONNX_Graph;
```

---

## Model Loading Pipeline

### From File to Executable Graph

```
onnx_run <filename>
    │
    ├─ ULFS_Stat()   — check file exists and size ≤ 8 MB
    ├─ ULFS_Open()   — open file descriptor
    ├─ ULFS_Read()   — read protobuf bytes into g_model_buffer
    │
    ├─ KMEM_ArenaCreate(128 MB)  — create fresh tensor arena
    ├─ ONNX_Graph_Init()         — zero-initialise g_graph
    │
    ├─ ONNX_LoadProtobuf()       — parse binary → graph structure
    │     ├─ ModelProto (field 7 → graph)
    │     ├─ GraphProto (field 5 → nodes, field 8 → initializers)
    │     ├─ NodeProto  (op_type, inputs, outputs, attributes)
    │     └─ TensorProto (dims, raw_data → arena copy)
    │
    ├─ Shape propagation pass    — compute all output tensor shapes
    ├─ ONNX_Graph_BuildDependencies()
    ├─ ONNX_Graph_GenerateSchedule()
    │
    ├─ Allocate intermediate tensors into arena
    ├─ Allocate dummy input  (all-zeros)
    │
    └─ ONNX_Runtime_Inference()
```

### Creating Compatible Models

Use `scripts/generate_models.py` or `scripts/export_alexnet_tiny.py`:

```python
import onnx
from onnx import helper, TensorProto, numpy_helper
import numpy as np

# Weight initializer
W = numpy_helper.from_array(
    np.random.randn(10, 64).astype(np.float32),
    name="fc_W"
)

# GEMM node — use transB=1 so weights are stored [out, in]
gemm = helper.make_node(
    "Gemm",
    inputs=["x", "fc_W", "fc_b"],
    outputs=["y"],
    transB=1, alpha=1.0, beta=1.0
)

graph = helper.make_graph(
    [gemm],
    "my_model",
    inputs=[helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 64])],
    outputs=[helper.make_tensor_value_info("y", TensorProto.FLOAT, [1, 10])],
    initializer=[W, b],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
model.ir_version = 8
```

Copy the `.onnx` file to `src/storage/` (must be ≤ 4 MB to be auto-embedded), then rebuild:

```bash
make clean && make all
```

---

## Protobuf Parser Implementation

### Wire Format Overview

ONNX models are serialized as Protocol Buffers. Each field is encoded as:

```
[tag varint] [value]

tag = (field_number << 3) | wire_type

Wire types:
  0 = VARINT            (integers, booleans, enums)
  1 = FIXED64           (float64, int64 — fixed 8 bytes)
  2 = LENGTH_DELIMITED  (strings, bytes, nested messages)
  5 = FIXED32           (float32, int32 — fixed 4 bytes)
```

### Varint Decoding

```c
static uint64_t proto_read_varint(ProtoReader* r) {
    uint64_t result = 0;
    uint32_t shift  = 0;
    while (r->pos < r->size) {
        uint8_t b = r->data[r->pos++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;   // no continuation bit → done
        shift += 7;
    }
    return result;
}
```

### Operator String → Enum Mapping

The parser maps ONNX op-type strings to internal enum values using first-character branching
for O(1) lookup without `strcmp`:

```c
// Examples from proto_parse_node():
if      (op[0]=='C' && op[1]=='o')  op_enum = ONNX_OP_CONV;
else if (op[0]=='M' && op[1]=='a')  op_enum = ONNX_OP_MAXPOOL;
else if (op[0]=='G' && op[1]=='e')  op_enum = ONNX_OP_GEMM;
else if (op[0]=='L' && op[1]=='R')  op_enum = ONNX_OP_LRN;       // Added v2.0
else if (op[0]=='B' && op[1]=='a')  op_enum = ONNX_OP_BATCHNORM;
// … 42 operators total
```

### Attribute Parsing

Key operator attributes are extracted from `AttributeProto` messages:

| Attribute Name    | Stored In                        | Used By                 |
|-------------------|----------------------------------|-------------------------|
| `kernel_shape`    | `attributes.kernel_shape[]`      | Conv, MaxPool           |
| `strides`         | `attributes.strides[]`           | Conv, MaxPool           |
| `pads`            | `attributes.pads[]`              | Conv, MaxPool           |
| `dilations`       | `attributes.dilations[]`         | Conv                    |
| `group`           | `attributes.group`               | Conv (depthwise)        |
| `axis`            | `attributes.axis`                | Flatten, Softmax, LRN   |
| `alpha`           | `attributes.alpha`               | GEMM, LeakyReLU, LRN   |
| `beta`            | `attributes.beta`                | GEMM, LRN               |

---

## Graph Construction & Memory Management

### Tensor Arena

All activation tensors are allocated from a single contiguous 128 MB arena:

```c
// Allocation (O(1) pointer bump)
void* ptr = KMEM_ArenaAlloc(arena, size, KMEM_TENSOR_ALIGN);
//   KMEM_TENSOR_ALIGN = 64 bytes (cache-line aligned)

// Query
uint64_t used = KMEM_ArenaGetUsed(arena);   // bytes in use
```

**Arena properties:**
- No free-list traversal — O(1) allocation
- No fragmentation — bump-pointer only
- Cache-line aligned — NEON-friendly accesses
- Persistent for the model lifetime — freed only when `onnx_run` returns

### Initializer Handling

Weights and biases from the ONNX protobuf `raw_data` field are copied into the arena at load time.
The protobuf buffer can then be reused for the next model.

---

## Shape Propagation

One of the key improvements in v2.0 is a **pre-inference shape propagation pass** that computes
output tensor sizes for every operator before any allocation occurs.
This eliminates the "1-element placeholder" bug that caused wrong memory allocation for Conv and GEMM nodes.

### Propagation Rules

| Operator                | Output Shape Formula |
|-------------------------|----------------------|
| **Conv2D**              | `[N, C_out, (H + 2P - K) / S + 1, (W + 2P - K) / S + 1]` |
| **MaxPool / AvgPool**   | `[N, C, (H + 2P - K) / S + 1, (W + 2P - K) / S + 1]` |
| **GlobalAveragePool**   | `[N, C, 1, 1]` |
| **GEMM** (transB=0)     | `[M, N]` where B is `[K, N]` |
| **GEMM** (transB=1)     | `[M, N]` where B is `[N, K]` — auto-detected from B.dims[0] vs K |
| **Flatten** (axis=1)    | `[dims[0..axis-1], dims[axis..end]]` |
| **LRN**                 | Same as input |
| **Unary ops** (Relu …)  | Same as input |
| **Binary ops** (Add …)  | Max of two input shapes |

The propagation loop runs in topological order (schedule order) so each op sees already-propagated
input shapes before it computes its own output shape.

---

## Scheduling & Dependency Analysis

### Kahn's Algorithm (Topological Sort)

```c
// 1. Compute in-degree for every node
for (i = 0; i < graph->num_nodes; i++)
    in_degree[i] = graph->nodes[i].num_dependencies;

// 2. Repeatedly pick a node with in_degree == 0
for (iter = 0; iter < graph->num_nodes; iter++) {
    find node_idx where !visited[node_idx] && in_degree[node_idx] == 0;
    schedule[length++] = &nodes[node_idx];
    visited[node_idx]  = true;

    // Decrement in_degree of every node that depends on node_idx
    for (i = 0; i < graph->num_nodes; i++) {
        if (nodes[i] depends on nodes[node_idx])
            in_degree[i]--;
    }
}
```

If the loop exits before scheduling all nodes, a cycle was detected → `STATUS_ERROR_INVALID_GRAPH`.

### Dependency Analysis

For each node N, `ONNX_Graph_BuildDependencies()` scans every other node P. If P produces a tensor
that N consumes (non-initializer), P is added to `N->dependencies[]`.

---

## Runtime Execution Engine

### Inference Loop

```c
Status ONNX_Runtime_Inference(ctx, inputs[], num_inputs, outputs[], num_outputs)
{
    // 1. Copy user input data into graph input tensors
    for i in 0..num_inputs:
        mem_copy(graph->inputs[i]->data, inputs[i]->data, size)

    // 2. Execute schedule
    for i in 0..schedule_length:
        node = exec_schedule[i]

        // Lazy allocate output tensors whose shapes were just propagated
        for each output of node:
            if output->data == NULL && output->data_size > 0:
                ONNX_Graph_AllocateTensor(graph, output)

        status = ONNX_Runtime_ExecuteNode(ctx, node)
        if status != STATUS_OK: return status

        THREAD_Yield()   // cooperative multitasking

    // 3. Return output pointers
    for i in 0..num_outputs:
        outputs[i] = graph->outputs[i]
}
```

### Node Dispatch

```c
Status ONNX_Runtime_ExecuteNode(ctx, node) {
    uint64_t t0 = HAL_Timer_GetUs();

    switch (node->op_type) {
        case ONNX_OP_CONV:    return ONNX_Execute_Conv(node, ctx);
        case ONNX_OP_MAXPOOL: return ONNX_Execute_MaxPool(node, ctx);
        case ONNX_OP_GEMM:    return ONNX_Execute_GEMM(node, ctx);
        case ONNX_OP_LRN:     return ONNX_Execute_LRN(node, ctx);   // v2.0
        case ONNX_OP_RELU:    return ONNX_Execute_Relu(node, ctx);
        // … 42 cases total
        default:
            print("Unsupported operator: ", name);
            return STATUS_ERROR_UNSUPPORTED_OPERATOR;
    }

    node->exec_time_us += HAL_Timer_GetElapsedUs(t0);
    node->exec_count++;
}
```

---

## Operator Implementation Deep Dive

### Supported Operators (v2.0)

| Category         | Operators                                              | Notes |
|------------------|--------------------------------------------------------|-------|
| **Arithmetic**   | Add, Sub, Mul, Div, MatMul                             | Element-wise + matmul |
| **Activations**  | Relu, LeakyRelu, Sigmoid, Tanh, Softmax               | |
| **Convolution**  | Conv (2-D, NCHW, grouped)                             | Batch loop fixed v2.0 |
| **Pooling**      | MaxPool, AveragePool, GlobalAveragePool               | Batch loop fixed v2.0 |
| **Normalization**| BatchNormalization, **LRN** *(new v2.0)*              | LRN: β=0.75 fast-path |
| **Linear**       | Gemm (transA=0, **transB auto**, alpha, beta)         | transB fixed v2.0 |
| **Shape**        | Flatten, Reshape, Transpose, Squeeze, Unsqueeze       | |
| **Reduction**    | ReduceSum, ReduceMean, ReduceMax, ReduceMin           | |
| **Math**         | Abs, Neg, Exp, Log, Sqrt, Ceil, Floor, Sin, Cos       | |
| **Utility**      | Clip, Identity, Concat, Cast                          | |

### Conv2D — Corrected Implementation (v2.0)

Previous versions iterated over `[C_out, H, W]` without the outer batch dimension `N`.
v2.0 adds the correct outer `nb` loop:

```c
uint32_t batch_n = (uint32_t)x->shape.dims[0];
for (uint32_t nb = 0; nb < batch_n; nb++) {
  for (uint32_t oc = 0; oc < c_out; oc++) {
    for (uint32_t oh = 0; oh < h_out; oh++) {
      for (uint32_t ow = 0; ow < w_out; ow++) {
        float sum = b_data ? b_data[oc] : 0.0f;
        for (uint32_t ic = 0; ic < c_in; ic++) {
          for (uint32_t kh = 0; kh < k_h; kh++) {
            for (uint32_t kw = 0; kw < k_w; kw++) {
              int32_t ih = (int32_t)(oh*stride_h + kh) - (int32_t)pad_h;
              int32_t iw = (int32_t)(ow*stride_w + kw) - (int32_t)pad_w;
              if (ih >= 0 && ih < h_in && iw >= 0 && iw < w_in) {
                sum += x_data[nb*C_in*H*W + ic*H*W + ih*W + iw]
                     * w_data[oc*C_in*kH*kW + ic*kH*kW + kh*kW + kw];
              }
            }
          }
        }
        y_data[nb*C_out*H_out*W_out + oc*H_out*W_out + oh*W_out + ow] = sum;
      }
    }
  }
}
```

### GEMM — transB Auto-Detection (v2.0)

AlexNet and most PyTorch-exported models store FC weights transposed (`transB=1`).
v2.0 infers transposition from the weight tensor shape rather than relying on
a parsed attribute (which the minimal parser may not capture):

```c
uint32_t K = (uint32_t)A->shape.dims[1];

// If B.dims[0] != K, B must be stored as [N_out, K] — transposed
bool transB = ((uint32_t)B->shape.dims[0] != K);
uint32_t N_out = transB ? (uint32_t)B->shape.dims[0]
                        : (uint32_t)B->shape.dims[1];

for (uint32_t i = 0; i < M; i++)
  for (uint32_t j = 0; j < N_out; j++) {
      float sum = 0.0f;
      for (uint32_t k = 0; k < K; k++) {
          float b_val = transB ? b_data[j*K + k]      // B[j,k]
                               : b_data[k*N_out + j]; // B[k,j]
          sum += a_data[i*K + k] * b_val;
      }
      y_data[i*N_out + j] = alpha * sum + beta * (c_data ? c_data[j] : 0.0f);
  }
```

### LRN — Local Response Normalization (New in v2.0)

Required by AlexNet after `conv1` and `conv2`. Implements the ONNX spec formula:

```
y[n,c,h,w] = x[n,c,h,w] / (bias + alpha/size * Σ x[n,j,h,w]²)^beta

  j = max(0, c - floor(size/2)) … min(C-1, c + floor(size/2))
  Default: size=5, alpha=0.0001, beta=0.75, bias=1.0
```

The `β = 0.75` fast path avoids `fast_exp(β * log(d))`:

```c
if (beta == 0.75f) {
    // denom^0.75 = sqrt(denom * sqrt(denom))
    float sd = fast_sqrt(denom);
    denom_pow = fast_sqrt(sd * denom);
} else {
    denom_pow = fast_exp(beta * fast_log(denom));
}
```

---

## Embedded Model Catalogue

All models reside in `src/storage/` and are embedded into the kernel initfs at build time
by `scripts/embed_storage.py` (files ≤ 4 MB only).

| File                    | Size    | Input Shape       | Output        | Architecture           | Operators Tested |
|-------------------------|---------|-------------------|---------------|------------------------|------------------|
| `tiny_mlp.onnx`         | 5 KB    | `[1, 16]`         | `[1, 4]`      | 3-layer MLP            | Gemm, Relu, Softmax |
| `resnet_micro.onnx`     | 40 KB   | `[1, 3, 16, 16]`  | `[1, 10]`     | ResNet (2 blocks)      | Conv, Add (residual), GAP |
| `complex_model.onnx`    | <1 KB   | —                 | —             | Toy model              | — |
| `conv_bn_net.onnx`      | 98 KB   | `[1, 3, 16, 16]`  | `[1, 10]`     | Conv + BN + GAP        | Conv, BatchNorm, GlobalAvgPool |
| `lenet5.onnx`           | 174 KB  | `[1, 1, 28, 28]`  | `[1, 10]`     | LeNet-5                | Conv, MaxPool, Flatten, Gemm |
| `alexnet_tiny.onnx`     | 222 KB  | `[1, 3, 32, 32]`  | `[1, 10]`     | AlexNet-tiny           | Conv, **LRN**, MaxPool, Gemm |
| `vgg_nano.onnx`         | 439 KB  | `[1, 3, 32, 32]`  | `[1, 10]`     | VGG-style (5 convs)    | Deep Conv stacks, GAP |
| `transformer_tiny.onnx` | 789 KB  | `[1, 32]`         | `[1, 10]`     | 6-block FF Transformer | Gemm-heavy, Add, Relu |
| `alexnet_Opset17.onnx`  | 234 MB  | `[1, 3, 224, 224]`| `[1, 1000]`   | Full AlexNet           | **NOT embedded — too large** |

> **Note:** `alexnet_Opset17.onnx` is skipped by `embed_storage.py` (exceeds 4 MB). It can be
> written to `flash.img` for access via ULFS `/storage`, but requires the arena to be enlarged
> beyond 128 MB (full model weights + activations ≈ 450+ MB).

---

## Shell Commands Reference

### `onnx_info <filename>`

Prints model metadata without running inference:

```
> onnx_info lenet5.onnx
ONNX Model Information
  File: lenet5.onnx
  Nodes: 18
  Tensors: 42
  Inputs:  input [float32, 1x1x28x28]
  Outputs: output [float32, 1x10]
```

### `onnx_run <filename>`

Loads the model, generates a zero-input tensor, runs one forward pass, and prints per-node timing:

```
> onnx_run alexnet_tiny.onnx
onnx_run: loading alexnet_tiny.onnx (227914 bytes)
[ONNX] Starting inference...
  Executing: conv1 (Conv)
  Executing: relu1 (Relu)
  Executing: lrn1 (LRN)
  Executing: pool1 (MaxPool)
  … 24 nodes …
[ONNX] Inference complete!

Per-node statistics:
  conv1: 15234 us (1 run)
  lrn1:  3892 us (1 run)
  pool1: 1021 us (1 run)
  …
```

### Regenerating Models

```bash
# Generate the 6-model test suite (tiny_mlp through transformer_tiny)
python3 scripts/generate_models.py

# Regenerate alexnet_tiny specifically
python3 scripts/export_alexnet_tiny.py

# Rebuild kernel (embeds models ≤ 4 MB automatically)
make clean && make all
```

---

## Performance & Profiling

### Viewing Per-Node Timing

After `onnx_run`, per-node execution times (µs) are printed by `ONNX_Runtime_PrintProfile()`.
They are accumulated across multiple calls:

```
========== Runtime Profile ==========
Total inferences: 1
Average time: 42317 us

Per-node statistics:
  conv1: 15234 us (1 run)
  relu1:    82 us (1 run)
  lrn1:   3892 us (1 run)
  pool1:  1021 us (1 run)
  …
```

### Expected Latency (QEMU Cortex-A53, O2)

| Model                   | Approx. Latency |
|-------------------------|-----------------|
| `tiny_mlp.onnx`         | < 1 ms          |
| `resnet_micro.onnx`     | ~ 5–15 ms       |
| `lenet5.onnx`           | ~ 20–50 ms      |
| `conv_bn_net.onnx`      | ~ 30–60 ms      |
| `vgg_nano.onnx`         | ~ 80–200 ms     |
| `alexnet_tiny.onnx`     | ~ 100–300 ms    |
| `transformer_tiny.onnx` | ~ 10–30 ms      |

*(Times are QEMU estimates — real hardware will differ.)*

---

## Known Limitations & Roadmap

### Missing Operators (Blocks Real-World Models)

| Operator            | Needed By              | Priority |
|---------------------|------------------------|----------|
| `Reshape`           | ResNet-50, EfficientNet| High |
| `Transpose`         | ONNX export artifacts  | High |
| `Slice`             | Most models            | High |
| `Pad` (explicit)    | Many CNNs              | High |
| Broadcasting Add/Mul| NCHW bias addition     | High |
| `Gather`            | Embedding layers       | Medium |
| `LayerNormalization`| All transformers       | Medium |
| `LSTM`, `GRU`       | RNN models             | Low |
| `Split`             | Multi-head attention   | Low |

### Numerical Limitations

| Issue                              | Impact                              |
|------------------------------------|-------------------------------------|
| float16 / bfloat16 not supported   | Quantized models fail to load       |
| int8 operations not implemented    | INT8 quantized models unsupported   |
| Softmax: no max-subtraction trick  | Numerical overflow for large logits |
| GEMM transA not parsed             | Some models may need it             |

### Memory Architecture Constraints

- The static 8 MB model buffer lives in BSS — increasing it enlarges the initial BSS segment
- A streaming protobuf reader is needed to handle models > 128 MB RAM
- The 128 MB arena must be reduced if other kernel components need more heap

---

## Integration & Usage Examples

### Embedding a Custom Model in InitFS

1. Export your model:
   ```python
   # In Python with onnx installed:
   # Make sure model is Opset 17 and ≤ 4 MB
   with open("src/storage/my_model.onnx", "wb") as f:
       f.write(model.SerializeToString())
   ```

2. Rebuild:
   ```bash
   make clean && make all
   ```

3. At boot, `ls /storage` will show `my_model.onnx`. Run with:
   ```
   onnx_run my_model.onnx
   ```

### Calling the Runtime Programmatically (C API)

```c
#include "onnx/onnx_loader.h"
#include "onnx/onnx_runtime.h"
#include "kernel/kmem.h"

static ONNX_Graph     g_graph;
static ONNX_InferenceContext g_ctx;

Status run_model(const uint8_t* buf, uint64_t len) {
    // 1. Create arena
    kmem_arena_t* arena = KMEM_ArenaCreate(64 * 1024 * 1024, KMEM_TENSOR_ALIGN);

    // 2. Load
    ONNX_Graph_Init(&g_graph, "my_model");
    g_graph.tensor_arena = arena;
    Status s = ONNX_LoadProtobuf(&g_graph, buf, len);
    if (s != STATUS_OK) return s;

    // 3. Shape propagate + schedule (done inside cmd_onnx_run,
    //    call manually if bypassing the shell command)
    ONNX_Graph_BuildDependencies(&g_graph);
    ONNX_Graph_GenerateSchedule(&g_graph);

    // 4. Create context
    g_ctx.graph          = &g_graph;
    g_ctx.workspace      = NULL;
    g_ctx.workspace_size = 0;

    // 5. Build input tensor
    ONNX_Tensor input = {0};
    input.dtype = ONNX_DTYPE_FLOAT32;
    input.shape.ndim = 4;
    input.shape.dims[0] = 1; input.shape.dims[1] = 3;
    input.shape.dims[2] = 32; input.shape.dims[3] = 32;
    input.shape.total_elements = 1*3*32*32;
    input.data_size = input.shape.total_elements * 4;
    input.data = KMEM_ArenaAlloc(arena, input.data_size, KMEM_TENSOR_ALIGN);
    // … fill input.data with your pixel values …

    // 6. Run inference
    ONNX_Tensor* outputs[1];
    ONNX_Tensor* inputs_arr[1] = {&input};
    return ONNX_Runtime_Inference(&g_ctx, inputs_arr, 1, outputs, 1);
}
```

---

## FAQ & Troubleshooting

### Q: Model is rejected with "invalid file size"
**A:** The file exceeds `MAX_MODEL_SIZE` (currently 8 MB). Either use a smaller model
or increase the constant in `src/onnx/onnx_cmds.c`. Note that `MAX_ARENA_SIZE` must
also be large enough to hold all weights + activations.

### Q: "Error: Maximum tensor limit reached"
**A:** The model has more tensors than `ONNX_MAX_TENSORS` (512). Increase the constant
in `include/onnx/onnx_types.h` and rebuild.

### Q: "Unsupported operator: Reshape" (or Transpose, Slice …)
**A:** The operator is parsed but not yet implemented in `onnx_runtime.c`. See the
roadmap section above for implementation priority.

### Q: Output values look correct in shape but wrong numerically
**A:** Most likely a GEMM transB issue with a non-PyTorch exporter. Check the weight
tensor shape: if `B.dims[0] == K` then `transB=0`; if `B.dims[0] == N_out` then
`transB=1`. The runtime auto-detects this from shape, but ambiguous square matrices
(K == N_out) may be misclassified.

### Q: Large model causes Out of Memory during inference
**A:** Increase `MAX_ARENA_SIZE` in `onnx_cmds.c`. Keep in mind QEMU has 512 MB total
and the kernel itself uses ~10 MB. AlexNet at full resolution needs ~450 MB of arena
space and cannot run in QEMU without reducing image resolution.

### Q: The model file doesn't appear in `/storage`
**A:** Check that the file is ≤ 4 MB (the `embed_storage.py` limit). If larger, it will
be skipped during `make generate_initfs` with a printed warning. Use `ls /storage` in the
MiniOS shell to confirm embedded files.

### Q: How do I add support for a new operator?
1. Add `ONNX_OP_NEWOP` to the enum in `include/onnx/onnx_types.h`
2. Add `case ONNX_OP_NEWOP: return "NewOp";` in `src/onnx/onnx_types.c`
3. Add the string match in `proto_parse_node()` in `src/onnx/onnx_loader.c`
4. Implement `ONNX_Execute_NewOp()` in `src/onnx/onnx_runtime.c`
5. Add a `case ONNX_OP_NEWOP:` dispatch in `ONNX_Runtime_ExecuteNode()`
6. Add shape propagation in the shape pass in `onnx_cmds.c`
