import torch
from torch import nn
from torch.nn import functional as F

class ImageClassifierModel(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 16, kernel_size = 3, stride = 1, padding = 1)
        self.conv2 = nn.Conv2d(16, 8, kernel_size = 5, stride = 2, padding = 2)
        self.fc1 = nn.Linear(16 * 5 * 5, 84)
        self.fc2 = nn.Linear(84, 10)

    def forward(self, x):
        x = F.max_pool2d(F.relu(self.conv1(x)), (2, 2))
        x = F.max_pool2d(F.relu(self.conv2(x)), 2)
        x = torch.flatten(x, 1)
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        x = self.fc3(x)
        return x


model = ImageClassifierModel()
dummy_input = (torch.randn(1, 1, 32, 32))
model.eval()  # Crucial: Set model to evaluation mode

# 2. Prepare Dummy Input
# The input shape must match the model's expected input (Batch, Channels, Height, Width)

# 3. Export the Model
onnx_file_name = "Image_Classifier_Model.onnx"

torch.onnx.export(
    model,                  # The model to be exported
    dummy_input,            # Sample input
    onnx_file_name,         # Output file name
    export_params=True,     # Store trained parameter weights
    opset_version=14,       # ONNX version (14+ recommended for modern models)
    do_constant_folding=True, # Optimize constant operations
    input_names=['input'],   # Rename inputs
    output_names=['output'], # Rename outputs
    dynamic_axes={          # Enable variable batch size
        'input': {0: 'batch_size'},
        'output': {0: 'batch_size'}
    }
)

print(f"Model exported to {onnx_file_name}")
