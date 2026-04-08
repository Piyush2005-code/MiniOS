#!/usr/bin/env python3
import onnx
from onnx import helper, TensorProto
import numpy as np

# Z = ReLU(X @ W + B)
# Input X: [1, 4]
# Weight W: [4, 3]
# Bias B: [1, 3]
# Output Z: [1, 3]

X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1, 4])
Z = helper.make_tensor_value_info('Z', TensorProto.FLOAT, [1, 3])

W_data = np.array([
    [0.1,  0.2, -0.1],
    [-0.2, 0.1,  0.3],
    [0.5, -0.1,  0.2],
    [0.0,  0.4, -0.2]
], dtype=np.float32)

B_data = np.array([[0.1, -0.2, 0.5]], dtype=np.float32)

W = helper.make_tensor('W', TensorProto.FLOAT, [4, 3], W_data.flatten().tolist())
B = helper.make_tensor('B', TensorProto.FLOAT, [1, 3], B_data.flatten().tolist())

# X @ W -> Y
matmul_node = helper.make_node(
    'MatMul',
    inputs=['X', 'W'],
    outputs=['Y'],
    name='matmul_node'
)

# Y + B -> Y2
add_node = helper.make_node(
    'Add',
    inputs=['Y', 'B'],
    outputs=['Y2'],
    name='add_node'
)

# ReLU(Y2) -> Z
relu_node = helper.make_node(
    'Relu',
    inputs=['Y2'],
    outputs=['Z'],
    name='relu_node'
)

graph = helper.make_graph(
    [matmul_node, add_node, relu_node],
    'ComplexModel',
    [X],
    [Z],
    [W, B]
)

model = helper.make_model(graph, producer_name='minios')
model.opset_import[0].version = 13

onnx.save(model, 'complex_model.onnx')
print("Generated complex_model.onnx")
