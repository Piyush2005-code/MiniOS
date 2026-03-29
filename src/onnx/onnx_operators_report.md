# ONNX Runtime Operators Report

## Overview of Added Operators
We have expanded the capabilities of the MiniOS ONNX runtime from a basic set of 3 operators (Add, MatMul, ReLU) by adding 20 new operators across various categories. These operators were integrated seamlessly into the existing graph compilation and execution framework.

### Arithmetic Operations
- **Sub, Mul, Div**: Added naive element-wise implementations alongside the existing `Add` operator. Supports standard mathematical tensor computations.

### Activation Functions
- **Sigmoid**: Implemented using a custom fast exponential approximation for the bare-metal environment.
- **Tanh**: Implemented using the fast exponential function to approximate the hyperbolic tangent curve.
- **Softmax**: Implemented with basic numerical stability checks (subtracting max value before exponentiation) across 1D dimensions.
- **LeakyRelu**: Added as an additional operator, applying a small non-zero slope (default $\alpha=0.01$) for negative inputs.

### Shape Manipulations
- **Reshape, Flatten, Squeeze, Unsqueeze**: Implemented as memory copy (or pointer reassignment) operations. Since memory structures remain largely unchanged or entirely flattened, these mostly rely on tensor shape re-interpretation and memory blocks moving.
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

## Future Improvements & Optimizations

While the functional foundations of these operators are complete, their implementations are largely naive and targeted toward minimal verifiable functionality. The following architectural and performance improvements are needed:

### 1. Attribute Parsing Support
Presently, most complex operators (e.g. `Conv`, `MaxPool`, `Concat`) use hard-coded default parameters. The `onnx_loader.c` Protobuf parser must be enhanced to read `AttributeProto` fields.
- **Required**: Parsing kernel sizes, strides, pads, axis indices, and specific scalar attributes like $\alpha$ and $\beta$.

### 2. Broadcasting and Dynamic Shapes
- **Broadcasting**: Arithmetic operators currently assume identical total elements. Real ONNX models aggressively utilize NumPy-style broadcasting (e.g. adding a 1D bias vector to a 3D image tensor).
- **ND-Transpose and Concat**: Generalize dimension handling for `Transpose` and `Concat` beyond 1D and 2D arrays.

### 3. Mathematical Hardware Optimizations
- **Fast Math**: Hand-crafted math routines (`fast_exp`, `fast_tanh`, and square root) are suitable for early proofs-of-concept but lack the precision and bounds-checking required for large production models. Standard bare-metal numerical libraries should be linked or compiled.
- **SIMD / NEON**: Computations like `MatMul` and `Conv` are executed via $O(N^3)$ nested loops. They urgently require ARM64 NEON intrinsic implementations or standard im2col mapping techniques to execute efficiently on edge hardware.

### 4. Memory Optimizations (Zero-Copy)
- Operators like `Reshape`, `Flatten`, `Squeeze`, and `Unsqueeze` currently copy memory chunks. With an optimized memory manager, these can simply act as tensor aliases (creating a new `ONNX_Tensor` with an updated `shape` that shares the same underlying `data` pointer).

### 5. Multi-Type Support
- Currently, execution is restricted to `ONNX_DTYPE_FLOAT32`. Future patches must implement dispatch layers for `int8`, `int32`, and `float64`, which is specifically critical for the `Cast` operator and quantized model execution.