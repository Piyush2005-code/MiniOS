#!/usr/bin/env python3
"""
Simple ONNX Model Generator
Creates a basic neural network model in ONNX format
"""

import numpy as np
import onnx
from onnx import helper, TensorProto


def create_simple_linear_model():
    """
    Creates a simple linear model: Y = X * W + B
    Input: [batch_size, 3]
    Output: [batch_size, 2]
    """
    
    # Define input tensor
    X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [None, 3])
    
    # Define output tensor
    Y = helper.make_tensor_value_info('Y', TensorProto.FLOAT, [None, 2])
    
    # Create weight and bias tensors
    W = helper.make_tensor(
        name='W',
        data_type=TensorProto.FLOAT,
        dims=[3, 2],
        vals=np.random.randn(3, 2).flatten().tolist()
    )
    
    B = helper.make_tensor(
        name='B',
        data_type=TensorProto.FLOAT,
        dims=[2],
        vals=np.random.randn(2).flatten().tolist()
    )
    
    # Create MatMul node: X * W
    matmul_node = helper.make_node(
        'MatMul',
        inputs=['X', 'W'],
        outputs=['XW']
    )
    
    # Create Add node: XW + B
    add_node = helper.make_node(
        'Add',
        inputs=['XW', 'B'],
        outputs=['Y']
    )
    
    # Create the graph
    graph_def = helper.make_graph(
        nodes=[matmul_node, add_node],
        name='SimpleLinearModel',
        inputs=[X],
        outputs=[Y],
        initializer=[W, B]
    )
    
    # Create the model
    model_def = helper.make_model(graph_def, producer_name='minios-onnx-generator')
    model_def.opset_import[0].version = 13
    
    # Check the model
    onnx.checker.check_model(model_def)
    
    return model_def


def create_simple_cnn_model():
    """
    Creates a simple CNN model with Conv2D + ReLU
    Input: [batch_size, 1, 28, 28]
    Output: [batch_size, 8, 26, 26]
    """
    
    # Define input tensor (e.g., 28x28 grayscale image)
    X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [None, 1, 28, 28])
    
    # Define output tensor
    Y = helper.make_tensor_value_info('Y', TensorProto.FLOAT, [None, 8, 26, 26])
    
    # Create convolutional kernel (8 filters, 3x3 kernel)
    W = helper.make_tensor(
        name='conv_weight',
        data_type=TensorProto.FLOAT,
        dims=[8, 1, 3, 3],  # [out_channels, in_channels, kernel_h, kernel_w]
        vals=np.random.randn(8, 1, 3, 3).flatten().tolist()
    )
    
    # Create bias
    B = helper.make_tensor(
        name='conv_bias',
        data_type=TensorProto.FLOAT,
        dims=[8],
        vals=np.random.randn(8).flatten().tolist()
    )
    
    # Create Conv node
    conv_node = helper.make_node(
        'Conv',
        inputs=['X', 'conv_weight', 'conv_bias'],
        outputs=['conv_out'],
        kernel_shape=[3, 3],
        pads=[0, 0, 0, 0]
    )
    
    # Create ReLU node
    relu_node = helper.make_node(
        'Relu',
        inputs=['conv_out'],
        outputs=['Y']
    )
    
    # Create the graph
    graph_def = helper.make_graph(
        nodes=[conv_node, relu_node],
        name='SimpleCNNModel',
        inputs=[X],
        outputs=[Y],
        initializer=[W, B]
    )
    
    # Create the model
    model_def = helper.make_model(graph_def, producer_name='minios-onnx-generator')
    model_def.opset_import[0].version = 13
    
    # Check the model
    onnx.checker.check_model(model_def)
    
    return model_def


def main():
    """Generate and save ONNX models"""
    
    print("Generating simple linear model...")
    linear_model = create_simple_linear_model()
    onnx.save(linear_model, 'simple_linear.onnx')
    print("✓ Saved: simple_linear.onnx")
    print(f"  Input: [batch_size, 3]")
    print(f"  Output: [batch_size, 2]")
    print()
    
    print("Generating simple CNN model...")
    cnn_model = create_simple_cnn_model()
    onnx.save(cnn_model, 'simple_cnn.onnx')
    print("✓ Saved: simple_cnn.onnx")
    print(f"  Input: [batch_size, 1, 28, 28]")
    print(f"  Output: [batch_size, 8, 26, 26]")
    print()
    
    print("Models generated successfully!")
    print("\nYou can inspect the models using:")
    print("  - onnx.load('simple_linear.onnx')")
    print("  - Netron: https://netron.app/")


if __name__ == '__main__':
    main()
