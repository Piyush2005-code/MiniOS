import os
import urllib.request
import numpy as np
import shutil

MODELS = {
    "squeezenet": "https://github.com/onnx/models/raw/main/validated/vision/classification/squeezenet/model/squeezenet1.0-12.onnx",
    "mobilenetv2": "https://github.com/onnx/models/raw/main/validated/vision/classification/mobilenet/model/mobilenetv2-12.onnx",
    "resnet50": "https://github.com/onnx/models/raw/main/validated/vision/classification/resnet/model/resnet50-v2-7.onnx"
}

MINIOS_MODELS_DIR = "src/storage/bench/models"
MINIOS_INPUTS_DIR = "src/storage/bench/inputs"
UBUNTU_MODELS_DIR = "../vm-unikernel-benchmark/ubuntu_vm/shared/bench/models"
UBUNTU_INPUTS_DIR = "../vm-unikernel-benchmark/ubuntu_vm/shared/bench/inputs"

os.makedirs(MINIOS_MODELS_DIR, exist_ok=True)
os.makedirs(MINIOS_INPUTS_DIR, exist_ok=True)
os.makedirs(UBUNTU_MODELS_DIR, exist_ok=True)
os.makedirs(UBUNTU_INPUTS_DIR, exist_ok=True)

np.random.seed(42)
input_shape = (1, 3, 224, 224)
dummy_input = np.random.randn(*input_shape).astype(np.float32)

print("Generating input tensors...")
for ds in ["squeezenet", "mobilenetv2", "resnet50"]:
    minios_input = os.path.join(MINIOS_INPUTS_DIR, f"{ds}.bin")
    ubuntu_input = os.path.join(UBUNTU_INPUTS_DIR, f"{ds}.npy")
    with open(minios_input, "wb") as f:
        f.write(dummy_input.tobytes())
    np.save(ubuntu_input, dummy_input)

print("Inputs generated. Downloading models...")
for name, url in MODELS.items():
    minios_path = os.path.join(MINIOS_MODELS_DIR, f"{name}.onnx")
    ubuntu_path = os.path.join(UBUNTU_MODELS_DIR, f"{name}.onnx")
    
    if not os.path.exists(minios_path):
        print(f"Downloading {name}...")
        urllib.request.urlretrieve(url, minios_path)
    shutil.copy2(minios_path, ubuntu_path)

print("All done!")
