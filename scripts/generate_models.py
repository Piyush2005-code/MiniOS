#!/usr/bin/env python3
"""
generate_models.py
------------------
Generates a set of ONNX Opset-17 models of varying sizes and
architectures for testing the MiniOS inference engine.

Models generated:
  1. tiny_mlp.onnx          ~  8 KB   Pure MLP, 3 layers
  2. lenet5.onnx             ~ 50 KB   LeNet-5 (28x28 input)
  3. conv_bn_net.onnx        ~200 KB   Conv + BatchNorm + ReLU net
  4. alexnet_tiny.onnx       ~222 KB   Already exists, skipped
  5. vgg_nano.onnx           ~600 KB   VGG-style deeper conv stack
  6. resnet_micro.onnx       ~900 KB   Tiny ResNet with residual Add
  7. transformer_tiny.onnx   ~1.4 MB   MatMul-heavy transformer enc block

All models are < 4 MB so they get embedded in initfs and are available
immediately in the MiniOS shell without flash setup.
"""

import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper
import os, sys

np.random.seed(0)
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "src", "storage")


# ===========================================================================
# Helper utilities
# ===========================================================================

def rn(shape, scale=0.01):
    return (np.random.randn(*shape) * scale).astype(np.float32)

def zeros(shape):
    return np.zeros(shape, dtype=np.float32)

def init(name, arr):
    return numpy_helper.from_array(arr, name=name)

def vi(name, dtype, shape):
    return helper.make_tensor_value_info(name, dtype, shape)

def node(op, inputs, outputs, name=None, **kwargs):
    return helper.make_node(op, inputs=inputs, outputs=outputs,
                             name=name or f"{op}_{outputs[0]}", **kwargs)

def save(model, filename):
    path = os.path.join(OUT_DIR, filename)
    onnx.checker.check_model(model)
    size = model.ByteSize()
    with open(path, "wb") as f:
        f.write(model.SerializeToString())
    print(f"  [{size//1024:>5} KB]  {filename}")
    return path

def make_model(graph):
    m = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    m.ir_version = 8
    return m


# ===========================================================================
# 1. tiny_mlp.onnx
#    Input [1,16] -> Linear(16,32) -> ReLU -> Linear(32,16) -> ReLU
#    -> Linear(16,4) -> Softmax
#    Exercises: Gemm, Relu, Softmax
# ===========================================================================

def build_tiny_mlp():
    inits, nodes = [], []

    def fc(inp, in_f, out_f, tag):
        W = rn([out_f, in_f]); b = zeros([out_f])
        wn, bn, on = f"W_{tag}", f"b_{tag}", f"{tag}_gemm"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Gemm", [inp, wn, bn], [on], name=f"gemm_{tag}",
                          transB=1, alpha=1.0, beta=1.0))
        rout = f"{tag}_relu"
        nodes.append(node("Relu", [on], [rout], name=f"relu_{tag}"))
        return rout

    x = fc("input", 16, 32, "l1")
    x = fc(x, 32, 16, "l2")

    # Last layer: no relu, just softmax
    W = rn([4, 16]); b = zeros([4])
    inits.extend([init("W_l3", W), init("b_l3", b)])
    nodes.append(node("Gemm", [x, "W_l3", "b_l3"], ["l3_gemm"],
                       name="gemm_l3", transB=1, alpha=1.0, beta=1.0))
    nodes.append(node("Softmax", ["l3_gemm"], ["output"], name="softmax", axis=1))

    g = helper.make_graph(nodes, "tiny_mlp",
                          [vi("input", TensorProto.FLOAT, [1, 16])],
                          [vi("output", TensorProto.FLOAT, [1, 4])],
                          initializer=inits)
    return make_model(g)


# ===========================================================================
# 2. lenet5.onnx
#    Classic LeNet-5 for 28x28 grayscale.
#    Input [1,1,28,28]
#    Conv(6,5x5)->ReLU->MaxPool(2x2)->Conv(16,5x5)->ReLU->MaxPool(2x2)
#    ->Flatten->FC(120)->ReLU->FC(84)->ReLU->FC(10)->Softmax
#    Exercises: Conv, MaxPool, Flatten, Gemm, Relu, Softmax
# ===========================================================================

def build_lenet5():
    inits, nodes = [], []
    ctr = [0]

    def conv(inp, c_in, c_out, k, name, pads=None):
        if pads is None: pads = [0]*4
        W = rn([c_out, c_in, k, k]); b = zeros([c_out])
        wn, bn, on = f"{name}_W", f"{name}_b", f"{name}_out"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Conv", [inp, wn, bn], [on], name=name,
                           kernel_shape=[k, k], pads=pads))
        relu_out = f"{name}_relu"
        nodes.append(node("Relu", [on], [relu_out], name=f"{name}_relu"))
        return relu_out

    def pool(inp, k, s, name):
        out = f"{name}_out"
        nodes.append(node("MaxPool", [inp], [out], name=name,
                           kernel_shape=[k, k], strides=[s, s]))
        return out

    def fc(inp, in_f, out_f, name, relu=True):
        W = rn([out_f, in_f]); b = zeros([out_f])
        wn, bn, on = f"{name}_W", f"{name}_b", f"{name}_out"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Gemm", [inp, wn, bn], [on],
                           name=name, transB=1, alpha=1.0, beta=1.0))
        if relu:
            rout = f"{name}_relu"
            nodes.append(node("Relu", [on], [rout], name=f"{name}_relu"))
            return rout
        return on

    x = conv("input", 1, 6, 5, "conv1")
    x = pool(x, 2, 2, "pool1")
    x = conv(x, 6, 16, 5, "conv2")
    x = pool(x, 2, 2, "pool2")

    flat = "flatten_out"
    nodes.append(node("Flatten", [x], [flat], name="flatten", axis=1))

    x = fc(flat, 256, 120, "fc1")
    x = fc(x, 120, 84, "fc2")
    x = fc(x, 84, 10, "fc3", relu=False)
    nodes.append(node("Softmax", [x], ["output"], name="softmax", axis=1))

    g = helper.make_graph(nodes, "lenet5",
                          [vi("input", TensorProto.FLOAT, [1, 1, 28, 28])],
                          [vi("output", TensorProto.FLOAT, [1, 10])],
                          initializer=inits)
    return make_model(g)


# ===========================================================================
# 3. conv_bn_net.onnx
#    A BatchNorm-heavy network to test BN operator.
#    Input [1,3,16,16]
#    Conv+BN+ReLU x3 -> GlobalAvgPool -> FC(10) -> Softmax
#    Exercises: Conv, BatchNormalization, GlobalAveragePool, Flatten, Gemm
# ===========================================================================

def build_conv_bn_net():
    inits, nodes = [], []

    def conv_bn_relu(inp, c_in, c_out, k, name, pads=None):
        if pads is None: pads = [k//2]*4
        # Conv
        W = rn([c_out, c_in, k, k]); wb = zeros([c_out])
        wn, bwn = f"{name}_W", f"{name}_Wb"
        inits.extend([init(wn, W), init(bwn, wb)])
        conv_out = f"{name}_conv"
        nodes.append(node("Conv", [inp, wn, bwn], [conv_out], name=f"{name}_conv",
                           kernel_shape=[k, k], pads=pads))
        # BatchNorm (scale, B, mean, var)
        scale = np.ones(c_out, np.float32)
        bn_b  = np.zeros(c_out, np.float32)
        mean  = np.zeros(c_out, np.float32)
        var   = np.ones(c_out, np.float32)
        sn, bbn, mn, vn = (f"{name}_bn_{s}" for s in ["scale","B","mean","var"])
        inits.extend([init(sn, scale), init(bbn, bn_b),
                      init(mn, mean),  init(vn, var)])
        bn_out = f"{name}_bn"
        nodes.append(node("BatchNormalization",
                           [conv_out, sn, bbn, mn, vn], [bn_out],
                           name=f"{name}_bn", epsilon=1e-5, momentum=0.9))
        relu_out = f"{name}_relu"
        nodes.append(node("Relu", [bn_out], [relu_out], name=f"{name}_relu"))
        return relu_out

    x = conv_bn_relu("input", 3,  16, 3, "block1")
    x = conv_bn_relu(x,       16, 32, 3, "block2")
    x = conv_bn_relu(x,       32, 64, 3, "block3")

    gap = "gap_out"
    nodes.append(node("GlobalAveragePool", [x], [gap], name="gap"))

    flat = "flat_out"
    nodes.append(node("Flatten", [gap], [flat], name="flatten", axis=1))

    W = rn([10, 64]); b = zeros([10])
    inits.extend([init("fc_W", W), init("fc_b", b)])
    nodes.append(node("Gemm", [flat, "fc_W", "fc_b"], ["fc_out"],
                       name="fc", transB=1, alpha=1.0, beta=1.0))
    nodes.append(node("Softmax", ["fc_out"], ["output"], name="softmax", axis=1))

    g = helper.make_graph(nodes, "conv_bn_net",
                          [vi("input", TensorProto.FLOAT, [1, 3, 16, 16])],
                          [vi("output", TensorProto.FLOAT, [1, 10])],
                          initializer=inits)
    return make_model(g)


# ===========================================================================
# 4. vgg_nano.onnx
#    VGG-style: stacked 3x3 convs with pooling, then FC.
#    Input [1,3,32,32]
#    [Conv3x3-ReLU] x2 -> Pool -> [Conv3x3-ReLU] x3 -> Pool -> FC(256) -> FC(10)
#    Exercises: deep Conv stacking, larger weight tensors
# ===========================================================================

def build_vgg_nano():
    inits, nodes = [], []

    def conv_relu(inp, c_in, c_out, name):
        W = rn([c_out, c_in, 3, 3]); b = zeros([c_out])
        wn, bn, on = f"{name}_W", f"{name}_b", f"{name}_conv"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Conv", [inp, wn, bn], [on], name=f"{name}_conv",
                           kernel_shape=[3, 3], pads=[1,1,1,1]))
        rout = f"{name}_relu"
        nodes.append(node("Relu", [on], [rout], name=f"{name}_relu"))
        return rout

    def maxpool(inp, name):
        out = f"{name}_out"
        nodes.append(node("MaxPool", [inp], [out], name=name,
                           kernel_shape=[2,2], strides=[2,2]))
        return out

    def fc_relu(inp, in_f, out_f, name, relu=True):
        W = rn([out_f, in_f]); b = zeros([out_f])
        wn, bn, on = f"{name}_W", f"{name}_b", f"{name}_out"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Gemm", [inp, wn, bn], [on],
                           name=name, transB=1, alpha=1.0, beta=1.0))
        if relu:
            rout = f"{name}_relu"
            nodes.append(node("Relu", [on], [rout], name=f"{name}_relu"))
            return rout
        return on

    # Block 1: [1,3,32,32] -> [1,32,32,32] -> [1,32,16,16]
    x = conv_relu("input", 3,  32, "c1a")
    x = conv_relu(x,       32, 32, "c1b")
    x = maxpool(x, "pool1")

    # Block 2: [1,32,16,16] -> [1,64,16,16] -> [1,64,8,8]
    x = conv_relu(x, 32, 64, "c2a")
    x = conv_relu(x, 64, 64, "c2b")
    x = conv_relu(x, 64, 64, "c2c")
    x = maxpool(x, "pool2")

    # GlobalAveragePool -> [1,64,1,1] then Flatten -> [1,64]
    gap = "gap_out"
    nodes.append(node("GlobalAveragePool", [x], [gap], name="gap"))
    flat = "flat_out"
    nodes.append(node("Flatten", [gap], [flat], name="flatten", axis=1))

    # Small FC head: 64 -> 128 -> 10
    x = fc_relu(flat, 64, 128, "fc1")
    x = fc_relu(x,   128, 10,  "fc2", relu=False)
    nodes.append(node("Softmax", [x], ["output"], name="softmax", axis=1))

    g = helper.make_graph(nodes, "vgg_nano",
                          [vi("input", TensorProto.FLOAT, [1, 3, 32, 32])],
                          [vi("output", TensorProto.FLOAT, [1, 10])],
                          initializer=inits)
    return make_model(g)


# ===========================================================================
# 5. resnet_micro.onnx
#    Tiny ResNet with one residual block.
#    Input [1,8,16,16]
#    Conv1x1 (stem) -> [Conv3x3 + shortcut Add] x2 -> GlobalAvgPool -> FC(10)
#    Exercises: Conv, Add (residual), GlobalAveragePool
# ===========================================================================

def build_resnet_micro():
    inits, nodes = [], []
    C = 16  # channels throughout residual blocks

    def conv(inp, c_in, c_out, k, name, pads=None, stride=1):
        if pads is None: pads = [k//2]*4
        W = rn([c_out, c_in, k, k]); b = zeros([c_out])
        wn, bn, on = f"{name}_W", f"{name}_b", f"{name}_out"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Conv", [inp, wn, bn], [on], name=name,
                           kernel_shape=[k, k], pads=pads, strides=[stride, stride]))
        return on

    def relu(inp, name):
        out = f"{name}_out"
        nodes.append(node("Relu", [inp], [out], name=name))
        return out

    def res_block(inp, c, name):
        # Conv3x3 -> ReLU -> Conv3x3 -> Add(shortcut) -> ReLU
        h = conv(inp, c, c, 3, f"{name}_c1")
        h = relu(h, f"{name}_r1")
        h = conv(h,  c, c, 3, f"{name}_c2")
        add_out = f"{name}_add"
        nodes.append(node("Add", [h, inp], [add_out], name=f"{name}_add"))
        return relu(add_out, f"{name}_r2")

    # Stem: [1,3,16,16] -> [1,C,16,16]
    x = conv("input", 3, C, 3, "stem")
    x = relu(x, "stem_relu")

    # Two residual blocks
    x = res_block(x, C, "res1")
    x = res_block(x, C, "res2")

    # GlobalAvgPool -> [1,C,1,1]
    gap = "gap_out"
    nodes.append(node("GlobalAveragePool", [x], [gap], name="gap"))

    # Flatten -> [1, C]
    flat = "flat_out"
    nodes.append(node("Flatten", [gap], [flat], name="flatten", axis=1))

    # FC: C -> 10
    W = rn([10, C]); b = zeros([10])
    inits.extend([init("fc_W", W), init("fc_b", b)])
    nodes.append(node("Gemm", [flat, "fc_W", "fc_b"], ["fc_out"],
                       name="fc", transB=1, alpha=1.0, beta=1.0))
    nodes.append(node("Softmax", ["fc_out"], ["output"], name="softmax", axis=1))

    g = helper.make_graph(nodes, "resnet_micro",
                          [vi("input", TensorProto.FLOAT, [1, 3, 16, 16])],
                          [vi("output", TensorProto.FLOAT, [1, 10])],
                          initializer=inits)
    return make_model(g)


# ===========================================================================
# 6. transformer_tiny.onnx
#    One transformer encoder-like block (no attention — simplified).
#    Uses heavy MatMul chains to stress-test Gemm.
#    Input [1, 32]  (flat token embedding)
#    FF1(32->64)->ReLU->FF2(64->32)->Add+clip(residual) -> FC(10)->Softmax
#    + a wide MatMul-chain block to get the size up to ~1.4 MB
#    Exercises: Gemm, Relu, Add, Clip, Softmax
# ===========================================================================

def build_transformer_tiny():
    inits, nodes = [], []

    def fc(inp, in_f, out_f, name, relu=True):
        W = rn([out_f, in_f]); b = zeros([out_f])
        wn, bn, on = f"{name}_W", f"{name}_b", f"{name}_out"
        inits.extend([init(wn, W), init(bn, b)])
        nodes.append(node("Gemm", [inp, wn, bn], [on],
                           name=name, transB=1, alpha=1.0, beta=1.0))
        if relu:
            rout = f"{name}_relu"
            nodes.append(node("Relu", [on], [rout], name=f"{name}_relu"))
            return rout
        return on

    D = 64   # model dim
    FF = 256  # feed-forward hidden dim

    # Wide embedding projection: [1,32] -> [1,D]
    x = fc("input", 32, D, "embed")

    # 6 transformer-style FF blocks: D -> FF -> D
    for i in range(6):
        h = fc(x, D, FF, f"ff{i}a")
        h = fc(h, FF, D, f"ff{i}b", relu=False)
        # Residual add
        add_out = f"res{i}"
        nodes.append(node("Add", [h, x], [add_out], name=f"add{i}"))
        # Clip as a simple layer-norm surrogate
        clip_out = f"clip{i}"
        nodes.append(node("Relu", [add_out], [clip_out], name=f"relu_res{i}"))
        x = clip_out

    # Final classifier
    x = fc(x, D, 10, "classifier", relu=False)
    nodes.append(node("Softmax", [x], ["output"], name="softmax", axis=1))

    g = helper.make_graph(nodes, "transformer_tiny",
                          [vi("input", TensorProto.FLOAT, [1, 32])],
                          [vi("output", TensorProto.FLOAT, [1, 10])],
                          initializer=inits)
    return make_model(g)


# ===========================================================================
# Main
# ===========================================================================

MODELS = [
    ("tiny_mlp.onnx",          build_tiny_mlp,          "Pure MLP [1,16]->[1,4]  (Gemm+Relu+Softmax)"),
    ("lenet5.onnx",             build_lenet5,             "LeNet-5  [1,1,28,28]->[1,10]  (Conv+Pool+Gemm)"),
    ("conv_bn_net.onnx",        build_conv_bn_net,        "Conv+BN  [1,3,16,16]->[1,10]  (Conv+BatchNorm+GAP)"),
    ("vgg_nano.onnx",           build_vgg_nano,           "VGG-nano [1,3,32,32]->[1,10]  (Deep Conv stacks)"),
    ("resnet_micro.onnx",       build_resnet_micro,       "ResNet   [1,3,16,16]->[1,10]  (Conv+Add residual)"),
    ("transformer_tiny.onnx",   build_transformer_tiny,   "Transformer-tiny [1,32]->[1,10] (MatMul-heavy)"),
]

if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"\nGenerating {len(MODELS)} ONNX models -> {os.path.abspath(OUT_DIR)}/\n")
    print(f"  {'Size':>8}   {'Filename':<30}  Description")
    print(f"  {'-'*8}   {'-'*30}  {'-'*40}")

    for filename, builder, desc in MODELS:
        out_path = os.path.join(OUT_DIR, filename)
        if os.path.exists(out_path):
            size = os.path.getsize(out_path)
            print(f"  [{size//1024:>5} KB]  {filename:<30}  (already exists, skipping)")
            continue
        try:
            model = builder()
            path = save(model, filename)
            print(f"            {'':30}  {desc}")
        except Exception as e:
            print(f"  [ERROR]  {filename}: {e}")

    print(f"\nDone. Run in MiniOS:")
    for filename, _, _ in MODELS:
        stem = filename.replace(".onnx", "")
        print(f"  onnx_run {filename}")
    print()
