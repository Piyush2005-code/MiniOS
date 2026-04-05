# MiniOS Latency Bottleneck Analysis (Codebase Deep Dive)

## What Was Investigated
- Runtime inference path in `src/onnx/onnx_runtime.c`
- Benchmark path in `src/onnx/onnx_cmds.c`
- Background daemon telemetry in `src/kernel/daemon.c`

## Primary Bottlenecks Found

1. Heavy inference-time logging in hot path
- `ONNX_Runtime_Inference` printed:
  - `[ONNX] Starting inference...`
  - per-node `Executing: ...`
  - `[ONNX] Inference complete!`
- `ONNX_Execute_Transpose` also printed large debug dumps every call.
- This added large UART overhead and scheduler churn during benchmark loops.

2. Forced per-node yielding in hot path
- `ONNX_Runtime_Inference` called `THREAD_Yield()` after every node.
- This increased scheduler overhead and let low-priority telemetry threads run during timed execution.

3. Convolution kernel ignored ONNX `group` semantics
- `ONNX_Execute_Conv` previously looped over all input channels for every output channel.
- For depthwise/grouped conv models (notably shufflenet), this massively over-computed and used wrong weight-channel indexing.
- Result: huge latency inflation.

4. No optimized path for very common 1x1 conv
- Squeezenet has many 1x1 conv layers.
- Generic kernel paid extra loop/branch costs not needed for 1x1 no-pad no-dilation case.

## Fixes Applied

### A) Runtime controls for benchmark mode
- Added runtime control API in:
  - `include/onnx/onnx_runtime.h`
  - `src/onnx/onnx_runtime.c`
- New controls:
  - `ONNX_Runtime_SetVerbose(bool)`
  - `ONNX_Runtime_SetYieldBetweenNodes(bool)`

### B) Daemon telemetry control
- Added daemon telemetry API in:
  - `include/kernel/daemon.h`
  - `src/kernel/daemon.c`
- New controls:
  - `DAEMON_SetTelemetryEnabled(bool)`
  - `DAEMON_GetTelemetryEnabled(void)`
- Periodic `[CLOCK]`, `[MEMW]`, `[RTMON]` prints are now suppressible during benchmark timing.

### C) Benchmark command now enters low-overhead runtime mode
- Updated `cmd_run_bench` in `src/onnx/onnx_cmds.c`:
  - temporarily disables runtime verbose logging
  - disables per-node yield
  - disables daemon telemetry
  - restores prior settings after benchmark

### D) Convolution kernel corrected and optimized
- Updated `ONNX_Execute_Conv` in `src/onnx/onnx_runtime.c`:
  - implemented proper grouped-conv indexing (`group`, `c_in/group`, `c_out/group`)
  - added dilation + full pad handling in output geometry/indexing
  - added fast path for 1x1 grouped conv with no pad/dilation

## Measurement Summary

### Before grouped-conv + 1x1 optimizations (same session, runs=1)
- mnist: `6.5 ms`
- squeezenet: `33002.3 ms`
- shufflenet: `30777.1 ms`

### After grouped-conv + 1x1 optimizations (same session, runs=1)
- mnist: `6.3 ms`
- squeezenet: `24019.3 ms`
- shufflenet: `5756.2 ms`

### Improvement (runs=1 reference)
- mnist: `~3.08%` faster
- squeezenet: `~27.22%` faster
- shufflenet: `~81.30%` faster

### Post-fix stability sample (runs=5, warmup=1)
- mnist mean: `5.56 ms`
- squeezenet mean: `24788.08 ms`
- shufflenet mean: `5715.48 ms`

## Remaining Bottlenecks
- Generic 3x3 convolution is still scalar and naive (no blocking/SIMD/GEMM transform).
- ONNX loader still emits very verbose parse logs (affects wall-clock benchmarking flow, not per-inference timed section).
- No specialized depthwise 3x3 micro-kernel yet.

## Next High-Impact Optimizations
1. Add dedicated depthwise 3x3 kernel path.
2. Add im2col+GEMM path (or direct blocked kernel) for common conv shapes.
3. Add compile-time/runtime switch for ONNX loader parsing logs.
4. Optionally use ARM NEON intrinsics for Conv/GEMM hot loops.
