#!/usr/bin/env python3
"""
Convert ONNX model to custom binary format for MiniOS

This script converts a .onnx file to our lightweight binary format
that can be easily parsed in the bare-metal kernel.

Usage:
    python convert_onnx_to_binary.py model.onnx output.bin
    
Or to generate a C header:
    python convert_onnx_to_binary.py model.onnx output.h --format=c_array
"""

import sys
import struct
import argparse
import os
import onnx
from onnx import numpy_helper

# Magic number: 'ONNX'
MAGIC = 0x4F4E4E58
VERSION = 1
INVALID_INDEX = 0xFFFFFFFF

MAX_NAME_LEN = 64
MAX_DIMS = 8
MAX_IO = 16
MAX_ATTR_INTS = 16

# Operator type mapping (must match onnx_types.h)
OP_TYPE_MAP = {
    'Undefined': 0,
    'Add': 1,
    'Sub': 2,
    'Mul': 3,
    'Div': 4,
    'MatMul': 5,
    'Relu': 6,
    'Sigmoid': 7,
    'Tanh': 8,
    'Softmax': 9,
    'Conv': 10,
    'MaxPool': 11,
    'AveragePool': 12,
    'Reshape': 13,
    'Transpose': 14,
    'Flatten': 15,
    'BatchNormalization': 16,
    'Gemm': 17,
    'Concat': 18,
    'LeakyRelu': 19,
    'GlobalAveragePool': 20,
    'Squeeze': 21,
    'Unsqueeze': 22,
    'Cast': 23,
    'Abs': 24,
    'Neg': 25,
    'Exp': 26,
    'Log': 27,
    'Sqrt': 28,
    'Ceil': 29,
    'Floor': 30,
    'Sin': 31,
    'Cos': 32,
    'ReduceSum': 33,
    'ReduceMean': 34,
    'ReduceMax': 35,
    'ReduceMin': 36,
    'Clip': 37,
    'Identity': 38,
    'LRN': 39,
    'Dropout': 40,
    'Split': 41,
    'Constant': 42,
}

# Data type mapping (must match onnx_types.h)
DTYPE_MAP = {
    1: 1,   # FLOAT32
    2: 2,   # UINT8
    3: 3,   # INT8
    4: 4,   # UINT16
    5: 5,   # INT16
    6: 6,   # INT32
    7: 7,   # INT64
    11: 11, # FLOAT64
}


def pack_name(name: str) -> bytes:
    raw = (name or "").encode("utf-8")[: MAX_NAME_LEN - 1]
    return raw + (b"\0" * (MAX_NAME_LEN - len(raw)))


def normalize_dims(dims):
    out = []
    for d in dims[:MAX_DIMS]:
        v = int(d)
        out.append(v if v > 0 else 1)
    if not out:
        out = [1]
    return out


def value_info_desc(vinfo):
    dtype = 1
    dims = [1]
    if hasattr(vinfo, "type") and hasattr(vinfo.type, "tensor_type"):
        tt = vinfo.type.tensor_type
        if tt.elem_type:
            dtype = DTYPE_MAP.get(tt.elem_type, 1)
        if tt.HasField("shape"):
            parsed = []
            for dim in tt.shape.dim:
                if dim.HasField("dim_value") and dim.dim_value > 0:
                    parsed.append(int(dim.dim_value))
                else:
                    parsed.append(1)
            dims = normalize_dims(parsed)
    return dtype, dims


def initializer_desc(tensor_proto):
    dtype = DTYPE_MAP.get(tensor_proto.data_type, 1)
    dims = normalize_dims(list(tensor_proto.dims))
    arr = numpy_helper.to_array(tensor_proto)
    data = arr.tobytes(order="C")
    return dtype, dims, data


def attr_int_list(vals):
    return [int(v) for v in list(vals)[:MAX_ATTR_INTS]]


def parse_node_attributes(node):
    attrs = {
        "kernel_shape": [],
        "strides": [],
        "pads": [],
        "dilations": [],
        "axis": 0,
        "group": 0,
        "alpha": 0.0,
        "beta": 0.0,
        "fuse_relu": 0,
        "keepdims": 0,
        "perm": [],
    }

    for a in node.attribute:
        n = a.name
        if n == "kernel_shape":
            attrs["kernel_shape"] = attr_int_list(a.ints)
        elif n == "strides":
            attrs["strides"] = attr_int_list(a.ints)
        elif n == "pads":
            attrs["pads"] = attr_int_list(a.ints)
        elif n == "dilations":
            attrs["dilations"] = attr_int_list(a.ints)
        elif n == "axis":
            attrs["axis"] = int(a.i)
        elif n == "group":
            attrs["group"] = int(a.i)
        elif n == "alpha":
            attrs["alpha"] = float(a.f)
        elif n == "beta":
            attrs["beta"] = float(a.f)
        elif n == "keepdims":
            attrs["keepdims"] = int(a.i)
        elif n == "perm":
            attrs["perm"] = attr_int_list(a.ints)
        elif n == "axes":
            attrs["perm"] = attr_int_list(a.ints)
        elif n == "split":
            attrs["kernel_shape"] = attr_int_list(a.ints)
        elif n == "value" and node.op_type == "Constant" and a.type == onnx.AttributeProto.TENSOR:
            t = a.t
            attrs["group"] = int(DTYPE_MAP.get(t.data_type, 0))
            arr = numpy_helper.to_array(t)
            if arr.dtype.kind in ("i", "u"):
                attrs["kernel_shape"] = [int(v) for v in arr.reshape(-1).tolist()[:MAX_ATTR_INTS]]

    return attrs


def build_tensor_catalog(graph):
    tensor_order = []
    seen = set()

    def add_name(name):
        if not name:
            return
        if name in seen:
            return
        seen.add(name)
        tensor_order.append(name)

    init_map = {t.name: t for t in graph.initializer}
    vinfo_map = {}
    for v in list(graph.input) + list(graph.output) + list(graph.value_info):
        vinfo_map[v.name] = v

    for v in graph.input:
        add_name(v.name)
    for v in graph.output:
        add_name(v.name)
    for v in graph.value_info:
        add_name(v.name)
    for t in graph.initializer:
        add_name(t.name)

    for node in graph.node:
        for name in node.input:
            add_name(name)
        for name in node.output:
            add_name(name)

    tensors = []
    tensor_index = {}
    data_blob = bytearray()

    for name in tensor_order:
        dtype = 1
        dims = [1]
        is_init = 0
        data_offset = 0
        data_size = 0

        if name in init_map:
            dtype, dims, raw = initializer_desc(init_map[name])
            is_init = 1
            data_offset = len(data_blob)
            data_size = len(raw)
            data_blob.extend(raw)
        elif name in vinfo_map:
            dtype, dims = value_info_desc(vinfo_map[name])

        dims_padded = list(dims[:MAX_DIMS]) + [0] * max(0, MAX_DIMS - len(dims))
        tensors.append(
            {
                "name": name,
                "dtype": dtype,
                "ndim": len(dims[:MAX_DIMS]),
                "dims": dims_padded,
                "is_initializer": is_init,
                "data_offset": data_offset,
                "data_size": data_size,
            }
        )
        tensor_index[name] = len(tensors) - 1

    graph_input_indices = []
    init_names = set(init_map.keys())
    for inp in graph.input:
        if inp.name in init_names:
            continue
        if inp.name in tensor_index:
            graph_input_indices.append(tensor_index[inp.name])

    graph_output_indices = []
    for out in graph.output:
        if out.name in tensor_index:
            graph_output_indices.append(tensor_index[out.name])

    return tensors, tensor_index, bytes(data_blob), graph_input_indices, graph_output_indices


def serialize_tensor_def(t):
    return struct.pack(
        "<64sII8QIIQQ",
        pack_name(t["name"]),
        int(t["dtype"]),
        int(t["ndim"]),
        *[int(x) for x in t["dims"]],
        int(t["is_initializer"]),
        0,
        int(t["data_offset"]),
        int(t["data_size"]),
    )


def serialize_node_def(node, idx, tensor_index):
    name = node.name if node.name else f"{node.op_type}_{idx}"
    op_type = OP_TYPE_MAP.get(node.op_type, 0)

    in_names = [n for n in node.input if n][:MAX_IO]
    out_names = [n for n in node.output if n][:MAX_IO]

    input_indices = [INVALID_INDEX] * MAX_IO
    output_indices = [INVALID_INDEX] * MAX_IO
    for i, n in enumerate(in_names):
        input_indices[i] = tensor_index.get(n, INVALID_INDEX)
    for i, n in enumerate(out_names):
        output_indices[i] = tensor_index.get(n, INVALID_INDEX)

    attrs = parse_node_attributes(node)

    def pad_i64(vals):
        a = [int(v) for v in vals[:MAX_ATTR_INTS]]
        return a + [0] * (MAX_ATTR_INTS - len(a))

    kernel_shape = pad_i64(attrs["kernel_shape"])
    strides = pad_i64(attrs["strides"])
    pads = pad_i64(attrs["pads"])
    dilations = pad_i64(attrs["dilations"])
    perm = pad_i64(attrs["perm"])

    return struct.pack(
        "<64sIII16I16II16qI16qI16qI16qqqffIqI16q",
        pack_name(name),
        op_type,
        len(in_names),
        len(out_names),
        *input_indices,
        *output_indices,
        len(attrs["kernel_shape"][:MAX_ATTR_INTS]),
        *kernel_shape,
        len(attrs["strides"][:MAX_ATTR_INTS]),
        *strides,
        len(attrs["pads"][:MAX_ATTR_INTS]),
        *pads,
        len(attrs["dilations"][:MAX_ATTR_INTS]),
        *dilations,
        int(attrs["axis"]),
        int(attrs["group"]),
        float(attrs["alpha"]),
        float(attrs["beta"]),
        int(attrs["fuse_relu"]),
        int(attrs["keepdims"]),
        len(attrs["perm"][:MAX_ATTR_INTS]),
        *perm,
    )


def convert_to_binary(model, output_file):
    """Convert ONNX model to custom binary format."""

    graph = model.graph
    tensors, tensor_index, data_blob, graph_inputs, graph_outputs = build_tensor_catalog(graph)

    tensor_defs = bytearray()
    for t in tensors:
        tensor_defs.extend(serialize_tensor_def(t))

    node_defs = bytearray()
    for i, node in enumerate(graph.node):
        node_defs.extend(serialize_node_def(node, i, tensor_index))

    input_section = bytearray()
    for idx in graph_inputs:
        input_section.extend(struct.pack("<I", idx))

    output_section = bytearray()
    for idx in graph_outputs:
        output_section.extend(struct.pack("<I", idx))

    tensor_data_offset = (
        struct.calcsize("<IIIIIIQ")
        + len(tensor_defs)
        + len(node_defs)
        + len(input_section)
        + len(output_section)
    )

    header = struct.pack(
        "<IIIIIIQ",
        MAGIC,
        VERSION,
        len(graph.node),
        len(tensors),
        len(graph_inputs),
        len(graph_outputs),
        tensor_data_offset,
    )

    payload = bytearray()
    payload.extend(header)
    payload.extend(tensor_defs)
    payload.extend(node_defs)
    payload.extend(input_section)
    payload.extend(output_section)
    payload.extend(data_blob)

    with open(output_file, "wb") as f:
        f.write(payload)

    print(f"Converting model: {graph.name or os.path.basename(output_file)}")
    print(f"  Nodes: {len(graph.node)}")
    print(f"  Tensors: {len(tensors)}")
    print(f"  Inputs: {len(graph_inputs)}")
    print(f"  Outputs: {len(graph_outputs)}")
    print(f"  Initializer bytes: {len(data_blob)}")
    print(f"Successfully written to: {output_file}")


def convert_to_c_array(model, output_file):
    """Convert ONNX model to C array header file"""
    
    # First convert to binary format
    import tempfile
    
    with tempfile.NamedTemporaryFile(delete=False, suffix='.bin') as tmp:
        tmp_name = tmp.name
    
    try:
        convert_to_binary(model, tmp_name)
        
        # Read binary and convert to C array
        with open(tmp_name, 'rb') as f:
            data = f.read()
        
        # Generate C header
        var_name = os.path.basename(output_file).replace('.', '_').replace('-', '_')
        if var_name.endswith('_h'):
            var_name = var_name[:-2]
        
        with open(output_file, 'w') as f:
            f.write(f"/* Auto-generated from ONNX model */\n")
            f.write(f"/* Total size: {len(data)} bytes */\n\n")
            f.write(f"#ifndef {var_name.upper()}_H\n")
            f.write(f"#define {var_name.upper()}_H\n\n")
            f.write(f"const unsigned char {var_name}_data[] = {{\n")
            
            for i in range(0, len(data), 12):
                chunk = data[i:i+12]
                hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
                f.write(f"  {hex_str},\n")
            
            f.write(f"}};\n\n")
            f.write(f"const unsigned int {var_name}_len = {len(data)};\n\n")
            f.write(f"#endif /* {var_name.upper()}_H */\n")
        
        print(f"\nC header written to: {output_file}")
        print(f"Usage in code:")
        print(f"  #include \"{os.path.basename(output_file)}\"")
        print(f"  ONNX_LoadEmbedded(&graph, {var_name}_data, {var_name}_len, ONNX_FORMAT_CUSTOM_BINARY);")
        
    finally:
        os.unlink(tmp_name)


def main():
    parser = argparse.ArgumentParser(description='Convert ONNX to MiniOS format')
    parser.add_argument('input', help='Input .onnx file')
    parser.add_argument('output', help='Output file')
    parser.add_argument('--format', choices=['binary', 'c_array'], default='binary',
                        help='Output format (default: binary)')
    
    args = parser.parse_args()
    
    # Load ONNX model
    print(f"Loading ONNX model: {args.input}")
    model = onnx.load(args.input)
    onnx.checker.check_model(model)
    print("Model is valid!")
    
    # Convert
    if args.format == 'c_array':
        convert_to_c_array(model, args.output)
    else:
        convert_to_binary(model, args.output)


if __name__ == '__main__':
    main()
