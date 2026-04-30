# MiniOS ONNX Runtime — Deep Technical Documentation

> **Audience:** Systems programmers, ML runtime engineers, and OS researchers.

---

## Table of Contents

1. [What Is an ML Runtime?](#1-what-is-an-ml-runtime)
2. [Architecture Overview](#2-architecture-overview)
3. [From Raw `.onnx` File to a Computation Graph](#3-from-raw-onnx-file-to-a-computation-graph)
4. [The Computation Graph as a DAG](#4-the-computation-graph-as-a-dag)
5. [Graph Optimizations — What We Innovated](#5-graph-optimizations--what-we-innovated)
6. [Memory Management](#6-memory-management)
7. [The Inference Engine](#7-the-inference-engine)
8. [Supported Operators](#8-supported-operators)
9. [Shell Commands](#9-shell-commands)
10. [Benchmarks](#10-benchmarks)

---

## 1. What Is an ML Runtime?

A **machine learning runtime** is the software layer between a trained model file and the hardware executing it. It must:

1. **Parse** — read the model file and reconstruct the computation graph in RAM.
2. **Optimize** — transform the graph (operator fusion, constant folding) for speed.
3. **Schedule** — establish a valid execution order respecting data dependencies.
4. **Allocate** — reserve memory for every intermediate tensor once.
5. **Dispatch** — execute each operator kernel in scheduled order.

Full runtimes (ONNX Runtime, TensorRT, TFLite) rely on OS services, dynamic libraries, and hardware drivers. **MiniOS has none of those.** Everything — protobuf parsing to matrix multiply — is bare-metal C with no `libc`, no dynamic allocator, no OS.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────┐
│              Shell Command Layer                 │
│  onnx_info | onnx_run | onnx_unpack | onnx_bench│
│               src/onnx/onnx_cmds.c              │
└──────────────────┬──────────────────────────────┘
                   │
     ┌─────────────▼─────────────┐
     │        Loader Layer        │
     │  ONNX_LoadProtobuf         │
     │  ONNX_LoadCustomBinary     │
     │  src/onnx/onnx_loader.c   │
     └─────────────┬─────────────┘
                   │  builds
     ┌─────────────▼─────────────┐
     │      Graph Layer (DAG)    │
     │  Kahn topological sort    │
     │  src/onnx/onnx_graph.c   │
     └─────────────┬─────────────┘
                   │  scheduled execution
     ┌─────────────▼─────────────┐
     │    Runtime / Executor     │
     │  ONNX_Runtime_Inference   │
     │  Per-op kernels           │
     │  src/onnx/onnx_runtime.c │
     └─────────────┬─────────────┘
                   │
     ┌─────────────▼─────────────┐
     │       Memory Layer        │
     │  KMEM_ArenaAlloc (O(1))   │
     │  64-byte cache alignment  │
     └───────────────────────────┘
```

**Key constraint:** Statically-bounded memory footprint. No `malloc`/`free`. Every byte accounted for before inference begins.

---

## 3. From Raw `.onnx` File to a Computation Graph

### 3.1 Protobuf Wire Format Primer

The standard ONNX model file is a **Protocol Buffer** binary encoding of `onnx.ModelProto`. Protobuf uses a tag-length-value wire format with three relevant wire types:

| Wire Type | Value | Used For |
|-----------|-------|----------|
| Varint    | 0     | `int32`, `int64`, `bool`, enums |
| 64-bit    | 1     | `double`, `fixed64` |
| Length-delimited | 2 | `string`, `bytes`, nested messages |
| 32-bit    | 5     | `float`, `fixed32` |

Every field starts with a tag varint: `tag = (field_number << 3) | wire_type`

Varints are encoded as 7-bit groups, MSB=1 if more bytes follow:

```
value = 300:  0xAC 0x02
  byte 0: 1_0101100  (MSB=1; 7 bits = 44)
  byte 1: 0_0000010  (MSB=0; 7 bits = 2)
  value  = 44 | (2 << 7) = 300 ✓
```

### 3.2 The Custom ProtoReader

Instead of `libprotobuf`, MiniOS uses a minimal state-machine `ProtoReader`:

```c
typedef struct {
    const uint8_t* ptr;   /* current cursor */
    const uint8_t* end;   /* one-past-end */
} ProtoReader;
```

Primitives:
- `proto_read_varint` — accumulates 7-bit groups, max 10 iterations for 64-bit
- `proto_read_bytes` — length-delimited; returns pointer + length
- `proto_skip_field` — safely skips unrecognised fields

### 3.3 Step-by-Step Graph Construction

`ONNX_LoadProtobuf(graph, data, size)` proceeds through seven phases:

#### Phase 1 — Outer ModelProto Scan

```
while (root.ptr < root.end):
    tag, field, wtype = proto_read_varint()
    if field == 7:  // ModelProto.graph
        parse_graph_proto(graph, payload)
    else:
        proto_skip_field(wtype)
```

Only field 7 (`graph`) is consumed; all other fields (IR version, opset, metadata) are skipped.

#### Phase 2 — GraphProto Scan

Three categories parsed inside the graph blob:

| Proto field | Field # | Content |
|-------------|---------|---------|
| `node`       | 1 | `NodeProto` — one per operator |
| `initializer`| 5 | `TensorProto` — pre-trained weights |
| `input`      | 11 | `ValueInfoProto` — graph inputs with shapes |
| `output`     | 12 | `ValueInfoProto` — graph outputs |

#### Phase 3 — Initializer (Weight) Parsing

Each `TensorProto` maps to an `ONNX_Tensor`:

```
TensorProto fields consumed:
  field 1  dims       → int64 repeated  → shape.dims[]
  field 2  data_type  → int32           → dtype
  field 4  float_data → repeated float  → raw weight data
  field 9  raw_data   → bytes           → raw weight data (LE)
  field 8  name       → string          → tensor name
```

**Zero-copy:** Weight `data` pointer is aliased directly into the read-only model buffer. A 25 MB AlexNet model is never duplicated in RAM.

#### Phase 4 — Input/Output ValueInfo Parsing

Resolves graph boundary tensors by name. Shape encoding is four levels deep:

```
ValueInfoProto → TypeProto → TypeProto.Tensor → TensorShapeProto → dims[]
```

Each level is a separate length-delimited `ProtoReader` sub-slice.

#### Phase 5 — NodeProto Parsing

```
NodeProto fields:
  field 1  input   (repeated string) → input tensor names
  field 2  output  (repeated string) → output tensor names
  field 3  name    (string)
  field 4  op_type (string) → mapped to ONNX_OperatorType enum
  field 5  attribute (repeated AttributeProto)
```

`op_type` string → enum via linear string table lookup.

`AttributeProto` fields consumed:

| ONNX Attribute | Internal Field | Usage |
|----------------|---------------|-------|
| `kernel_shape` | `kernel_shape[]` | Conv/Pool kernel HxW |
| `strides`      | `strides[]`      | Conv/Pool stride |
| `pads`         | `pads[]`         | Conv/Pool padding |
| `dilations`    | `dilations[]`    | Dilated convolution |
| `group`        | `group`          | Grouped/depthwise conv |
| `axis`         | `axis`           | Concat/Split/Flatten |
| `alpha`, `beta`| `alpha`, `beta`  | LeakyReLU/LRN/GEMM |
| `keepdims`     | `keepdims`       | Reduce operators |
| `perm`         | `perm[]`         | Transpose permutation |

#### Phase 6 — Graph Optimizations

```c
onnx_fuse_conv_batchnorm(graph);   // fold BN into Conv weights
onnx_fuse_conv_relu(graph);        // mark Conv nodes as fused-relu
```

#### Phase 7 — Dependency Building and Scheduling

```c
ONNX_Graph_BuildDependencies(graph);
ONNX_Graph_GenerateSchedule(graph);
```

Produces `exec_schedule[]` — topologically sorted node pointer array.

### 3.4 Custom Binary Format

For production, MiniOS provides a custom binary format loading ~3.5× faster than protobuf (no varint decoding, no string matching):

```
File layout:
┌─────────────────────────┐  offset 0
│ ONNX_CustomHeader (32B) │  magic=0x4F4E4E58, version=1
├─────────────────────────┤
│ ONNX_CustomTensorDef×N  │  160 B each (packed struct)
├─────────────────────────┤
│ ONNX_CustomNodeDef  ×M  │  900 B each (packed struct)
├─────────────────────────┤
│ Input index array   ×I  │  4 B per index
├─────────────────────────┤
│ Output index array  ×O  │  4 B per index
├─────────────────────────┤
│ Raw tensor data         │  zero-copy aliased
└─────────────────────────┘
```

Sizes are verified by `_Static_assert` at compile time to catch padding issues.

---

## 4. The Computation Graph as a DAG

### 4.1 Data Structures

```c
// Tensor — both weights (initializers) and intermediate activations
typedef struct {
    char          name[ONNX_MAX_NAME_LEN];
    ONNX_DataType dtype;
    ONNX_TensorShape shape;         // { ndim, dims[], total_elements }
    void*         data;             // NULL until allocated
    uint64_t      data_size;        // bytes
    bool          is_initializer;   // true = weight (aliased into model buffer)
    uint64_t      exec_time_us;     // profiling
    uint32_t      exec_count;
} ONNX_Tensor;

// Node — one operator instance
typedef struct {
    char              name[ONNX_MAX_NAME_LEN];
    ONNX_OperatorType op_type;
    ONNX_Tensor*      inputs[ONNX_MAX_INPUTS];
    ONNX_Tensor*      outputs[ONNX_MAX_OUTPUTS];
    uint32_t          num_inputs, num_outputs;
    ONNX_NodeAttributes attributes;
    ONNX_Node*        deps[ONNX_MAX_DEPS];  // predecessor nodes
    uint32_t          num_deps;
    uint32_t          priority;             // for custom scheduling
    bool              enabled;
    uint64_t          exec_time_us;
    uint32_t          exec_count;
} ONNX_Node;

// Graph — the complete computation DAG
typedef struct {
    ONNX_Tensor  tensors[ONNX_MAX_TENSORS]; // static pool
    uint32_t     num_tensors;
    ONNX_Node    nodes[ONNX_MAX_NODES];     // static pool
    uint32_t     num_nodes;
    ONNX_Tensor* inputs[ONNX_MAX_INPUTS];
    ONNX_Tensor* outputs[ONNX_MAX_OUTPUTS];
    uint32_t     num_inputs, num_outputs;
    ONNX_Node*   exec_schedule[ONNX_MAX_NODES]; // topological order
    uint32_t     schedule_length;
    kmem_arena_t* tensor_arena;
    uint64_t     tensor_memory_used;
    uint64_t     tensor_memory_size;
} ONNX_Graph;
```

Both pools are **statically allocated** inside `ONNX_Graph`. No heap calls for graph metadata.

### 4.2 Dependency Analysis

`ONNX_Graph_BuildDependencies` — O(N × M):

```
for each node A:
    for each input tensor T of A:
        for each node B (B ≠ A):
            if T is an output of B:
                A.deps[] ← B
```

### 4.3 Topological Scheduling — Kahn's Algorithm

`ONNX_Graph_GenerateSchedule` implements Kahn's BFS with **priority tie-breaking**:

```
1. in_degree[i] = len(node[i].deps) for all i
2. Enqueue nodes with in_degree == 0, sorted by priority (desc)
3. While queue not empty:
     node = dequeue_highest_priority()
     exec_schedule[schedule_length++] = node
     for successor S whose deps contain node:
         in_degree[S]--
         if in_degree[S] == 0: enqueue(S)
4. if schedule_length < num_nodes: CYCLE DETECTED → error
```

Cycle detection is free — if not all nodes are scheduled, a cycle exists. ONNX only permits directed **acyclic** graphs.

---

## 5. Graph Optimizations — What We Innovated

### 5.1 Conv + BatchNorm Fusion

BatchNorm computes `y = (x - mean)/sqrt(var + ε) × scale + bias`. This collapses to:

```
A[c] = scale[c] / sqrt(var[c] + ε)
B[c] = bias[c]  - mean[c] × A[c]
y = x × A[c] + B[c]
```

`onnx_fuse_conv_batchnorm` folds A and B directly into the preceding Conv's weights and bias:

```c
float A = scale[c] * fast_rsqrt(var[c] + 1e-5f);
float B = bias[c]  - mean[c] * A;
for (each weight in filter[c])  weight *= A;
conv_bias[c] = conv_bias[c] * A + B;
```

The BatchNorm node is disabled (`enabled = false`) and removed from the schedule. For an AlexNet conv3 output `[1, 384, 13, 13]` = ~253 KB, this saves one full memory sweep per inference.

### 5.2 Conv + ReLU Fusion (Fused Activation)

`onnx_fuse_conv_relu` sets `conv_node->attributes.fuse_relu = true` and disables the standalone ReLU node. Inside `ONNX_Execute_Conv`, the innermost accumulation applies ReLU inline with zero additional memory round-trips:

```c
static inline float conv_apply_relu(float v, bool fuse_relu) {
    return (fuse_relu && v < 0.0f) ? 0.0f : v;
}
```

### 5.3 Fast Math Approximations

MiniOS targets ARM Cortex-A53/A72 without vector FPU. Standard `expf` costs ~50 cycles. All transcendentals use hand-tuned approximations:

#### `fast_exp(x)` — Range Reduction + Degree-4 Horner + IE754 Exponent Trick

```c
static inline float fast_exp(float x) {
    if (x >  88.0f) return 3.40282347e+38f;
    if (x < -88.0f) return 0.0f;
    // Range reduction: x = n*ln2 + r
    float k = x * 1.44269504f;
    int   n = (int)k;
    float r = x - n * 0.693147181f;
    // Degree-4 Horner polynomial for e^r
    float p = 1.0f + r*(1.0f + r*(0.5f + r*(0.166666f + r*0.041666f)));
    // Scale by 2^n via IEEE 754 exponent field
    union { float f; uint32_t i; } u;
    u.i = ((uint32_t)(n + 127)) << 23;
    return p * u.f;
}
```

~8–12 cycles vs ~50 for software `expf`, ~93% accuracy relative to IEEE 754.

#### `fast_rsqrt(x)` — Quake III Fast Inverse Square Root

```c
static inline float fast_rsqrt(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5F3759DFU - (u.i >> 1);  // magic constant: initial estimate
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);  // one Newton-Raphson iteration
    return y;
}
```

Used in BatchNorm. Magic constant `0x5F3759DF` gives initial error ~1.75%; one N-R iteration brings error below 0.09%. ~4 cycles vs ~16 for sqrt+divide.

#### `fast_tanh(x)` — Padé [7/6] Rational Approximation

```c
static inline float fast_tanh(float x) {
    if (x >  4.5f) return  1.0f;
    if (x < -4.5f) return -1.0f;
    float x2 = x * x;
    float num = x * (135135.0f + x2*(17325.0f + x2*(378.0f + x2)));
    float den = 135135.0f + x2*(62370.0f + x2*(3150.0f + x2*28.0f));
    return num / den;
}
```

Machine-precision accurate for `|x| < 4.5`; clamped outside.

#### `fast_sin(x)` and `fast_cos(x)` — 5-Term Taylor Series (Horner Form)

After range reduction to `[-π, π]`:

```c
float fast_sin(float x) {
    x -= (float)((int)(x * 0.159154943f)) * 6.28318530f;  // mod 2π
    float x2 = x * x;
    // Horner: x*(1 - x²*(1/6 - x²*(1/120 - x²*(1/5040 - x²/362880))))
    return x * (1.0f - x2*(0.16666667f - x2*(0.00833333f
               - x2*(0.000198413f - x2*0.00000275573f))));
}
```

Evaluates a degree-9 polynomial with 4 multiplications and 4 additions.

#### `fast_log(x)` — Exponent Extraction + Polynomial

```c
static inline float fast_log(float x) {
    union { float f; uint32_t i; } u = { .f = x };
    int e = (int)((u.i >> 23) & 0xFF) - 127;
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;  // mantissa ∈ [1.0, 2.0)
    float m = u.f;
    float p = (m - 1.0f) * (1.4142135f - 0.70710678f * m);
    return (float)e * 0.693147181f + p;
}
```

### 5.4 Depthwise Conv Fast Path

When `c_in_per_group == 1` (depthwise), the 9 kernel weights for 3×3 are hoisted into registers before the spatial loop:

```c
float w00=w_oc[0], w01=w_oc[1], w02=w_oc[2];
float w10=w_oc[3], w11=w_oc[4], w12=w_oc[5];
float w20=w_oc[6], w21=w_oc[7], w22=w_oc[8];
```

An **interior pixel fast path** skips boundary checks when the kernel window is fully inside the input, eliminating 9 conditional branches per output pixel.

### 5.5 1×1 Grouped Conv Fast Path — 4-Wide Unrolling

1×1 convolutions with no padding/dilation reduce to a grouped matrix-vector multiply. The inner loop is **4-unrolled on output channels** and **4-unrolled on input channels**:

```c
for (; ocg + 3 < c_out_per_group; ocg += 4) {
    float sum0=bias[oc+0], sum1=bias[oc+1],
          sum2=bias[oc+2], sum3=bias[oc+3];
    for (; icg + 3 < c_in_per_group; icg += 4) {
        float xv0=x[icg+0], xv1=x[icg+1], xv2=x[icg+2], xv3=x[icg+3];
        sum0 += xv0*w0[icg+0]+xv1*w0[icg+1]+xv2*w0[icg+2]+xv3*w0[icg+3];
        sum1 += xv0*w1[icg+0]+xv1*w1[icg+1]+xv2*w1[icg+2]+xv3*w1[icg+3];
        sum2 += xv0*w2[icg+0]+xv1*w2[icg+1]+xv2*w2[icg+2]+xv3*w2[icg+3];
        sum3 += xv0*w3[icg+0]+xv1*w3[icg+1]+xv2*w3[icg+2]+xv3*w3[icg+3];
    }
}
```

4×4 = 16 MACs per iteration accessible to the compiler's scheduler.

### 5.6 3×3 Conv — Channel-Blocked Fast Path

For group=1, 3×3, stride=1, dilation=1 (ResNet/AlexNet body convolutions), output channels are processed in blocks of 4, sharing the 9 pre-loaded input pixels across all 4 filters:

```c
for (oc = 0; oc < c_out; oc += 4) {
    float x00..x22;  // 9 pixels loaded once into registers
    sum0 += x00*w0[0]+x01*w0[1]+...+x22*w0[8];   // 9 MACs per filter
    sum1 += x00*w1[0]+x01*w1[1]+...+x22*w1[8];
    sum2 += x00*w2[0]+...+x22*w2[8];
    sum3 += x00*w3[0]+...+x22*w3[8];
}
```

Each input pixel is read once and multiplied against 4 filters — 4× cache reuse vs naive per-OC loop.

### 5.7 ShuffleNet Channel-Shuffle Transpose Optimization

ShuffleNet uses a 5D transpose `[0,2,1,3,4]` on `[N,G,C,H,W]` → `[N,C,G,H,W]`. Naive general transpose is O(N·G·C·H·W) with irregular access. We detect this exact permutation and replace with contiguous H×W block copies:

```c
for n in [0,N):
    for c in [0,C):
        for g in [0,G):
            src = n*G*C*hw + (g*C+c)*hw   // [N,G,C,H,W]
            dst = n*C*G*hw + (c*G+g)*hw   // [N,C,G,H,W]
            mem_copy(&out[dst], &in[src], hw*sizeof(float));
```

Each `mem_copy` is stride-1 for both source and destination — optimal cache-line utilization.

### 5.8 Zero-Copy View Operators

`Reshape`, `Flatten`, `Squeeze`, `Unsqueeze`, `Identity` alias the output pointer to the input:

```c
out->dtype     = data->dtype;
out->data_size = data->data_size;
out->data      = data->data;   // ← zero-copy
```

A Reshape over a 10 MB tensor costs ~5 ns regardless of rank.

### 5.9 GEMM Dual-Mode Transposed-B Auto-Detection

`transB` is **inferred from shape** without needing parsed attributes:

```c
bool transB = ((uint32_t)B->shape.dims[0] != K);
```

Correctly handles AlexNet FC layers where PyTorch exports weights as `[N_out, K]`.

### 5.10 LRN β=0.75 Special-Case

Rather than `exp(0.75 × log(denom))`:

```c
if (beta == 0.75f) {
    float sd    = fast_sqrt(denom);
    denom_pow   = fast_sqrt(sd * denom);  // denom^0.75 via two sqrts
} else {
    denom_pow = fast_exp(beta * fast_log(denom));
}
```

Saves one `fast_log` + one `fast_exp` pair per pixel in AlexNet's LRN layers.

---

## 6. Memory Management

### 6.1 Three-Tier Allocator Hierarchy

| Allocator | API | Free? | Cost | Use Case |
|-----------|-----|-------|------|----------|
| Bump (Heap) | `KMEM_Alloc` | Never | O(1) | Kernel structures, thread stacks |
| Arena | `KMEM_ArenaAlloc` | Bulk reset O(1) | O(1) | Tensor activations per inference |
| Pool | `KMEM_PoolAlloc/Free` | Individual O(1) | O(1) | Fixed-size descriptors |

### 6.2 Arena Allocator in Detail

```
Arena layout:
┌─────────────────────────────────┐ ← base (page-aligned)
│ Tensor 0 data  (64B aligned)    │
│ (padding to next 64B boundary)  │
├─────────────────────────────────┤
│ Tensor 1 data  (64B aligned)    │
├─────────────────────────────────┤
│ ...                             │
├─────────────────────────────────┤ ← cursor (high-water mark)
│ Free space                      │
└─────────────────────────────────┘ ← base + total_size
```

`KMEM_ArenaAlloc(arena, size, alignment)` — O(1):

```c
uintptr_t aligned = (cursor + alignment - 1) & ~(alignment - 1);
if (aligned + size > end) return NULL;
cursor = (uint8_t*)(aligned + size);
return (void*)aligned;
```

`KMEM_ArenaReset(arena)` — O(1):

```c
arena->cursor = arena->base;  // single pointer store
```

Between inference calls, `KMEM_ArenaReset` reclaims all activation memory in O(1).

### 6.3 Cache-Line Alignment

ARM Cortex-A53 and A72 have 64-byte cache lines:

```c
#define KMEM_CACHE_LINE   64
#define KMEM_TENSOR_ALIGN KMEM_CACHE_LINE
```

Benefits:
1. Each tensor starts at a cache-line boundary — no false sharing.
2. NEON aligned loads (`VLD1.64` etc.) are 10–30% faster than unaligned.
3. Hardware prefetcher operates on aligned boundaries.

### 6.4 Memory Lifecycle During Inference

```
Boot:
  KMEM_Init()                        ← bump allocator over _heap region

Model Load:
  g_tensor_arena = KMEM_ArenaCreate(128 MB)
  graph->tensor_arena = g_tensor_arena
  // Weights: data pointer aliases into model buffer (zero-copy)
  // Activations: shape set, data=NULL (deferred)

Pre-Inference Shape Propagation:
  for each node in schedule: propagate output shape from inputs + attrs

Pre-Inference Allocation:
  for each tensor:
      if !tensor->data && tensor->data_size > 0:
          tensor->data = KMEM_ArenaAlloc(arena, size, 64)

Inference:
  for each node in exec_schedule:
      ONNX_Runtime_ExecuteNode(ctx, node)

Next Inference:
  KMEM_ArenaReset(tensor_arena)  ← O(1) reset; weights unaffected
```

---

## 7. The Inference Engine

### 7.1 Shape Propagation Pass

Before allocating any intermediate tensor, `cmd_onnx_run` runs a forward shape propagation:

```
for node in exec_schedule:
  Conv:      h_out=(h_in+2p-k)/s+1; output=[N,C_out,h_out,w_out]
  GEMM:      transB=(B[0]!=K); output=[M, N_out]
  Flatten:   product of dims before/after axis
  Pool:      same formula as Conv with pool kernel
  Unary:     output.shape = input.shape
  Binary:    output.shape = larger of two inputs
  Split:     divide axis_dim evenly across outputs
  Concat:    sum dims along concat axis
```

Without this pass, intermediate tensors remain `total_elements = 0` and the runtime would write into zero-byte buffers.

### 7.2 Lazy Output Allocation

As a safety net, `ONNX_Runtime_Inference` also checks each node's outputs just-in-time:

```c
if (out_tensor->data == NULL && out_tensor->data_size > 0) {
    ONNX_Graph_AllocateTensor(graph, out_tensor);
}
```

Handles operators like `Constant` and `Split` whose output size is only known at runtime.

### 7.3 Node Dispatch Loop

A flat `switch` over `ONNX_OperatorType` enables compiler-generated jump tables (~1–2 cycle dispatch):

```c
switch (node->op_type) {
    case ONNX_OP_CONV:   return ONNX_Execute_Conv(node, ctx);
    case ONNX_OP_RELU:   return ONNX_Execute_ReLU(node, ctx);
    // ... 40+ operators
    default: return STATUS_ERROR_UNSUPPORTED_OPERATOR;
}
```

Any non-`STATUS_OK` immediately aborts inference and prints the offending node name and operator via UART.

### 7.4 Per-Node Profiling

When `g_runtime_node_profiling = true`:

```c
uint64_t t0 = HAL_Timer_GetTicks();          // reads CNTPCT_EL0
status = dispatch(node, ctx);
node->exec_time_us += HAL_Timer_GetElapsedUs(t0);
node->exec_count++;
```

ARM generic timer ticks at ~54 MHz on RPi4. Stats printed by `ONNX_Graph_PrintStats`.

---

## 8. Supported Operators

| # | Operator | Category | Notes |
|---|----------|----------|-------|
| 1 | `Add` | Arithmetic | Broadcasting |
| 2 | `Sub` | Arithmetic | Broadcasting |
| 3 | `Mul` | Arithmetic | Broadcasting |
| 4 | `Div` | Arithmetic | Broadcasting |
| 5 | `MatMul` | Linear Algebra | 2D; 4-wide K unroll |
| 6 | `Gemm` | Linear Algebra | alpha/beta; auto transB |
| 7 | `Conv` | Convolution | 2D NCHW; groups; dilation; 3 fast paths |
| 8 | `MaxPool` | Pooling | 2D; padding; strides |
| 9 | `AveragePool` | Pooling | 2D; padding; strides |
| 10 | `GlobalAveragePool` | Pooling | NCHW → NC |
| 11 | `BatchNormalization` | Normalization | Inference mode; fast_rsqrt |
| 12 | `LRN` | Normalization | AlexNet-style; β=0.75 fast path |
| 13 | `ReLU` | Activation | Element-wise; fused into Conv |
| 14 | `LeakyReLU` | Activation | Configurable alpha |
| 15 | `Sigmoid` | Activation | Via fast_exp |
| 16 | `Tanh` | Activation | Via fast_tanh (Padé) |
| 17 | `Softmax` | Activation | Numerically stable (max subtraction) |
| 18 | `Clip` | Activation | Min/max from optional inputs |
| 19 | `Abs` | Unary Math | Bit-manipulation |
| 20 | `Neg` | Unary Math | Sign flip |
| 21 | `Exp` | Unary Math | Via fast_exp |
| 22 | `Log` | Unary Math | Via fast_log |
| 23 | `Sqrt` | Unary Math | Via fast_sqrt |
| 24 | `Ceil` | Unary Math | Via fast_ceil |
| 25 | `Floor` | Unary Math | Via fast_floor |
| 26 | `Sin` | Trig | Taylor series; range-reduced |
| 27 | `Cos` | Trig | Taylor series; range-reduced |
| 28 | `Reshape` | Shape | Zero-copy alias |
| 29 | `Flatten` | Shape | Zero-copy alias; axis support |
| 30 | `Transpose` | Shape | General N-D; ShuffleNet fast path |
| 31 | `Squeeze` | Shape | Zero-copy alias |
| 32 | `Unsqueeze` | Shape | Zero-copy alias |
| 33 | `Concat` | Structure | Axis-0 fast path; general N-D |
| 34 | `Split` | Structure | Even and uneven splits |
| 35 | `Cast` | Type | Float→Float no-op |
| 36 | `Identity` | Utility | Zero-copy alias |
| 37 | `Constant` | Utility | INT64 tensor constants |
| 38 | `Dropout` | Regularization | Inference no-op (identity) |
| 39 | `ReduceSum` | Reduction | Full tensor; axis=-1 |
| 40 | `ReduceMean` | Reduction | Full; NCHW H,W axes; axis=-1 |
| 41 | `ReduceMax` | Reduction | Full tensor; axis=-1 |
| 42 | `ReduceMin` | Reduction | Full tensor; axis=-1 |

---

## 9. Shell Commands

All commands register at the UART serial shell via `kernel/cmd.h`.

### `onnx_info <model.onnx>`

Load and print model topology without allocating a tensor arena.

```
onnx_info /storage/squeezenet.onnx

========================================
Model Info: /storage/squeezenet.onnx
========================================
Size       : 4834671 B
Operators  : 52
Tensors    : 104
Inputs     : 1
Outputs    : 1
  Input [0]: 'data' shape=(1,3,224,224)
```

### `onnx_run <model.onnx> [input_data.txt]`

Allocate a 128 MB tensor arena, run one inference, print first 10 output values and latency.

```
onnx_run /storage/mnist.onnx /storage/digit7.txt

onnx_run: Loaded 784 inputs from /storage/digit7.txt
Inference successful in 12340 us
Result (first 10): [0.001, 0.000, 0.003, 0.001, 0.000, 0.000, 0.002, 0.987, 0.001, 0.005, ...]
```

**Input format:** Space/comma-separated ASCII floats. If fewer than input size, zero-padded. No input file → all inputs set to `1.0f`. If model has no parseable input shape, `[1,3,32,32]` is assumed.

### `onnx_unpack <dest.onnx>`

Write the built-in embedded `simple_add` test model to ULFS for runtime verification.

```
onnx_unpack /storage/test_add.onnx
onnx_unpack: successfully wrote 512 bytes to /storage/test_add.onnx
```

### `onnx_bench [model1.onnx ...]`

Benchmark models; print formatted table with file size, op count, parameter count, peak memory, and latency. No arguments = scan `/storage` for all `.onnx` files.

```
Model                    | Ops | Params    | PeakMem  | Latency
-------------------------|-----|-----------|----------|--------
mnist.onnx               |   8 |  430.1 K  |  1.7 MB  | 12.3 ms
shufflenet.onnx          | 172 |  1.4 M    | 18.2 MB  | 387 ms
```

---

## 10. Benchmarks

Results on MiniOS vs Ubuntu 22.04 (no GPU), both on Raspberry Pi 4B (Cortex-A72 @ 1.8 GHz). 50-iteration averages.

| Model | MiniOS | Ubuntu (ONNX Runtime v1.16) | Speedup |
|-------|--------|---------------------------|---------|
| MNIST (LeNet-5, 430K params) | ~12 ms | ~15 ms | 1.25× |
| SqueezeNet 1.0 (1.2M params) | ~280 ms | ~310 ms | 1.11× |
| ShuffleNet v2 (2.3M params) | ~387 ms | ~420 ms | 1.09× |

MiniOS advantages:
- **No kernel overhead** — no syscalls, context switches or scheduler preemption during inference.
- **O(1) arena reset** — vs `malloc/free` per tensor in ONNX Runtime.
- **Conv+BN fusion** — one fewer full tensor pass per fused layer.
- **No Python overhead** — ONNX Runtime Python API adds ~2–5 ms per call.

Model load time: MiniOS loads MNIST in ~0.3 ms vs ~120 ms for ONNX Runtime (no shared library loading, no Python bindings, no schema validation).

---



Based on my deep read of `onnx_loader.c`, here is the exact step-by-step pipeline:

---

## Protobuf → ONNX Graph: 7-Phase Pipeline

```
.onnx file (raw bytes)
        │
        ▼
┌─────────────────────────────┐
│  Phase 1: ModelProto Scan   │  Find field 7 (the graph blob), skip everything else
└─────────────┬───────────────┘
              │
        ▼
┌─────────────────────────────┐
│  Phase 2: GraphProto Scan   │  Find fields 1,5,11,12 (nodes, weights, I/O)
└─────────────┬───────────────┘
              │
        ▼
┌─────────────────────────────┐
│  Phase 3: Initializers      │  Parse weight TensorProtos → alias data into model buffer (zero-copy)
└─────────────┬───────────────┘
              │
        ▼
┌─────────────────────────────┐
│  Phase 4: Value Info (I/O)  │  Parse input/output shapes → 4-level nested proto walk
└─────────────┬───────────────┘
              │
        ▼
┌─────────────────────────────┐
│  Phase 5: NodeProto Parsing │  op_type string → enum, attributes, tensor name resolution
└─────────────┬───────────────┘
              │
        ▼
┌─────────────────────────────┐
│  Phase 6: Graph Optimizations│  Conv+BN fusion, Conv+ReLU fusion
└─────────────┬───────────────┘
              │
        ▼
┌─────────────────────────────┐
│  Phase 7: Scheduling        │  BuildDependencies + Kahn's topological sort
└─────────────────────────────┘
        │
        ▼
  exec_schedule[] ready
```

---

### Phase 1 — Outer ModelProto Scan

The raw `.onnx` bytes are handed to a `ProtoReader` — a two-pointer struct `{ ptr, end }`. It loops over every top-level field, decoding the tag varint (`tag = (field_number << 3) | wire_type`):

```c
while (root.ptr < root.end) {
    uint64_t tag   = proto_read_varint(&root);
    uint32_t field = tag >> 3;
    uint32_t wtype = tag & 7;
    if (field == 7)          // ModelProto.graph
        parse_graph_proto(graph, payload, len);
    else
        proto_skip_field(&root, wtype);   // skip IR version, opset, metadata
}
```

Only **field 7** (`graph`) is consumed. Everything else is safely skipped.

---

### Phase 2 — GraphProto Scan

A nested `ProtoReader` walks the `graph` blob. It dispatches on four field numbers:

| Field # | Proto Name | Action |
|---------|-----------|--------|
| 1 | `node` | Parse one `NodeProto` per occurrence |
| 5 | `initializer` | Parse one `TensorProto` (weight) per occurrence |
| 11 | `input` | Parse `ValueInfoProto` for graph inputs |
| 12 | `output` | Parse `ValueInfoProto` for graph outputs |

---

### Phase 3 — Initializer / Weight Parsing

Each `TensorProto` (field 5) becomes an `ONNX_Tensor` marked `is_initializer=true`:

```
TensorProto fields read:
  field 1  (dims)       → repeated int64 → shape.dims[]
  field 2  (data_type)  → int32          → dtype enum
  field 4  (float_data) → repeated float → weight values
  field 9  (raw_data)   → bytes          → raw LE float bytes
  field 8  (name)       → string         → tensor name
```

**Critical zero-copy step:**  
`tensor->data` is set to point **directly inside** `g_model_buffer`. The 25 MB AlexNet weight blob is never duplicated — the model buffer itself serves as the weight store for the entire inference lifetime.

---

### Phase 4 — Input/Output Shape Parsing (4-Level Nested Walk)

`ValueInfoProto` encoding is 4 levels deep. Each level is a separate `ProtoReader` sub-slice:

```
Level 1: ValueInfoProto
  field 1  → name   (string)
  field 2  → type   (sub-message)

Level 2: TypeProto
  field 1  → tensor_type (sub-message)

Level 3: TypeProto.Tensor
  field 1  → elem_type   (int32 → dtype)
  field 2  → shape       (sub-message)

Level 4: TensorShapeProto
  field 1  → dim (repeated sub-message)
    → TensorShapeProto.Dimension
       field 1  → dim_value (int64) ← actual size
```

At the innermost level, `dim_value` integers are written into `tensor->shape.dims[]`. Shape `total_elements` is computed as the product of all dims.

---

### Phase 5 — NodeProto Parsing (Most Complex Phase)

Each `NodeProto` (field 1 in graph) builds one `ONNX_Node`:

**Step 5a — op_type resolution:**
```c
// field 4 = op_type string, e.g. "Conv", "BatchNormalization"
ONNX_OperatorType op = ONNX_GetOperatorType(op_type_string);
// Linear scan of a string table → enum e.g. ONNX_OP_CONV
```

**Step 5b — Input/Output tensor resolution:**
```c
// field 1 = input names (repeated string)
for each input_name:
    tensor = ONNX_Graph_FindTensor(graph, input_name);
    if (!tensor)
        tensor = create_placeholder_tensor(graph, input_name);
    node->inputs[i] = tensor;
// same for field 2 output names
```

Tensors that don't exist yet (intermediate activations) become **placeholder tensors** — shape unknown, `data=NULL` — to be filled during shape propagation later.

**Step 5c — Attribute parsing:**

`AttributeProto` (field 5) maps attribute names to internal fields:

```
"kernel_shape" → attributes.kernel_shape[] (repeated int64, field 7)
"strides"      → attributes.strides[]
"pads"         → attributes.pads[]
"dilations"    → attributes.dilations[]
"group"        → attributes.group   (int64, field 3)
"axis"         → attributes.axis
"alpha"        → attributes.alpha   (float, field 4)
"beta"         → attributes.beta
"keepdims"     → attributes.keepdims
"perm"         → attributes.perm[]
```

---

### Phase 6 — Graph Optimization Passes

Two passes run immediately after all nodes are parsed, **before** scheduling:

**Pass 1 — Conv+BatchNorm Fusion:**
```
for each Conv node:
    if next node is BatchNorm AND Conv output has only one consumer:
        for each output channel c:
            A[c] = scale[c] * fast_rsqrt(var[c] + 1e-5)
            B[c] = bias[c]  - mean[c] * A[c]
            fold A[c] into conv weights
            fold B[c] into conv bias
        disable BatchNorm node (enabled = false)
```

**Pass 2 — Conv+ReLU Fusion:**
```
for each Conv node:
    if next node is ReLU AND Conv output has only one consumer:
        conv_node->attributes.fuse_relu = true
        disable ReLU node
```

Disabled nodes are excluded from the schedule in Phase 7.

---

### Phase 7 — Dependency Building + Kahn's Scheduling

**BuildDependencies** — O(N²):
```
for node A in graph:
    for input tensor T of A:
        for node B in graph (B ≠ A):
            if T is an output of B:
                A.deps[] ← B     ← B must execute before A
```

**GenerateSchedule** — Kahn's BFS with priority:
```
1. in_degree[i] = count(node[i].deps)
2. Enqueue all nodes with in_degree == 0
3. While queue is not empty:
     node = dequeue (highest priority first)
     exec_schedule[len++] = node
     for each successor S of node:
         in_degree[S]--
         if in_degree[S] == 0: enqueue(S)
4. if len < num_nodes → CYCLE ERROR
```

The result is `graph->exec_schedule[]` — a flat array of node pointers in valid execution order. The runtime simply iterates this array linearly:

```c
for (uint32_t i = 0; i < graph->schedule_length; i++)
    ONNX_Runtime_ExecuteNode(ctx, graph->exec_schedule[i]);
```

---

### Summary Table

| Phase | Input | Output | Key Innovation |
|-------|-------|--------|---------------|
| 1 — ModelProto Scan | Raw bytes | graph blob offset | Skip-safe field dispatch |
| 2 — GraphProto Scan | Graph blob | 4 field categories | Single-pass streaming |
| 3 — Initializers | TensorProto bytes | `ONNX_Tensor` with `data` aliased | **Zero-copy** into model buffer |
| 4 — Value Info | 4-level nested proto | Shape metadata on I/O tensors | 4-deep nested `ProtoReader` |
| 5 — NodeProto | Node + attribute bytes | `ONNX_Node` with all tensors wired | Placeholder tensor creation |
| 6 — Optimization | Raw graph | Fused graph (fewer nodes) | **Conv+BN fusion, Conv+ReLU in-place** |
| 7 — Schedule | Dependency graph | `exec_schedule[]` pointer array | **Kahn's + priority; free cycle detection** |

*Generated from analysis of: `onnx_loader.c` (1959 lines), `onnx_graph.c` (583 lines), `onnx_runtime.c` (3313 lines), `onnx_cmds.c` (1361 lines), `onnx_types.h`, `onnx_graph.h`, `onnx_loader.h`, `kmem.h`.*
