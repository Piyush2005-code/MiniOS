#!/usr/bin/env python3
import os
import numpy as np

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "src", "storage")

def generate_data():
    os.makedirs(OUT_DIR, exist_ok=True)
    np.random.seed(42)
    
    print(f"Generating 10 synthetic MNIST test samples in {OUT_DIR}")
    labels = []
    
    for i in range(10):
        # Generate [1, 1, 28, 28] float32 tensor
        # values between 0.0 and 1.0
        data = np.random.rand(1, 1, 28, 28).astype(np.float32)
        
        filepath = os.path.join(OUT_DIR, f"mnist_test_{i}.bin")
        data.tofile(filepath)
        
        # Random label just for format
        label = np.random.randint(0, 10)
        labels.append(str(label))
        print(f"  Saved {filepath} (label={label})")
        
    with open(os.path.join(OUT_DIR, "mnist_labels.txt"), "w") as f:
        f.write("\n".join(labels))
        
if __name__ == "__main__":
    generate_data()
