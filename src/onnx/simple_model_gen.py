#!/usr/bin/env python3
"""
Generate a minimal ONNX model without PyTorch
Just a simple Add operation: Y = X + B
"""
import onnx
from onnx import helper, TensorProto
import numpy as np

# Create input tensor
X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1, 3])

# Create output tensor
Y = helper.make_tensor_value_info('Y', TensorProto.FLOAT, [1, 3])

# Create bias initializer
B_data = np.array([1.0, 2.0, 3.0], dtype=np.float32)
B = helper.make_tensor('B', TensorProto.FLOAT, [1, 3], B_data.flatten().tolist())

# Create node: Y = X + B
add_node = helper.make_node(
    'Add',
    inputs=['X', 'B'],
    outputs=['Y'],
    name='add_node'
)

# Create graph
graph = helper.make_graph(
    [add_node],           # nodes
    'SimpleAdd',          # name
    [X],                  # inputs
    [Y],                  # outputs
    [B]                   # initializers
)

# Create model
model = helper.make_model(graph, producer_name='minios')
model.opset_import[0].version = 13

# Save
onnx.save(model, 'simple_add.onnx')
print("Generated simple_add.onnx")
print(f"Size: {len(onnx._serialize(model))} bytes")
