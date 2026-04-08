#!/usr/bin/env python3
"""
generate_npy_inputs.py
-----------------------
Generates the .npy input tensor files for the Ubuntu benchmark environment.
Must produce byte-identical data to the .bin files already in src/storage/bench/inputs/
(same seed=42, float32, shape=[1,3,224,224]).

Usage:
    python3 scripts/ubuntu_bench/generate_npy_inputs.py
"""
import numpy as np
import os

# Same seed and shape as download_models.py
np.random.seed(42)
input_shape = (1, 3, 224, 224)
dummy_input = np.random.randn(*input_shape).astype(np.float32)

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "bench", "inputs")
os.makedirs(OUTPUT_DIR, exist_ok=True)

MODELS = ["squeezenet", "mobilenetv2", "resnet50"]

for name in MODELS:
    npy_path = os.path.join(OUTPUT_DIR, f"{name}.npy")
    np.save(npy_path, dummy_input)
    print(f"  Saved {npy_path}  ({os.path.getsize(npy_path):,} bytes)")

print(f"\nAll {len(MODELS)} .npy inputs generated in {OUTPUT_DIR}/")
print("Shape: (1, 3, 224, 224)  dtype: float32  seed: 42")
