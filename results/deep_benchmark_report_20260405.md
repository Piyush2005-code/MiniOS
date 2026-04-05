# MiniOS vs Ubuntu: Deep Benchmark Analysis (ML Inference)

- Generated: 2026-04-05 23:57:30
- Scope: `mnist`, `squeezenet`, `shufflenet` on ARM64 QEMU (`virt`, `cortex-a57`, single core, 2GB RAM).
- Objective: identify where MiniOS performs better and where it still lacks versus Ubuntu for ML inference workloads.

## Dataset and Methodology

- Ubuntu reference uses existing 50-run result files:
  - `results/mnist_ubuntu.json`
  - `results/squeezenet_ubuntu.json`
  - `results/shufflenet_ubuntu.json`
- MiniOS uses fresh post-optimization logs from this session:
  - `mnist`: n=50 from `/tmp/minios_fresh_50.log`
  - `squeezenet`: n=15 aggregated from `/tmp/minios_sq_batches.log` + `/tmp/minios_sq_fresh3.log`
  - `shufflenet`: n=15 aggregated from `/tmp/minios_hf_batches.log` + `/tmp/minios_hf_fresh3.log`
- Metrics: mean, median, p95, CV (jitter), throughput (inf/s), model load time, peak memory.
- Caveat: sample sizes differ by model for MiniOS due environment run-output constraints on long silent loops.

## Core Comparison

| Model | OS | n | Mean (ms) | Median (ms) | p95 (ms) | CV % | Throughput (inf/s) | Load (ms) | Peak Mem (KB) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mnist | MiniOS | 50 | 4.82 | 4.80 | 4.90 | 2.06 | 207.555 | 4.00 | 155 |
| mnist | Ubuntu | 50 | 8.55 | 8.50 | 9.14 | 3.49 | 117.026 | 485.65 | 46824 |
| squeezenet | MiniOS | 15 | 2805.63 | 2691.70 | 3431.60 | 8.27 | 0.356 | 7.00 | 23168 |
| squeezenet | Ubuntu | 50 | 2992.99 | 2991.77 | 3093.46 | 1.83 | 0.334 | 488.87 | 57892 |
| shufflenet | MiniOS | 18 | 1133.71 | 1113.15 | 1282.20 | 4.65 | 0.882 | 19.00 | 30085 |
| shufflenet | Ubuntu | 50 | 1386.98 | 1373.10 | 1484.00 | 2.71 | 0.721 | 1023.53 | 66908 |

## Relative Advantage (Ubuntu / MiniOS)

| Model | Latency Speedup | Load Speedup | Memory Advantage |
|---|---:|---:|---:|
| mnist | 1.77x | 121.41x | 302.09x |
| squeezenet | 1.07x | 69.84x | 2.50x |
| shufflenet | 1.22x | 53.87x | 2.22x |

## Sidecar Load-Path Impact (This Session)

| Model | Before (protobuf) ms | After (custom sidecar) ms | Improvement |
|---|---:|---:|---:|
| mnist | 11.00 | 5.00 | 54.5% |
| squeezenet | 15.00 | 7.00 | 53.3% |
| shufflenet | 38.00 | 19.00 | 50.0% |

## Where MiniOS Performs Better

- MiniOS has lower mean inference latency than Ubuntu on all three benchmarked models.
- MiniOS model load path is dramatically faster on all models (custom binary + embedded flow).
- MiniOS reports much lower peak memory footprint than Ubuntu across all tested models.
- For all tested models, MiniOS now shows stronger cold-start behavior due lightweight graph loading and custom binary format handling.
- End-to-end throughput advantage is strongest on `mnist`, and still material on `squeezenet` and `shufflenet`.

## Where MiniOS Still Lacks

- squeezenet: MiniOS jitter (CV=8.27%) is higher than Ubuntu (CV=1.83%).
- shufflenet: MiniOS jitter (CV=4.65%) is higher than Ubuntu (CV=2.71%).
- Benchmark sample sizes are asymmetric for this fresh run set (MiniOS: mnist n=50, squeezenet n=15, shufflenet n=15 vs Ubuntu n=50 each), so confidence differs by model.
- MiniOS currently depends on pre-converted `.mio` sidecars for best load-time performance; without this conversion step, load-time gains are reduced.
- Some long silent benchmark runs were interrupted in this environment, indicating toolchain/orchestration fragility for very long unattended loops.
- Determinism under sustained heavy loops can still improve; occasional outlier latency spikes were observed in MiniOS `squeezenet` samples.
- Measurement semantics differ (`peak_rss_kb` in MiniOS reflects arena-focused accounting, Ubuntu reflects process RSS), so memory comparison is directional, not fully apples-to-apples.

## Engineering Interpretation by Perspective

### 1. Latency / Throughput Perspective
- MiniOS is currently faster on all three workloads in this environment, indicating optimized operator paths and scheduler/telemetry controls are effective.
### 2. Cold-Start / Deployment Perspective
- MiniOS has a major advantage in `model_load_ms`, which is critical for short-lived inference jobs and fast iteration loops.
### 3. Memory Efficiency Perspective
- MiniOS remains significantly leaner in reported footprint, an important strength for constrained deployments.
### 4. Stability / Tail-Latency Perspective
- Ubuntu remains more consistent in coefficient-of-variation on at least some models; MiniOS tail control needs additional hardening.
### 5. Operability Perspective
- MiniOS performance is best when sidecar artifacts exist and runtime flags are correctly tuned; this introduces operational coupling to tooling/pipelines.

## Recommended Next Actions

1. Add periodic progress output or heartbeats for long benchmark loops to avoid orchestration interruptions in automated environments.
2. Run a fully symmetric 50-run MiniOS sweep for all models in a non-interruptible harness and refresh this report with matched sample sizes.
3. Add p99-tail regression gates in CI for `squeezenet` and `shufflenet` to prevent jitter regressions.
4. Standardize sidecar generation in build/release pipelines so load-time advantage is guaranteed in production.
