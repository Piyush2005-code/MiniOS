# ONNX Runtime for MiniOS

A complete, zero-dependency ONNX inference runtime for the MiniOS ARM64 bare-metal unikernel. Load and execute ONNX models with full control over graph construction, scheduling, and execution.

## 📊 Implementation Status

**Parser:** ✅ **100%** - Fully functional protobuf parser  
**Runtime:** 🟡 **~20%** - Basic operators implemented (3/18)  
**Overall:** 🟡 **~60%** - Core infrastructure complete, needs more operators

See [../../IMPLEMENTATION_STATUS.md](../../IMPLEMENTATION_STATUS.md) for detailed completeness analysis.

## 🎯 What You Can Do

✅ **Load ONNX models** - Parse .onnx files (embedded as C arrays via xxd)  
✅ **Build computation graphs** - Create nodes, tensors, and connections programmatically  
✅ **Run inference** - Execute full graphs, partial execution, or single nodes  
✅ **Custom scheduling** - Priority-based scheduling with dependency analysis  
✅ **Selective execution** - Run specific operators or subgraphs  
✅ **Performance profiling** - Track execution times and analyze bottlenecks  
✅ **Graph introspection** - Visualize, validate, and manipulate graph structure  
✅ **Simple neural networks** - MLPs with Linear layers and ReLU activation  

## 📁 Files

```
include/onnx/
  onnx_types.h         - Data types, operator enums (18 operators defined)
  onnx_graph.h         - Graph building and manipulation API
  onnx_runtime.h       - Inference execution engine
  onnx_loader.h        - Model loading (protobuf parser, C array embedding)
  onnx_loader_demo.h   - Model loading demo
  test_model.h         - Embedded test model (Y = X + B)

src/onnx/
  onnx_types.c         - Type utilities
  onnx_graph.c         - Graph implementation (~700 lines)
  onnx_runtime.c       - Runtime implementation (~450 lines, 3 operators working)
  onnx_loader.c        - Protobuf parser (~610 lines, FULLY FUNCTIONAL)
  onnx_loader_demo.c   - Model loading demo
  generate_onnx.py     - Python script to generate test ONNX models
  README.md            - This file
```

## 🚀 Quick Start

### Running the Model Loading Demo

```bash
cd MiniOS
make clean && make
./scripts/run.sh
```

This loads an embedded ONNX model (Y = X + B) and shows:
- Protobuf parsing in action
- Graph structure (1 node, 4 tensors)
- Dependency analysis
- Execution schedule generation

### Example Output

```
[ONNX Loader Demo] Loading embedded ONNX model...
[ONNX] Loading ONNX model (56 bytes)
[ONNX] IR version: 7
[ONNX] Graph size: 48 bytes
[ONNX] Graph parsed: 1 nodes, 4 tensors
[ONNX] Building dependencies...
[ONNX] Dependencies built: 1 nodes
[ONNX] Generating schedule...
[ONNX] Schedule generated: 1 nodes

Graph structure:
  add (Add)
    Inputs: X, B
    Outputs: Y
```

## 📖 Usage Guide

### Option A: Load from ONNX File (✅ WORKING)

**Step 1: Create an ONNX model**
```bash
cd src/onnx
python generate_onnx.py  # Creates simple_add.onnx
```

**Step 2: Convert to C array using xxd**
```bash
xxd -i simple_add.onnx > include/my_model.h
```

This creates a header like:
```c
unsigned char simple_add_onnx[] = {
  0x08, 0x07, 0x12, 0x30, 0x0a, 0x13, 0x08, 0x01, 0x1a, 0x03, 0x61, 0x64,
  // ... protobuf data
};
unsigned int simple_add_onnx_len = 56;
```

**Step 3: Load in kernel**
```c
#include "my_model.h"

ONNX_Graph graph;
ONNX_Graph_Init(&graph, "LoadedModel");

// Load embedded model
Status status = ONNX_LoadProtobuf(
    &graph,
    simple_add_onnx,      // From header file
    simple_add_onnx_len
);

if (status == STATUS_OK) {
    // Build dependencies and schedule
    ONNX_Graph_BuildDependencies(&graph);
    ONNX_Graph_GenerateSchedule(&graph);
    
    // Allocate memory and run inference
    // (see "Run Inference" section below)
}
```

**What Works:**
- ✅ Full protobuf parsing (ModelProto, GraphProto, NodeProto, TensorProto)
- ✅ Automatic tensor creation for inputs/outputs
- ✅ Initializer loading (weights/biases)
- ✅ Node creation with operator type detection
- ✅ Dependency building

**Current Limitations:**
- Only float32 dtype fully supported
- Attributes not yet parsed (needed for Conv stride, padding, etc.)
- Only 3 operators implemented (Add, MatMul, ReLU)

See [include/test_model.h](../../include/test_model.h) for a working example!

---

### Option B: Build Graph Manually

### 1. Create a Computation Graph

```c
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"

// Initialize graph
ONNX_Graph graph;
ONNX_Graph_Init(&graph, "MyModel");

// Define shapes
ONNX_TensorShape shape_input = {.ndim = 2, .dims = {1, 3}};
ONNX_TensorShape shape_output = {.ndim = 2, .dims = {1, 2}};

// Create tensors
ONNX_Tensor* input = ONNX_Graph_CreateTensor(&graph, "input", 
                                              ONNX_DTYPE_FLOAT32, &shape_input);
ONNX_Tensor* output = ONNX_Graph_CreateTensor(&graph, "output", 
                                               ONNX_DTYPE_FLOAT32, &shape_output);

// Set as graph input/output
graph.inputs[0] = input;
graph.num_inputs = 1;
graph.outputs[0] = output;
graph.num_outputs = 1;
```

### 2. Add Operators

```c
// Create a MatMul node
ONNX_Node* matmul = ONNX_Graph_AddNode(&graph, "matmul", ONNX_OP_MATMUL);
ONNX_Node_AddInput(matmul, input);
ONNX_Node_AddInput(matmul, weights);
ONNX_Node_AddOutput(matmul, output);

// Create a ReLU node
ONNX_Node* relu = ONNX_Graph_AddNode(&graph, "relu", ONNX_OP_RELU);
ONNX_Node_AddInput(relu, output);
ONNX_Node_AddOutput(relu, final_output);
```

### 3. Allocate Memory

```c
// Set up memory pool for tensors
static uint8_t memory_pool[64*1024];  // 64 KB
graph.tensor_memory_pool = memory_pool;
graph.tensor_memory_size = sizeof(memory_pool);

// Allocate all tensors
for (uint32_t i = 0; i < graph.num_tensors; i++) {
    ONNX_Graph_AllocateTensor(&graph, &graph.tensors[i]);
}
```

### 4. Schedule Execution

```c
// Build dependency graph
ONNX_Graph_BuildDependencies(&graph);

// Generate topological execution order
ONNX_Graph_GenerateSchedule(&graph);

// Or use custom scheduling with priorities
ONNX_Node_SetPriority(critical_node, 200);
ONNX_Graph_GenerateCustomSchedule(&graph);
```

### 5. Run Inference

```c
// Initialize runtime
ONNX_InferenceContext ctx;
ONNX_Runtime_Init(&ctx, &graph, 4096);

// Set input data
float* input_data = (float*)input->data;
input_data[0] = 1.0f;
input_data[1] = 2.0f;
input_data[2] = 3.0f;

// Run inference
ONNX_Tensor* inputs[1] = {input};
ONNX_Tensor* outputs[1];
ONNX_Runtime_Inference(&ctx, inputs, 1, outputs, 1);

// Get results
float* result = (float*)outputs[0]->data;
```

## 🎛️ Advanced Features

### Custom Scheduling

Schedule operations based on priorities while respecting dependencies:

```c
// Set priorities
ONNX_Node_SetPriority(important_op, 200);
ONNX_Node_SetPriority(normal_op, 100);
ONNX_Node_SetPriority(low_priority_op, 50);

// Generate schedule
ONNX_Graph_GenerateCustomSchedule(&graph);
```

### Selective Execution

Execute only part of the graph:

```c
// Execute up to a specific node
ONNX_Runtime_ExecuteUpTo(&ctx, "layer_3");

// Execute single node
ONNX_Runtime_ExecuteNode(&ctx, specific_node);
```

### Graph Introspection

```c
// Print graph structure
ONNX_Graph_Print(&graph);

// Validate graph
Status status = ONNX_Graph_Validate(&graph);

// Print performance stats
ONNX_Graph_PrintStats(&graph);
```

## 🔧 Supported Operators

### ✅ Working (3/18 = 17%)

| Operator | Status | Notes |
|----------|--------|-------|
| **Add** | ✅ | Element-wise addition, float32 only |
| **MatMul** | ✅ | Matrix multiplication (naive O(n³)) |
| **ReLU** | ✅ | max(0, x) activation |

### ⏳ Operators Requiring Implementation (15/18 = 83%)

| Category | Operators | Status | Priority |
|----------|-----------|--------|----------|
| **Arithmetic** | Sub, Mul, Div | Not implemented | Low |
| **Activations** | Sigmoid, Tanh, Softmax | Not implemented | High |
| **Convolution** | Conv2D | Stub exists | Critical |
| **Pooling** | MaxPool, AvgPool | Stub exists | Critical |
| **Shape Manipulation** | Reshape, Transpose, Flatten | Not implemented | Medium |
| **Normalization** | BatchNorm | Not implemented | High |
| **Linear Algebra** | GEMM | Not implemented | Medium |
| **Tensor Operations** | Concat | Not implemented | Medium |

These operators are defined in the type system but require implementation in the runtime engine. The operator infrastructure and dispatch mechanism are in place, making addition of new operators straightforward.

### Model Support Matrix

| Model Type | Current Support | Missing Operators |
|------------|----------------|-------------------|
| **Simple MLPs** | ✅ Fully supported | None |
| **Classification Networks** | Partial | Softmax |
| **Basic CNNs** | Not supported | Conv2D, MaxPool, Flatten |
| **Modern CNNs** | Not supported | Conv2D, BatchNorm, advanced pooling |
| **Residual Networks** | Not supported | Conv2D, BatchNorm, Concat |
| **Transformers** | Not supported | Attention, LayerNorm, GELU, Softmax |

## 📊 Data Types

| Type | Status | Notes |
|------|--------|-------|
| **float32** | ✅ | Fully supported in all operators |
| **int8, int32, int64** | ❌ | Defined but operators don't use |
| **uint8, uint16** | ❌ | Defined but operators don't use |
| **float64** | ❌ | Defined but operators don't use |

**Missing Features:**
- ❌ Broadcasting (needed for most real models)
- ❌ Dynamic shapes (all shapes must be static)
- ❌ Quantization (int8 inference)

## 🎯 Example Use Cases

### 1. Inference Optimization

```c
// Profile your model
ONNX_Runtime_Inference(&ctx, inputs, 1, outputs, 1);
ONNX_Runtime_PrintProfile(&ctx);

// Adjust priorities based on profiling
ONNX_Node_SetPriority(slow_node, 50);  // Lower priority
ONNX_Node_SetPriority(fast_node, 150); // Higher priority
ONNX_Graph_GenerateCustomSchedule(&graph);
```

### 2. Debugging

```c
// Execute step-by-step
for (uint32_t i = 0; i < graph.schedule_length; i++) {
    ONNX_Node* node = graph.exec_schedule[i];
    
    HAL_UART_PutString("Executing: ");
    HAL_UART_PutString(node->name);
    HAL_UART_PutString("\n");
    
    ONNX_Runtime_ExecuteNode(&ctx, node);
    
    // Inspect intermediate results
    // ...
}
```

### 3. Graph Manipulation

```c
// Experiment with different operator orderings
uint32_t schedule_v1[] = {0, 1, 2, 3};
uint32_t schedule_v2[] = {0, 2, 1, 3};

ONNX_Graph_SetCustomSchedule(&graph, schedule_v1, 4);
// Run and measure...

ONNX_Graph_SetCustomSchedule(&graph, schedule_v2, 4);
// Run and compare...
```

## �️ Development Roadmap

### ✅ Phase 1: Core Infrastructure (Complete)
- ✅ Graph building and manipulation API
- ✅ Tensor management with static allocation
- ✅ Dependency analysis using Kahn's algorithm
- ✅ Topological sorting for execution scheduling
- ✅ Priority-based custom scheduling
- ✅ Basic operators: Add, MatMul, ReLU
- ✅ ONNX Protobuf parser (fully functional)
- ✅ C array embedding for model loading
- ✅ Inference execution engine

### Phase 2: Essential Operators

**Arithmetic Operations**
- [ ] Subtraction (Sub)
- [ ] Multiplication (Mul)
- [ ] Division (Div)

**Activation Functions**
- [ ] Sigmoid
- [ ] Hyperbolic Tangent (Tanh)
- [ ] Softmax

**Shape Manipulation**
- [ ] Reshape
- [ ] Flatten

Completion of Phase 2 will enable execution of standard feedforward neural networks with multiple activation functions.

### Phase 3: Convolutional Neural Network Support

**Convolution Operations**
- [ ] 2D Convolution (Conv2D) with im2col optimization
- [ ] Attribute parsing for kernel_shape, strides, padding

**Pooling Operations**
- [ ] Max Pooling (MaxPool)
- [ ] Average Pooling (AvgPool)

**Normalization**
- [ ] Batch Normalization (BatchNorm)

**Additional Shape Operations**
- [ ] Transpose

Completion of Phase 3 will enable execution of convolutional neural networks including architectures like LeNet-5.

### Phase 4: Performance Optimizations

**SIMD Acceleration**
- [ ] NEON intrinsics for ARM64
- [ ] Vectorized element-wise operations
- [ ] Optimized matrix multiplication

**Tensor Operations**
- [ ] Broadcasting support for element-wise operations
- [ ] Multi-dimensional indexing optimizations

**Type System**
- [ ] Integer type support (int8, int32, int64)
- [ ] Quantized inference (int8)
- [ ] Type conversion operators

**Graph Optimization**
- [ ] Constant folding
- [ ] Operator fusion
- [ ] Dead code elimination
- [ ] Memory reuse analysis

Completion of Phase 4 will provide production-ready performance for embedded ML inference.

### Phase 5: Advanced Features

**Dynamic Execution**
- [ ] Dynamic shape support
- [ ] Control flow operators (If, Loop)

**Advanced Operators**
- [ ] Attention mechanisms
- [ ] Layer Normalization
- [ ] GELU activation
- [ ] Recurrent operators (LSTM, GRU)

**Infrastructure**
- [ ] Model caching and serialization
- [ ] Quantization-aware training support
- [ ] Runtime code generation

Phase 5 represents advanced capabilities for complex model architectures.

## 📚 API Reference

### Core APIs

**Graph Management:**
- `ONNX_Graph_Init()` - Initialize a graph
- `ONNX_Graph_CreateTensor()` - Create tensors
- `ONNX_Graph_AddNode()` - Add operators
- `ONNX_Graph_BuildDependencies()` - Analyze dependencies (Kahn's algorithm)
- `ONNX_Graph_GenerateSchedule()` - Topological execution order
- `ONNX_Graph_GenerateCustomSchedule()` - Priority-based scheduling

**Model Loading (✅ WORKING):**
- `ONNX_LoadProtobuf()` - Load ONNX protobuf format from C array
- `ONNX_LoadEmbedded()` - Wrapper for embedded models
- `ONNX_LoadCustomBinary()` - Custom binary format (stub only)

**Runtime Execution:**
- `ONNX_Runtime_Init()` - Initialize runtime
- `ONNX_Runtime_Inference()` - Run full inference
- `ONNX_Runtime_ExecuteNode()` - Execute single operator
- `ONNX_Runtime_ExecuteUpTo()` - Partial execution

**Introspection:**
- `ONNX_Graph_Print()` - Print graph structure
- `ONNX_Graph_Validate()` - Validate graph
- `ONNX_Graph_PrintStats()` - Print statistics
- `ONNX_Runtime_PrintProfile()` - Print execution profile

### Documentation

See header files for complete API documentation:
- [onnx_types.h](../../include/onnx/onnx_types.h) - Data structures, operator enums
- [onnx_graph.h](../../include/onnx/onnx_graph.h) - Graph building API
- [onnx_runtime.h](../../include/onnx/onnx_runtime.h) - Inference execution
- [onnx_loader.h](../../include/onnx/onnx_loader.h) - Model loading (protobuf parser)

### Key Resources

- [IMPLEMENTATION_STATUS.md](../../IMPLEMENTATION_STATUS.md) - **Detailed completeness analysis**
- [test_model.h](../../include/test_model.h) - Working embedded model example
- [generate_onnx.py](generate_onnx.py) - Generate test ONNX models

## 🔬 Operator Implementation Guide

### Understanding the Operator Infrastructure

The runtime uses a dispatch-based architecture where each operator is implemented as a separate function that operates on ONNX tensors. The infrastructure handles scheduling, memory management, and graph traversal automatically.

### Current Operator Implementations

**1. Element-wise Operations (Add, ReLU)**

These are the simplest operators - they apply the same operation independently to each element:

```c
// Add: C = A + B (element-wise)
Status ONNX_Execute_Add(ONNX_Node* node) {
    ONNX_Tensor* A = node->inputs[0];
    ONNX_Tensor* B = node->inputs[1];
    ONNX_Tensor* C = node->outputs[0];
    
    // Validation
    if (!A->data || !B->data || !C->data) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    // Shape compatibility check (should match)
    if (A->total_elements != B->total_elements) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    // Element-wise addition
    float* a_data = (float*)A->data;
    float* b_data = (float*)B->data;
    float* c_data = (float*)C->data;
    
    for (uint32_t i = 0; i < A->total_elements; i++) {
        c_data[i] = a_data[i] + b_data[i];
    }
    
    return STATUS_OK;
}

// ReLU: y = max(0, x)
Status ONNX_Execute_ReLU(ONNX_Node* node) {
    ONNX_Tensor* input = node->inputs[0];
    ONNX_Tensor* output = node->outputs[0];
    
    float* in = (float*)input->data;
    float* out = (float*)output->data;
    
    for (uint32_t i = 0; i < input->total_elements; i++) {
        out[i] = (in[i] > 0.0f) ? in[i] : 0.0f;
    }
    
    return STATUS_OK;
}
```

**2. Matrix Operations (MatMul)**

These require understanding tensor shapes and performing structured iteration:

```c
// MatMul: C = A @ B (matrix multiplication)
Status ONNX_Execute_MatMul(ONNX_Node* node) {
    ONNX_Tensor* A = node->inputs[0];
    ONNX_Tensor* B = node->inputs[1];
    ONNX_Tensor* C = node->outputs[0];
    
    // Extract dimensions: A is [M x K], B is [K x N], C is [M x N]
    uint32_t M = A->shape.dims[0];
    uint32_t K = A->shape.dims[1];
    uint32_t N = B->shape.dims[1];
    
    // Validate: A's columns must match B's rows
    if (A->shape.dims[1] != B->shape.dims[0]) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    float* a = (float*)A->data;
    float* b = (float*)B->data;
    float* c = (float*)C->data;
    
    // Initialize output to zero
    mem_zero(c, C->total_elements * sizeof(float));
    
    // Naive O(n³) matrix multiplication
    for (uint32_t i = 0; i < M; i++) {
        for (uint32_t j = 0; j < N; j++) {
            for (uint32_t k = 0; k < K; k++) {
                c[i * N + j] += a[i * K + k] * b[k * N + j];
            }
        }
    }
    
    return STATUS_OK;
}
```

### Step-by-Step Guide: Implementing a New Operator

#### Step 1: Add Operator Type Definition

In `include/onnx/onnx_types.h`, add your operator to the enum:

```c
typedef enum {
    ONNX_OP_UNKNOWN = 0,
    ONNX_OP_ADD,
    ONNX_OP_MATMUL,
    ONNX_OP_RELU,
    // ... existing operators ...
    ONNX_OP_YOUR_NEW_OP,  // Add here
} ONNX_OperatorType;
```

#### Step 2: Map String Name to Operator Type

In `src/onnx/onnx_loader.c`, update the `parse_operator_type()` function:

```c
static ONNX_OperatorType parse_operator_type(const char* op_type_str) {
    if (str_equal(op_type_str, "Add")) return ONNX_OP_ADD;
    if (str_equal(op_type_str, "MatMul")) return ONNX_OP_MATMUL;
    // ... existing mappings ...
    if (str_equal(op_type_str, "YourNewOp")) return ONNX_OP_YOUR_NEW_OP;
    return ONNX_OP_UNKNOWN;
}
```

#### Step 3: Implement the Operator Function

In `src/onnx/onnx_runtime.c`, implement the execution function:

```c
Status ONNX_Execute_YourNewOp(ONNX_Node* node) {
    // 1. Extract input/output tensors
    ONNX_Tensor* input = node->inputs[0];
    ONNX_Tensor* output = node->outputs[0];
    
    // 2. Validate inputs
    if (!input->data || !output->data) {
        HAL_UART_PutString("[ERROR] Null tensor data\n");
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    // 3. Validate shapes (if needed)
    if (input->shape.ndim != 2) {
        HAL_UART_PutString("[ERROR] Expected 2D tensor\n");
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    // 4. Cast data pointers
    float* in_data = (float*)input->data;
    float* out_data = (float*)output->data;
    
    // 5. Implement the operation
    for (uint32_t i = 0; i < input->total_elements; i++) {
        out_data[i] = /* your computation */;
    }
    
    return STATUS_OK;
}
```

#### Step 4: Add Dispatcher Case

In `src/onnx/onnx_runtime.c`, add the case to `ONNX_Runtime_ExecuteNode()`:

```c
Status ONNX_Runtime_ExecuteNode(ONNX_InferenceContext* ctx, ONNX_Node* node) {
    switch (node->op_type) {
        case ONNX_OP_ADD:
            return ONNX_Execute_Add(node);
        case ONNX_OP_MATMUL:
            return ONNX_Execute_MatMul(node);
        // ... existing cases ...
        case ONNX_OP_YOUR_NEW_OP:
            return ONNX_Execute_YourNewOp(node);
        default:
            return STATUS_ERROR_NOT_SUPPORTED;
    }
}
```

#### Step 5: Testing

Create a simple test in `src/onnx/onnx_loader_demo.c` or manually:

```c
void test_your_new_op(void) {
    ONNX_Graph graph;
    ONNX_Graph_Init(&graph, "TestGraph");
    
    // Create test tensors
    ONNX_TensorShape shape = {.ndim = 1, .dims = {4}};
    ONNX_Tensor* input = ONNX_Graph_CreateTensor(&graph, "in", 
                                                  ONNX_DTYPE_FLOAT32, &shape);
    ONNX_Tensor* output = ONNX_Graph_CreateTensor(&graph, "out",
                                                   ONNX_DTYPE_FLOAT32, &shape);
    
    // Create node
    ONNX_Node* node = ONNX_Graph_AddNode(&graph, "test", ONNX_OP_YOUR_NEW_OP);
    ONNX_Node_AddInput(node, input);
    ONNX_Node_AddOutput(node, output);
    
    // Allocate memory
    static uint8_t pool[4096];
    graph.tensor_memory_pool = pool;
    graph.tensor_memory_size = sizeof(pool);
    ONNX_Graph_AllocateTensor(&graph, input);
    ONNX_Graph_AllocateTensor(&graph, output);
    
    // Set input data
    float* in_data = (float*)input->data;
    in_data[0] = 1.0f; in_data[1] = 2.0f;
    in_data[2] = 3.0f; in_data[3] = 4.0f;
    
    // Execute
    Status status = ONNX_Execute_YourNewOp(node);
    if (status == STATUS_OK) {
        float* out_data = (float*)output->data;
        HAL_UART_PutString("Output: ");
        // Print results...
    }
}
```

### Common Implementation Patterns

**Pattern 1: Arithmetic Operations (Sub, Mul, Div)**
```c
// Template for binary element-wise ops
Status ONNX_Execute_BinaryOp(ONNX_Node* node, float (*op)(float, float)) {
    ONNX_Tensor* A = node->inputs[0];
    ONNX_Tensor* B = node->inputs[1];
    ONNX_Tensor* C = node->outputs[0];
    
    float* a = (float*)A->data;
    float* b = (float*)B->data;
    float* c = (float*)C->data;
    
    for (uint32_t i = 0; i < A->total_elements; i++) {
        c[i] = op(a[i], b[i]);
    }
    return STATUS_OK;
}
```

**Pattern 2: Activation Functions**
```c
// Sigmoid: 1 / (1 + exp(-x))
Status ONNX_Execute_Sigmoid(ONNX_Node* node) {
    ONNX_Tensor* input = node->inputs[0];
    ONNX_Tensor* output = node->outputs[0];
    
    float* in = (float*)input->data;
    float* out = (float*)output->data;
    
    for (uint32_t i = 0; i < input->total_elements; i++) {
        // Need to implement fast_exp() for bare-metal
        out[i] = 1.0f / (1.0f + fast_exp(-in[i]));
    }
    return STATUS_OK;
}
```

**Pattern 3: Reduction Operations (Softmax)**
```c
// Softmax: exp(x_i) / sum(exp(x_j)) for all j
Status ONNX_Execute_Softmax(ONNX_Node* node) {
    ONNX_Tensor* input = node->inputs[0];
    ONNX_Tensor* output = node->outputs[0];
    
    float* in = (float*)input->data;
    float* out = (float*)output->data;
    uint32_t n = input->total_elements;
    
    // 1. Find max for numerical stability
    float max_val = in[0];
    for (uint32_t i = 1; i < n; i++) {
        if (in[i] > max_val) max_val = in[i];
    }
    
    // 2. Compute exp(x - max) and sum
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = fast_exp(in[i] - max_val);
        sum += out[i];
    }
    
    // 3. Normalize
    for (uint32_t i = 0; i < n; i++) {
        out[i] /= sum;
    }
    
    return STATUS_OK;
}
```

### Implementing Complex Operators

**Conv2D Implementation Strategy**

Convolution is complex and requires:

1. **Attribute Parsing** (currently not implemented)
   - kernel_shape, strides, pads, dilations, group
   - Need to extend parser to handle attributes

2. **Im2Col Transformation**
   - Convert convolution to matrix multiplication
   - More efficient than naive nested loops
   - Requires temporary buffer

3. **Memory Management**
   - Im2col buffer can be large (input_h * input_w * kernel_h * kernel_w * channels)
   - May need dynamic allocation or static scratch buffer

**Skeleton Implementation:**

```c
Status ONNX_Execute_Conv(ONNX_Node* node) {
    // Inputs: [N, C_in, H_in, W_in], [C_out, C_in, K_h, K_w], [C_out] (bias)
    // Output: [N, C_out, H_out, W_out]
    
    // TODO: Parse attributes (stride, padding, etc.)
    // TODO: Implement im2col transformation
    // TODO: Call MatMul on transformed input
    // TODO: Add bias
    // TODO: Reshape to output dimensions
    
    return STATUS_ERROR_NOT_SUPPORTED;
}
```

### Operator Implementation Checklist

- [ ] Add operator type to enum
- [ ] Map operator string name to type
- [ ] Implement execution function
- [ ] Add validation (null checks, shape checks)
- [ ] Handle edge cases (empty tensors, dimension mismatches)
- [ ] Add dispatcher case
- [ ] Test with simple inputs
- [ ] Verify with ONNX model
- [ ] Profile performance
- [ ] Document any limitations

## 💡 Technical Details

### Architecture
- **Zero dependencies** - No libc, no stdlib, no OS
- **Static allocation** - Bump allocator, 64KB tensor pool
- **Bare-metal ARM64** - Runs on QEMU virt machine (Cortex-A53)
- **Binary size** - ~25KB kernel including full ONNX runtime

### Memory Layout
```
graph.tensor_memory_pool (64KB)
├── Tensor 0 data (aligned to 64 bytes)
├── Tensor 1 data (aligned to 64 bytes)
└── ... (bump allocator)
```

### Protobuf Parser Architecture

The ONNX protobuf parser is a custom, zero-dependency implementation that parses the ONNX binary format without requiring Google's protobuf library or any external dependencies.

#### Parser Implementation Details

**1. Low-Level Primitives (`onnx_loader.c`)**

```c
// Varint Decoding - ONNX uses Protocol Buffers varint encoding
static Status proto_read_varint(const uint8_t* data, uint32_t* pos, 
                                uint32_t len, uint64_t* value) {
    // Reads variable-length integers (1-10 bytes)
    // Each byte: [continuation_bit:1][data:7]
    // MSB=1 means more bytes follow, MSB=0 means last byte
}

// Tag Reading - Each field has a tag (field_number << 3 | wire_type)
static Status proto_read_tag(const uint8_t* data, uint32_t* pos,
                             uint32_t len, uint32_t* field_number,
                             uint32_t* wire_type) {
    // Wire types:
    // 0: Varint (int32, int64, bool, enum)
    // 1: 64-bit (fixed64, double)
    // 2: Length-delimited (string, bytes, embedded messages)
    // 5: 32-bit (fixed32, float)
}

// Length-Delimited Fields - Strings, bytes, nested messages
static Status proto_read_string(const uint8_t* data, uint32_t* pos,
                                uint32_t len, char* str, uint32_t max_len) {
    // 1. Read length as varint
    // 2. Copy 'length' bytes to output buffer
    // 3. Null-terminate string
}

static Status proto_read_bytes(const uint8_t* data, uint32_t* pos,
                               uint32_t len, uint8_t** out_data,
                               uint32_t* out_len) {
    // Similar to string but for binary data (tensor raw_data)
}
```

**2. Message Parsing - Hierarchical Structure**

```c
// TensorProto - Contains tensor metadata and data
static Status proto_parse_tensor(ONNX_Graph* graph,
                                 const uint8_t* data,
                                 uint32_t* pos, uint32_t len) {
    // Field 1: dims (repeated int64) - tensor shape
    // Field 2: data_type (int32) - FLOAT=1, INT8=3, INT32=6, etc.
    // Field 8: name (string) - tensor identifier
    // Field 9: raw_data (bytes) - actual tensor data
    
    // Parse loop:
    while (*pos < len) {
        uint32_t field_num, wire_type;
        proto_read_tag(data, pos, len, &field_num, &wire_type);
        
        switch(field_num) {
            case 1: // dims
                proto_read_varint(...); // Read dimension
                break;
            case 2: // data_type
                proto_read_varint(...); // Read dtype
                break;
            case 8: // name
                proto_read_string(...); // Read tensor name
                break;
            case 9: // raw_data
                proto_read_bytes(...); // Read tensor data
                break;
        }
    }
}

// NodeProto - Represents an operator in the computation graph
static Status proto_parse_node(ONNX_Graph* graph,
                               const uint8_t* data,
                               uint32_t* pos, uint32_t len) {
    // Field 1: input (repeated string) - input tensor names
    // Field 2: output (repeated string) - output tensor names  
    // Field 4: name (string) - node identifier
    // Field 7: op_type (string) - "Add", "MatMul", "Conv", etc.
    
    // Key feature: Auto-creates missing tensors
    // If input/output tensor doesn't exist, creates placeholder
    // This handles forward references in the graph
}

// GraphProto - Top-level computation graph
static Status proto_parse_graph(ONNX_Graph* graph,
                               const uint8_t* data,
                               uint32_t* pos, uint32_t len) {
    // Field 1: node (repeated NodeProto) - computation nodes
    // Field 5: initializer (repeated TensorProto) - weights/biases
    // Field 11: input (repeated ValueInfoProto) - graph inputs
    // Field 12: output (repeated ValueInfoProto) - graph outputs
    
    // Parsing strategy:
    // 1. Parse all nodes first (creates tensor placeholders)
    // 2. Parse initializers (fills in weight data)
    // 3. Mark graph inputs/outputs
}
```

**3. Main Entry Point**

```c
Status ONNX_LoadProtobuf(ONNX_Graph* graph,
                        const uint8_t* data,
                        uint32_t data_len) {
    // 1. Parse ModelProto outer wrapper
    //    Field 1: ir_version (int64)
    //    Field 7: graph (GraphProto)
    
    // 2. Extract and parse GraphProto
    proto_parse_graph(graph, graph_data, &pos, graph_len);
    
    // 3. Build dependency graph
    //    - For each node, find producer nodes of its inputs
    //    - Build adjacency list representation
    //    - Used for topological sorting
    
    ONNX_Graph_BuildDependencies(graph);
    
    // 4. Generate execution schedule
    //    - Topological sort using Kahn's algorithm
    //    - Ensures dependencies execute before consumers
    
    ONNX_Graph_GenerateSchedule(graph);
    
    return STATUS_OK;
}
```

**4. Key Design Decisions**

- **Zero-copy where possible**: Tensor raw_data points directly into the embedded array
- **No dynamic allocation**: Uses graph's pre-allocated tensor and node arrays
- **Defensive parsing**: Checks buffer bounds at every read operation
- **Graceful degradation**: Skips unknown fields using `proto_skip_field()`
- **Auto-healing**: Creates missing tensors automatically (forward references)

**5. Current Limitations**

- **Attributes not parsed**: Conv stride/padding, Pool kernel_size require attribute parsing
- **No type coercion**: Only float32 fully supported, no automatic type conversion
- **Static shapes only**: All tensor dimensions must be known at load time
- **No graph optimization**: Loads graph as-is, no constant folding or operator fusion

### Performance
- MatMul: O(n³) naive implementation (~500 cycles for 3x3)
- No SIMD optimizations yet (NEON could give 4-8x speedup)
- No operator fusion or graph optimization

## 📊 Comparison to Standard ONNX Runtime

| Feature | MiniOS ONNX | ONNX Runtime |
|---------|-------------|--------------|
| Binary Size | ~25 KB | 50-100 MB |
| Dependencies | 0 | C++ STL, Protobuf, OS |
| Memory | 64 KB static | Dynamic (GBs) |
| Operators | 3 working | 150+ |
| Data Types | 1 (float32) | 10+ |
| Platforms | Bare-metal ARM64 | Any OS |
| Use Case | Embedded/Edge | General purpose |

## 📄 License

Part of MiniOS - ARM64 Bare-Metal Unikernel for ML Inference

---

## 📋 Summary

### Current Capabilities

- ONNX model loading via protobuf parser
- Computation graph construction and manipulation
- Dependency-aware execution scheduling
- Simple neural network inference (Linear layers with ReLU activation)
- Performance profiling and graph introspection
- Zero-dependency bare-metal execution

### Implementation Requirements

The following components require implementation:

- 15 additional operators (arithmetic, activations, convolution, pooling)
- Broadcasting support for tensor operations
- Attribute parsing for parameterized operators
- Multi-dtype support beyond float32
- SIMD optimizations for ARM64 NEON

### Technical Characteristics

- **Binary Size:** ~25KB including full ONNX runtime
- **Memory Footprint:** 64KB static allocation
- **Dependencies:** Zero external dependencies
- **Platform:** ARM64 bare-metal (QEMU Cortex-A53)
- **Language:** C99 with no standard library

---

**Detailed Implementation Status:** [IMPLEMENTATION_STATUS.md](../../IMPLEMENTATION_STATUS.md)  
**Model Loading Example:** [test_model.h](../../include/test_model.h)  
**Operator Implementation Guide:** See "Operator Implementation Guide" section above
