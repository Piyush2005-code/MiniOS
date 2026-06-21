#!/usr/bin/env python3
"""
export_alexnet_tiny.py
----------------------
Exports a tiny AlexNet-architecture ONNX model for MiniOS benchmarking.

Architecture (verified shapes at each step):
  Input : [1, 3, 32, 32]
  conv1 : 16 filters, 3×3, pad=[1,1,1,1]  → [1, 16, 32, 32]
  relu1
  pool1 : 2×2, stride=2, no pads          → [1, 16, 16, 16]
  conv2 : 32 filters, 3×3, pad=[1,1,1,1]  → [1, 32, 16, 16]
  relu2
  pool2 : 2×2, stride=2, no pads          → [1, 32,  8,  8]
  conv3 : 32 filters, 3×3, pad=[1,1,1,1]  → [1, 32,  8,  8]
  relu3
  conv4 : 16 filters, 3×3, pad=[1,1,1,1]  → [1, 16,  8,  8]
  relu4
  pool3 : 2×2, stride=2, no pads          → [1, 16,  4,  4]
  flatten                                  → [1, 256]
  fc1   : 256 → 128,  relu
  fc2   : 128 →  64,  relu
  fc3   :  64 →  10
  softmax

Key design choices:
  - ONLY symmetric pads [1,1,1,1] for Conv (top=bottom=left=right=1)
  - NO pads for MaxPool (clean integer division)
  - NO LRN layers (simplifies runtime, removes shape-propagation risk)
  - Power-of-2 spatial dimensions throughout: 32→16→8→4
  - fc1 weight shape [128,256] matches flatten output 16×4×4=256 exactly
"""

import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper


def make_conv(name, input_name, weight_data, bias_data, kernel_shape,
              strides=(1, 1), pads=(0, 0, 0, 0), group=1):
    w_name = f"{name}_W"
    b_name = f"{name}_B"
    out_name = f"{name}_out"

    w_init = numpy_helper.from_array(weight_data.astype(np.float32), name=w_name)
    b_init = numpy_helper.from_array(bias_data.astype(np.float32), name=b_name)

    attrs = dict(kernel_shape=kernel_shape, strides=list(strides), pads=list(pads))
    if group > 1:
        attrs['group'] = group

    node = helper.make_node(
        "Conv",
        inputs=[input_name, w_name, b_name],
        outputs=[out_name],
        name=name,
        **attrs,
    )
    return node, [w_init, b_init], out_name


def make_relu(name, input_name):
    out_name = f"{name}_out"
    node = helper.make_node("Relu", inputs=[input_name], outputs=[out_name], name=name)
    return node, out_name


def make_maxpool(name, input_name, kernel_shape, strides):
    """MaxPool with NO padding — guarantees clean integer division."""
    out_name = f"{name}_out"
    node = helper.make_node(
        "MaxPool",
        inputs=[input_name],
        outputs=[out_name],
        name=name,
        kernel_shape=kernel_shape,
        strides=strides,
    )
    return node, out_name


def make_gemm(name, input_name, weight_data, bias_data, transB=1):
    w_name = f"{name}_W"
    b_name = f"{name}_B"
    out_name = f"{name}_out"

    w_init = numpy_helper.from_array(weight_data.astype(np.float32), name=w_name)
    b_init = numpy_helper.from_array(bias_data.astype(np.float32), name=b_name)

    node = helper.make_node(
        "Gemm",
        inputs=[input_name, w_name, b_name],
        outputs=[out_name],
        name=name,
        transB=transB,
        alpha=1.0,
        beta=1.0,
    )
    return node, [w_init, b_init], out_name


def make_flatten(name, input_name, axis=1):
    out_name = f"{name}_out"
    node = helper.make_node(
        "Flatten",
        inputs=[input_name],
        outputs=[out_name],
        name=name,
        axis=axis,
    )
    return node, out_name


def make_softmax(name, input_name):
    out_name = f"{name}_out"
    node = helper.make_node(
        "Softmax",
        inputs=[input_name],
        outputs=[out_name],
        name=name,
        axis=1,
    )
    return node, out_name


def conv_out_shape(h, w, kh, kw, sh, sw, ph):
    """ONNX output shape: (in + total_pad - kernel) / stride + 1."""
    return (h + ph - kh) // sh + 1, (w + ph - kw) // sw + 1


def build_alexnet_tiny():
    np.random.seed(42)
    nodes = []
    initializers = []

    # ---- Verify shapes with numpy before building ----
    H, W = 32, 32

    # conv1: 3×3, pad=[1,1,1,1] (total_pad=2), stride=1 → shape-preserving
    # H_out = (32 + 2 - 3) / 1 + 1 = 32
    W1 = np.random.randn(16, 3, 3, 3) * 0.01
    b1 = np.zeros(16)
    n, inits, x = make_conv("conv1", "input", W1, b1, [3, 3], strides=(1, 1), pads=[1, 1, 1, 1])
    nodes.append(n); initializers.extend(inits)
    H, W = conv_out_shape(H, W, 3, 3, 1, 1, 2)
    assert (H, W) == (32, 32), f"conv1: {H}×{W}"
    C = 16

    # relu1
    n, x = make_relu("relu1", x); nodes.append(n)

    # pool1: 2×2, stride=2, no pad → 16×16
    n, x = make_maxpool("pool1", x, [2, 2], [2, 2]); nodes.append(n)
    H, W = (H) // 2, (W) // 2          # (32-2)/2+1=16
    assert (H, W) == (16, 16), f"pool1: {H}×{W}"

    # conv2: 32 filters, 3×3, pad=1 → 32×16×16
    W2 = np.random.randn(32, C, 3, 3) * 0.01
    b2 = np.zeros(32)
    n, inits, x = make_conv("conv2", x, W2, b2, [3, 3], strides=(1, 1), pads=[1, 1, 1, 1])
    nodes.append(n); initializers.extend(inits)
    H, W = conv_out_shape(H, W, 3, 3, 1, 1, 2)
    assert (H, W) == (16, 16), f"conv2: {H}×{W}"
    C = 32

    # relu2
    n, x = make_relu("relu2", x); nodes.append(n)

    # pool2: 2×2, stride=2 → 32×8×8
    n, x = make_maxpool("pool2", x, [2, 2], [2, 2]); nodes.append(n)
    H, W = H // 2, W // 2
    assert (H, W) == (8, 8), f"pool2: {H}×{W}"

    # conv3: 32 filters, 3×3, pad=1 → 32×8×8
    W3 = np.random.randn(32, C, 3, 3) * 0.01
    b3 = np.zeros(32)
    n, inits, x = make_conv("conv3", x, W3, b3, [3, 3], strides=(1, 1), pads=[1, 1, 1, 1])
    nodes.append(n); initializers.extend(inits)
    H, W = conv_out_shape(H, W, 3, 3, 1, 1, 2)
    assert (H, W) == (8, 8), f"conv3: {H}×{W}"

    # relu3
    n, x = make_relu("relu3", x); nodes.append(n)

    # conv4: 16 filters, 3×3, pad=1 → 16×8×8
    W4 = np.random.randn(16, 32, 3, 3) * 0.01
    b4 = np.zeros(16)
    n, inits, x = make_conv("conv4", x, W4, b4, [3, 3], strides=(1, 1), pads=[1, 1, 1, 1])
    nodes.append(n); initializers.extend(inits)
    H, W = conv_out_shape(H, W, 3, 3, 1, 1, 2)
    assert (H, W) == (8, 8), f"conv4: {H}×{W}"
    C = 16

    # relu4
    n, x = make_relu("relu4", x); nodes.append(n)

    # pool3: 2×2, stride=2, no pad → 16×4×4
    n, x = make_maxpool("pool3", x, [2, 2], [2, 2]); nodes.append(n)
    H, W = H // 2, W // 2
    assert (H, W) == (4, 4), f"pool3: {H}×{W}"

    # flatten: 16×4×4 = 256
    flatten_size = C * H * W
    assert flatten_size == 256, f"flatten_size={flatten_size}"
    n, x = make_flatten("flatten1", x, axis=1); nodes.append(n)

    # fc1: 256 → 128
    W6 = np.random.randn(128, flatten_size) * 0.01
    b6 = np.zeros(128)
    n, inits, x = make_gemm("fc1", x, W6, b6, transB=1)
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu5", x); nodes.append(n)

    # fc2: 128 → 64
    W7 = np.random.randn(64, 128) * 0.01
    b7 = np.zeros(64)
    n, inits, x = make_gemm("fc2", x, W7, b7, transB=1)
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu6", x); nodes.append(n)

    # fc3: 64 → 10
    W8 = np.random.randn(10, 64) * 0.01
    b8 = np.zeros(10)
    n, inits, x = make_gemm("fc3", x, W8, b8, transB=1)
    nodes.append(n); initializers.extend(inits)

    # softmax
    n, output_name = make_softmax("softmax", x)
    nodes.append(n)

    # ---- Build graph ----
    graph = helper.make_graph(
        nodes,
        "alexnet_tiny",
        inputs=[helper.make_tensor_value_info("input",  TensorProto.FLOAT, [1, 3, 32, 32])],
        outputs=[helper.make_tensor_value_info(output_name, TensorProto.FLOAT, [1, 10])],
        initializer=initializers,
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    return model


if __name__ == "__main__":
    import os
    out_dir = os.path.join(os.path.dirname(__file__), "..", "src", "storage")
    out_path = os.path.join(out_dir, "alexnet_tiny.onnx")

    print("Building tiny AlexNet (Opset 17, clean shapes)...")
    model = build_alexnet_tiny()

    size_bytes = model.ByteSize()
    print(f"Model size: {size_bytes:,} bytes ({size_bytes/1024:.1f} KB)")

    with open(out_path, "wb") as f:
        f.write(model.SerializeToString())

    print(f"Saved: {os.path.abspath(out_path)}")
    print()
    print("Verified architecture:")
    print("  Input   : [1, 3, 32, 32]")
    print("  conv1   : 16×3×3 pad=1 → [1,16,32,32] + relu")
    print("  pool1   : 2×2 s=2       → [1,16,16,16]")
    print("  conv2   : 32×3×3 pad=1 → [1,32,16,16] + relu")
    print("  pool2   : 2×2 s=2       → [1,32, 8, 8]")
    print("  conv3   : 32×3×3 pad=1 → [1,32, 8, 8] + relu")
    print("  conv4   : 16×3×3 pad=1 → [1,16, 8, 8] + relu")
    print("  pool3   : 2×2 s=2       → [1,16, 4, 4]")
    print("  flatten :               → [1, 256]")
    print("  fc1     : 256→128 + relu")
    print("  fc2     : 128→ 64 + relu")
    print("  fc3     :  64→ 10")
    print("  softmax :               → [1, 10]")
