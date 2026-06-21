#!/usr/bin/env python3
import os
import json
import time
import numpy as np
import onnxruntime as ort

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS_DIR = os.path.join(PROJECT_DIR, "src", "storage")
RESULTS_DIR = os.path.join(PROJECT_DIR, "results")

os.makedirs(RESULTS_DIR, exist_ok=True)

MODELS = [
    "tiny_mlp.onnx",
    "lenet5.onnx",
    "conv_bn_net.onnx",
    "alexnet_tiny.onnx",
    "vgg_nano.onnx",
    "resnet_micro.onnx",
    "transformer_tiny.onnx",
    "mnist_mlp.onnx",
    "squeezenet_nano.onnx",
    "mobilenet_tiny.onnx"
]

def run_baseline(iters=50, warmup=3):
    print(f"Running macOS ONNXRuntime Baseline ({iters} iterations)")
    
    for model_name in MODELS:
        model_path = os.path.join(MODELS_DIR, model_name)
        if not os.path.exists(model_path):
            print(f"  [SKIP] {model_name} (not found)")
            continue
            
        print(f"\n>>> Benchmarking {model_name} (macOS ORT)...")
        
        try:
            # Load session
            sess_options = ort.SessionOptions()
            sess_options.intra_op_num_threads = 1 # Match single-threaded MiniOS
            sess_options.inter_op_num_threads = 1
            sess = ort.InferenceSession(model_path, sess_options, providers=['CPUExecutionProvider'])
            
            # Prepare dummy input
            inputs = {}
            for inp in sess.get_inputs():
                shape = inp.shape
                # Replace dynamic dims if any
                shape = [1 if type(d) != int or d <= 0 else d for d in shape]
                
                dtype = np.float32
                if inp.type == 'tensor(float)': dtype = np.float32
                elif inp.type == 'tensor(int64)': dtype = np.int64
                
                inputs[inp.name] = np.random.rand(*shape).astype(dtype)
                if dtype == np.float32:
                    inputs[inp.name] = np.ones(shape, dtype=np.float32)

            # Warmup
            for _ in range(warmup):
                sess.run(None, inputs)
                
            latencies_us = []
            for _ in range(iters):
                t0 = time.perf_counter()
                sess.run(None, inputs)
                t1 = time.perf_counter()
                latencies_us.append(int((t1 - t0) * 1_000_000))
                
            # Calc stats
            mean_us = int(np.mean(latencies_us))
            median_us = int(np.median(latencies_us))
            p95_us = int(np.percentile(latencies_us, 95))
            p99_us = int(np.percentile(latencies_us, 99))
            min_us = int(np.min(latencies_us))
            max_us = int(np.max(latencies_us))
            stddev_us = float(np.std(latencies_us))
            cv_pct = float((stddev_us / mean_us) * 100) if mean_us > 0 else 0.0
            
            result = {
                "model": model_name,
                "iters": iters,
                "warmup": warmup,
                "mean_us": mean_us,
                "median_us": median_us,
                "p95_us": p95_us,
                "p99_us": p99_us,
                "min_us": min_us,
                "max_us": max_us,
                "stddev_us": stddev_us,
                "cv_pct": cv_pct,
                "arena_kb": 0, # N/A for ORT baseline
                "params": 0,   # Will be parsed by plotting script
                "status": "OK",
                "latencies_us": latencies_us
            }
            
            out_path = os.path.join(RESULTS_DIR, f"{model_name}_macos_50.json")
            with open(out_path, "w") as f:
                json.dump(result, f, indent=2)
            print(f"    Saved {out_path} (mean = {mean_us} us)")
            
        except Exception as e:
            print(f"    [ERROR] {e}")

if __name__ == "__main__":
    run_baseline()
