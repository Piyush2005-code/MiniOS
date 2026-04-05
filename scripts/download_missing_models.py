import onnx
import numpy as np
import urllib.request
import os

def download_file(url, output_path):
    if not os.path.exists(output_path):
        print(f"Downloading {url} to {output_path}...")
        urllib.request.urlretrieve(url, output_path)

MODELS = {
    "mnist": "https://github.com/onnx/models/raw/main/validated/vision/classification/mnist/model/mnist-12.onnx",
    "shufflenet": "https://github.com/onnx/models/raw/main/validated/vision/classification/shufflenet/model/shufflenet-v2-12.onnx"
}

os.makedirs("src/storage/bench/models", exist_ok=True)
os.makedirs("src/storage/bench/inputs", exist_ok=True)
os.makedirs("scripts/ubuntu_bench/bench/inputs", exist_ok=True)

for name, url in MODELS.items():
    model_path = f"src/storage/bench/models/{name}.onnx"
    download_file(url, model_path)

    # Generate dummy input based on model name
    if name == "mnist":
        shape = (1, 1, 28, 28)
    elif name == "shufflenet":
        shape = (1, 3, 224, 224)

    np.random.seed(42)
    dummy_input = np.random.randn(*shape).astype(np.float32)

    bin_path = f"src/storage/bench/inputs/{name}.bin"
    npy_path = f"scripts/ubuntu_bench/bench/inputs/{name}.npy"

    with open(bin_path, "wb") as f:
        f.write(dummy_input.tobytes())
    np.save(npy_path, dummy_input)

    print(f"Generated input tensors for {name}")