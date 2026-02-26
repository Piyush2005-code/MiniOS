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
import onnx
from onnx import numpy_helper

# Magic number: 'ONNX'
MAGIC = 0x4F4E4E58
VERSION = 1

# Operator type mapping (must match onnx_types.h)
OP_TYPE_MAP = {
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


def convert_to_binary(model, output_file):
    """Convert ONNX model to custom binary format"""
    
    graph = model.graph
    
    # Count entities
    num_nodes = len(graph.node)
    num_tensors = len(graph.value_info) + len(graph.input) + len(graph.output)
    num_inputs = len(graph.input)
    num_outputs = len(graph.output)
    
    print(f"Converting model: {graph.name}")
    print(f"  Nodes: {num_nodes}")
    print(f"  Tensors: {num_tensors}")
    print(f"  Inputs: {num_inputs}")
    print(f"  Outputs: {num_outputs}")
    
    with open(output_file, 'wb') as f:
        # Write header
        header = struct.pack('<IIIIIIQ',
            MAGIC,
            VERSION,
            num_nodes,
            num_tensors,
            num_inputs,
            num_outputs,
            0  # tensor_data_offset - will calculate later
        )
        f.write(header)
        
        # Write tensor definitions
        print("\nWriting tensor definitions...")
        tensor_map = {}  # name -> index
        tensor_idx = 0
        
        # Helper to write tensor info
        def write_tensor_info(tensor):
            nonlocal tensor_idx
            name = tensor.name.encode('utf-8')[:63]  # Max 63 chars
            name = name + b'\0' * (64 - len(name))   # Pad to 64 bytes
            
            # Get type
            dtype = DTYPE_MAP.get(tensor.type.tensor_type.elem_type, 0)
            
            # Get shape
            shape = []
            if hasattr(tensor.type, 'tensor_type'):
                for dim in tensor.type.tensor_type.shape.dim:
                    if dim.dim_value:
                        shape.append(dim.dim_value)
                    else:
                        shape.append(1)  # Dynamic dimension -> use 1
            
            ndim = len(shape)
            shape = shape + [0] * (8 - ndim)  # Pad to 8 dimensions
            
            # Write: name(64), dtype(4), ndim(4), shape(8*8), is_initializer(1)
            f.write(name)
            f.write(struct.pack('<II', dtype, ndim))
            for dim in shape:
                f.write(struct.pack('<Q', dim))
            f.write(struct.pack('<B', 0))  # is_initializer
            
            tensor_map[tensor.name] = tensor_idx
            tensor_idx += 1
        
        # Write all tensors
        for inp in graph.input:
            write_tensor_info(inp)
        for out in graph.output:
            if out.name not in tensor_map:
                write_tensor_info(out)
        for val in graph.value_info:
            if val.name not in tensor_map:
                write_tensor_info(val)
        
        # Write node definitions
        print("\nWriting node definitions...")
        for node in graph.node:
            # Node: name(64), op_type(4), num_inputs(4), num_outputs(4)
            #       input_indices(16*4), output_indices(16*4)
            
            name = node.name.encode('utf-8')[:63] if node.name else node.op_type.encode('utf-8')[:63]
            name = name + b'\0' * (64 - len(name))
            
            op_type = OP_TYPE_MAP.get(node.op_type, 0)
            num_inputs = len(node.input)
            num_outputs = len(node.output)
            
            f.write(name)
            f.write(struct.pack('<III', op_type, num_inputs, num_outputs))
            
            # Write input indices
            for i in range(16):
                if i < num_inputs:
                    idx = tensor_map.get(node.input[i], 0xFFFFFFFF)
                else:
                    idx = 0xFFFFFFFF  # Invalid
                f.write(struct.pack('<I', idx))
            
            # Write output indices
            for i in range(16):
                if i < num_outputs:
                    idx = tensor_map.get(node.output[i], 0xFFFFFFFF)
                else:
                    idx = 0xFFFFFFFF
                f.write(struct.pack('<I', idx))
            
            print(f"  {node.op_type}: {node.input} -> {node.output}")
        
        # Write initializer data
        print("\nWriting initializer data...")
        for init in graph.initializer:
            tensor_idx = tensor_map.get(init.name, None)
            if tensor_idx is None:
                continue
            
            # Convert to numpy array
            data = numpy_helper.to_array(init)
            
            # Write: tensor_index(4), size(8), data(...)
            f.write(struct.pack('<IQ', tensor_idx, data.nbytes))
            f.write(data.tobytes())
            
            print(f"  {init.name}: {data.shape} = {data.nbytes} bytes")
    
    print(f"\nSuccessfully written to: {output_file}")


def convert_to_c_array(model, output_file):
    """Convert ONNX model to C array header file"""
    
    # First convert to binary format
    import tempfile
    import os
    
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
