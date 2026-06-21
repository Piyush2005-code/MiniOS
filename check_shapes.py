import onnx
model = onnx.load("models/alexnet_tiny.onnx")
for vi in model.graph.value_info:
    shape = [d.dim_value for d in vi.type.tensor_type.shape.dim]
    print(f"{vi.name}: {shape}")
