#!/usr/bin/env python3
"""
export_alexnet_tiny.py
----------------------
Exports a tiny AlexNet-architecture model in ONNX Opset 17 format.

Full AlexNet (234 MB) cannot fit in MiniOS — the unikernel has 512 MB total
RAM and needs the kernel + arena too.  This script creates a structurally
identical but dimensionally reduced variant:

  Input:  [1, 3, 32, 32]   (vs [1, 3, 224, 224])
  conv1:  16 filters, 5x5   (vs 64, 11x11)
  lrn1:   Local Response Normalization (same structure, tiny input)
  pool1:  2x2, stride 2
  conv2:  32 filters, 3x3   (vs 192, 5x5)
  lrn2:   Local Response Normalization
  pool2:  2x2, stride 2
  conv3:  32 filters, 3x3   (vs 384, 3x3)
  conv4:  32 filters, 3x3   (vs 256, 3x3)
  conv5:  16 filters, 3x3   (vs 256, 3x3)
  pool3:  2x2, stride 2
  flatten
  fc1:    512 -> 128         (vs 4096 -> 4096)
  fc2:    128 -> 64
  fc3:    64  -> 10          (vs 4096 -> 1000)
  softmax

Total weights ~450 KB, activations <2 MB — well within 8 MB model buffer
and 128 MB arena.
"""

import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper

def make_conv(name, input_name, weight_data, bias_data, kernel_shape,
              strides=(1,1), pads=(0,0,0,0)):
    w_name = f"{name}_W"
    b_name = f"{name}_B"
    out_name = f"{name}_out"

    w_init = numpy_helper.from_array(weight_data.astype(np.float32), name=w_name)
    b_init = numpy_helper.from_array(bias_data.astype(np.float32), name=b_name)

    node = helper.make_node(
        "Conv",
        inputs=[input_name, w_name, b_name],
        outputs=[out_name],
        name=name,
        kernel_shape=kernel_shape,
        strides=list(strides),
        pads=list(pads),
    )
    return node, [w_init, b_init], out_name


def make_relu(name, input_name):
    out_name = f"{name}_out"
    node = helper.make_node("Relu", inputs=[input_name], outputs=[out_name], name=name)
    return node, out_name


def make_lrn(name, input_name, size=5, alpha=0.0001, beta=0.75, bias=1.0):
    out_name = f"{name}_out"
    node = helper.make_node(
        "LRN",
        inputs=[input_name],
        outputs=[out_name],
        name=name,
        size=size,
        alpha=alpha,
        beta=beta,
        bias=bias,
    )
    return node, out_name


def make_maxpool(name, input_name, kernel_shape, strides, pads=(0,0,0,0)):
    out_name = f"{name}_out"
    node = helper.make_node(
        "MaxPool",
        inputs=[input_name],
        outputs=[out_name],
        name=name,
        kernel_shape=kernel_shape,
        strides=strides,
        pads=list(pads),
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


def build_alexnet_tiny():
    np.random.seed(42)
    nodes = []
    initializers = []

    # ---- conv1: [1,3,32,32] -> [1,16,28,28] (no padding, 5x5) ----
    W1 = np.random.randn(16, 3, 5, 5) * 0.01
    b1 = np.zeros(16)
    n, inits, x = make_conv("conv1", "input", W1, b1, [5,5])
    nodes.append(n); initializers.extend(inits)

    # relu1
    n, x = make_relu("relu1", x); nodes.append(n)

    # lrn1: [1,16,28,28] -> [1,16,28,28]
    n, x = make_lrn("lrn1", x, size=5, alpha=0.0001, beta=0.75, bias=1.0)
    nodes.append(n)

    # pool1: [1,16,28,28] -> [1,16,13,13] (2x2, stride 2, pad 0)
    n, x = make_maxpool("pool1", x, [3,3], [2,2], pads=[0,0,1,1])
    nodes.append(n)

    # ---- conv2: [1,16,13,13] -> [1,32,13,13] (pad=1) ----
    W2 = np.random.randn(32, 16, 3, 3) * 0.01
    b2 = np.zeros(32)
    n, inits, x = make_conv("conv2", x, W2, b2, [3,3], pads=[1,1,1,1])
    nodes.append(n); initializers.extend(inits)

    # relu2
    n, x = make_relu("relu2", x); nodes.append(n)

    # lrn2
    n, x = make_lrn("lrn2", x, size=5, alpha=0.0001, beta=0.75, bias=1.0)
    nodes.append(n)

    # pool2: [1,32,13,13] -> [1,32,6,6]
    n, x = make_maxpool("pool2", x, [3,3], [2,2], pads=[0,0,1,1])
    nodes.append(n)

    # ---- conv3: [1,32,6,6] -> [1,32,6,6] (pad=1) ----
    W3 = np.random.randn(32, 32, 3, 3) * 0.01
    b3 = np.zeros(32)
    n, inits, x = make_conv("conv3", x, W3, b3, [3,3], pads=[1,1,1,1])
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu3", x); nodes.append(n)

    # ---- conv4: [1,32,6,6] -> [1,32,6,6] (pad=1) ----
    W4 = np.random.randn(32, 32, 3, 3) * 0.01
    b4 = np.zeros(32)
    n, inits, x = make_conv("conv4", x, W4, b4, [3,3], pads=[1,1,1,1])
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu4", x); nodes.append(n)

    # ---- conv5: [1,32,6,6] -> [1,16,6,6] (pad=1) ----
    W5 = np.random.randn(16, 32, 3, 3) * 0.01
    b5 = np.zeros(16)
    n, inits, x = make_conv("conv5", x, W5, b5, [3,3], pads=[1,1,1,1])
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu5", x); nodes.append(n)

    # pool3: [1,16,6,6] -> [1,16,3,3]
    n, x = make_maxpool("pool3", x, [2,2], [2,2])
    nodes.append(n)

    # flatten: [1,16,3,3] -> [1, 144]
    n, x = make_flatten("flatten1", x, axis=1)
    nodes.append(n)

    # ---- fc1: 144 -> 128 (transB=1: W shape [128,144]) ----
    W6 = np.random.randn(128, 144) * 0.01
    b6 = np.zeros(128)
    n, inits, x = make_gemm("fc1", x, W6, b6, transB=1)
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu6", x); nodes.append(n)

    # ---- fc2: 128 -> 64 (transB=1: W shape [64,128]) ----
    W7 = np.random.randn(64, 128) * 0.01
    b7 = np.zeros(64)
    n, inits, x = make_gemm("fc2", x, W7, b7, transB=1)
    nodes.append(n); initializers.extend(inits)
    n, x = make_relu("relu7", x); nodes.append(n)

    # ---- fc3: 64 -> 10 (transB=1: W shape [10,64]) ----
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
    import os, sys
    out_dir = os.path.join(os.path.dirname(__file__), "..", "src", "storage")
    out_path = os.path.join(out_dir, "alexnet_tiny.onnx")

    print("Building tiny AlexNet (Opset 17)...")
    model = build_alexnet_tiny()

    size_bytes = model.ByteSize()
    print(f"Model size: {size_bytes:,} bytes ({size_bytes/1024:.1f} KB)")

    with open(out_path, "wb") as f:
        f.write(model.SerializeToString())

    print(f"Saved to: {os.path.abspath(out_path)}")
    print()
    print("Architecture:")
    print("  Input:  [1, 3, 32, 32]")
    print("  conv1:  16x5x5  -> relu -> lrn -> pool (3x3 s2)")
    print("  conv2:  32x3x3  -> relu -> lrn -> pool (3x3 s2)")
    print("  conv3:  32x3x3  -> relu")
    print("  conv4:  32x3x3  -> relu")
    print("  conv5:  16x3x3  -> relu -> pool (2x2 s2)")
    print("  flatten -> fc1(144->128) -> relu -> fc2(128->64) -> relu -> fc3(64->10) -> softmax")
    print("  Output: [1, 10]")
    print()
    print("To run in MiniOS:")
    print("  onnx_run /storage/alexnet_tiny.onnx")
