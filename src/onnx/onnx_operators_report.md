# ONNX Parser & Runtime Operations Report

This document details the architecture and implementation of the ONNX parser and computation graph execution engine built for the MiniOS bare-metal environment. It serves as both a report of recently added operators and a comprehensive guide for developers looking to understand or reimplement a lightweight ONNX parser from scratch.

---

## 1. How the ONNX Parser Works

ONNX files are serialized Protocol Buffers (protobufs). To avoid the heavy footprint of the official Google protobuf C++ library on a bare-metal unikernel, we implemented a custom, zero-dependency protobuf parser specifically tuned for ONNX.

### The Protobuf Wire Format
Protobuf is a binary format that stores data as a sequence of key-value pairs. Each field consists of:
1. **A Tag**: A variable-length integer (Varint) that encodes both the *Field Number* and the *Wire Type*.
   `Tag = (Field_Number << 3) | Wire_Type`
2. **The Data**: The format of the data depends on the Wire Type (e.g., Varint, 32-bit fixed, 64-bit fixed, or length-delimited for strings/bytes/nested messages).

### The Parsing Strategy
Our parser iterates through the binary buffer, reading the Tag, decoding the field number, and acting accordingly. If a field number is unknown, the parser uses the Wire Type to safely skip the data and continue to the next field.

The structure of an ONNX file is strictly hierarchical:
1. **ModelProto**: The root message. We look for field `7` (`graph`), which contains the `GraphProto`.
2. **GraphProto**: Defines the overall computation. We iterate over its fields:
   - Field `1`: `node` (repeated `NodeProto`) -> The actual operations.
   - Field `5`: `initializer` (repeated `TensorProto`) -> Constant weights and biases.
   - Field `11/12`: `input`/`output` (repeated `ValueInfoProto`) -> Overall graph inputs and outputs.
3. **NodeProto**: Defines an operator.
   - Field `1`: `input` (strings) -> Names of input tensors.
   - Field `2`: `output` (strings) -> Names of output tensors.
   - Field `4`: `op_type` (string) -> The operation to perform (e.g., "Add", "Conv").
4. **TensorProto**: Defines data arrays (weights).
   - Field `1`: `dims` (repeated int64) -> Tensor shape.
   - Field `2`: `data_type` -> float32, int8, etc.
   - Field `8`: `name` -> Identifier.
   - Field `9`: `raw_data` -> The raw binary byte array of the weights.

### Building the Executable Graph
As the parser traverses the protobuf fields, it simultaneously builds an executable data structure:
1. **Tensors as Shared Memory Entities**: When a `NodeProto` mentions an input/output string name, the parser checks if an `ONNX_Tensor` with that name exists. If not, it creates an empty placeholder.
2. **Filling Data**: When an `initializer` (TensorProto) is parsed, it finds its placeholder and populates it with the actual shape and memory-copies the `raw_data` into a kernel memory arena.
3. **Nodes and Edges**: Each parsed node maintains pointers to its input and output `ONNX_Tensor` structs. This inherently forms a Directed Acyclic Graph (DAG) in memory.

### Dependency Analysis and Scheduling
Once the graph is parsed, we must determine the order of execution:
1. **Kahn's Algorithm (Topological Sort)**:
   - We iterate over all nodes and count their "in-degree" (how many *other nodes* produce their input tensors). Tensors that come from `initializers` or graph `inputs` don't add to the in-degree.
   - Nodes with an in-degree of `0` are added to the execution schedule.
   - We then decrement the in-degree of all nodes that depend on the newly scheduled node.
   - This repeats until all nodes are scheduled. The result is a linear array of nodes guaranteed to execute in the correct mathematical order.

---

## 2. Overview of Added Operators
Building upon the parser and scheduling engine, we recently expanded the runtime from 3 to 23 operators. These are mapped during `NodeProto` parsing and dispatched in the execution engine.

### Arithmetic Operations
- **Sub, Mul, Div**: Added naive element-wise implementations alongside the existing `Add` operator. Supports standard mathematical tensor computations.

### Activation Functions
- **Sigmoid**: Implemented using a custom fast exponential approximation for the bare-metal environment.
- **Tanh**: Implemented using the fast exponential function to approximate the hyperbolic tangent curve.
- **Softmax**: Implemented with basic numerical stability checks (subtracting max value before exponentiation) across 1D dimensions.
- **LeakyRelu**: Added as an additional operator, applying a small non-zero slope (default $\alpha=0.01$) for negative inputs.

### Shape Manipulations
- **Reshape, Flatten, Squeeze, Unsqueeze**: Implemented as memory copy operations. Since memory structures remain largely unchanged or entirely flattened, these mostly rely on tensor shape re-interpretation and memory blocks moving.
- **Transpose**: Implemented strictly for 2D matrix transposition.

### Convolution & Pooling (CNN)
- **Conv**: Added a fundamental, naive nested-loop implementation for 2D convolutions supporting inputs `[1, C_in, H, W]` and weights `[C_out, C_in, K_h, K_w]`.
- **MaxPool, AvgPool**: Implemented naive 2x2 window pooling loops without dynamic attribute-driven strides or pads (defaults to $2 \times 2$ operations).
- **GlobalAveragePool**: Computes the mean across all spatial dimensions per channel.

### Complex / Extra Operations
- **BatchNorm**: Implemented naive batch normalization leveraging standard scale, bias, mean, and variance inputs. Includes a basic Newton-Raphson inverse square root approximation since no `math.h` is available.
- **GEMM**: General matrix multiplication implementing $Y = \alpha AB + \beta C$.
- **Concat**: Implemented a very basic zero-axis (axis=0) concatenation routine.
- **Cast**: Currently acts as a pass-through layer ensuring Float-to-Float compatibility.

---

## 3. Future Improvements & Optimizations

While the functional foundations of the parser and execution engine are complete, their implementations are targeted toward minimal verifiable functionality. The following architectural and performance improvements are needed:

### 1. Attribute Parsing Support
Presently, most complex operators (e.g. `Conv`, `MaxPool`, `Concat`) use hard-coded default parameters. The `onnx_loader.c` Protobuf parser must be enhanced to read `AttributeProto` fields (Field `5` inside `NodeProto`).
- **Required**: Parsing kernel sizes, strides, pads, axis indices, and specific scalar attributes like $\alpha$ and $\beta$.

### 2. Broadcasting and Dynamic Shapes
- **Broadcasting**: Arithmetic operators currently assume identical total elements. Real ONNX models aggressively utilize NumPy-style broadcasting (e.g. adding a 1D bias vector to a 3D image tensor).
- **ND-Transpose and Concat**: Generalize dimension handling for `Transpose` and `Concat` beyond 1D and 2D arrays.

### 3. Mathematical Hardware Optimizations
- **Fast Math**: Hand-crafted math routines (`fast_exp`, `fast_tanh`, and square root) are suitable for early proofs-of-concept but lack the precision and bounds-checking required for large production models.
- **SIMD / NEON**: Computations like `MatMul` and `Conv` are executed via $O(N^3)$ nested loops. They urgently require ARM64 NEON intrinsic implementations or standard im2col mapping techniques to execute efficiently on edge hardware.

### 4. Memory Optimizations (Zero-Copy)
- Operators like `Reshape`, `Flatten`, `Squeeze`, and `Unsqueeze` currently copy memory chunks. With an optimized memory manager, these can simply act as tensor aliases (creating a new `ONNX_Tensor` with an updated `shape` that points to the exact same `data` pointer as the input).

### 5. Multi-Type Support
- Currently, execution is restricted to `ONNX_DTYPE_FLOAT32`. Future patches must implement dispatch layers for `int8`, `int32`, and `float64`, which is specifically critical for the `Cast` operator and quantized model execution.