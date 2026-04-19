#!/usr/bin/env python3
"""
run_bench.py — Ubuntu ONNX Benchmark Runner
--------------------------------------------
CLI interface mirrors the MiniOS /bench/run_bench binary exactly:

  python3 /bench/run_bench.py \\
    --model  /bench/models/squeezenet.onnx \\
    --input  /bench/inputs/squeezenet.npy \\
    --runs   200 \\
    --warmup 10 \\
    --out    /bench/results/

JSON output schema (identical to MiniOS):
  {
    "model": "<stem>",
    "os": "ubuntu",
    "runs": <int>,
    "warmup": <int>,
    "latencies_ms": [<float>, ...],
    "peak_rss_kb": <int>,
    "model_load_ms": <float>
  }

Thread pinning is enforced via SessionOptions; environment variables
(OMP_NUM_THREADS=1, etc.) should also be set in /etc/environment.
"""

import argparse
import json
import os
import resource
import time

import numpy as np
import onnxruntime as ort


def parse_args():
    p = argparse.ArgumentParser(description="ONNX Benchmark Runner (Ubuntu)")
    p.add_argument("--model",  required=True, help="Path to .onnx model file")
    p.add_argument("--input",  required=True, help="Path to input .npy tensor file")
    p.add_argument("--runs",   type=int, default=200, help="Number of timed inference runs")
    p.add_argument("--warmup", type=int, default=10,  help="Number of warmup runs (not recorded)")
    p.add_argument("--out",    required=True,
                   help="Output directory (results written as <model_stem>.json)")
    return p.parse_args()


def main():
    args = parse_args()

    # ------------------------------------------------------------------ #
    #  Derive output path                                                  #
    # ------------------------------------------------------------------ #
    model_stem = os.path.splitext(os.path.basename(args.model))[0]
    os.makedirs(args.out, exist_ok=True)
    out_file = os.path.join(args.out, f"{model_stem}.json")

    # ------------------------------------------------------------------ #
    #  Load input tensor                                                   #
    # ------------------------------------------------------------------ #
    input_tensor = np.load(args.input)

    # ------------------------------------------------------------------ #
    #  Load model (timed separately)                                       #
    # ------------------------------------------------------------------ #
    sess_opts = ort.SessionOptions()
    sess_opts.intra_op_num_threads = 1
    sess_opts.inter_op_num_threads = 1
    sess_opts.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL

    load_start = time.perf_counter()
    session = ort.InferenceSession(
        args.model,
        sess_options=sess_opts,
        providers=["CPUExecutionProvider"],
    )
    load_end = time.perf_counter()
    model_load_ms = (load_end - load_start) * 1000.0

    # Discover the first input name
    input_name = session.get_inputs()[0].name

    # ------------------------------------------------------------------ #
    #  Warmup (not recorded)                                               #
    # ------------------------------------------------------------------ #
    for _ in range(args.warmup):
        session.run(None, {input_name: input_tensor})

    # ------------------------------------------------------------------ #
    #  Timed runs                                                          #
    # ------------------------------------------------------------------ #
    latencies_ms = []
    for _ in range(args.runs):
        t0 = time.perf_counter()
        session.run(None, {input_name: input_tensor})
        t1 = time.perf_counter()
        latencies_ms.append((t1 - t0) * 1000.0)

    # ------------------------------------------------------------------ #
    #  Peak RSS (Linux: ru_maxrss is in KB)                               #
    # ------------------------------------------------------------------ #
    peak_rss_kb = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss

    # ------------------------------------------------------------------ #
    #  Write JSON result                                                   #
    # ------------------------------------------------------------------ #
    result = {
        "model":         model_stem,
        "os":            "ubuntu",
        "runs":          args.runs,
        "warmup":        args.warmup,
        "latencies_ms":  latencies_ms,
        "peak_rss_kb":   peak_rss_kb,
        "model_load_ms": model_load_ms,
    }

    with open(out_file, "w") as f:
        json.dump(result, f, indent=2)

    print(f"[run_bench] Results written to {out_file}")
    print(f"[run_bench] model_load_ms={model_load_ms:.3f}  "
          f"mean_latency_ms={sum(latencies_ms)/len(latencies_ms):.3f}  "
          f"peak_rss_kb={peak_rss_kb}")


if __name__ == "__main__":
    main()
