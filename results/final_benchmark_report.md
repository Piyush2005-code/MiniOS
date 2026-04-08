# MiniOS Final Benchmark Report

## Scope
Final end-to-end validation after ONNX runtime fixes for:
- `MatMul` shape mismatch at `Times212` (mnist)
- shufflenet execution blockers encountered in follow-up (`Split`, `Constant`, `ReduceMean` support gaps)

## Final Model Results (Latest Binary)

| Model | Status | Latency (ms) | Runtime Errors |
|---|---|---:|---|
| mnist | PASS | 7.2 | None |
| squeezenet | PASS | 30451.8 | None |
| shufflenet | PASS | 26589.1 | None |

Source logs:
- `/tmp/mnist_final.log`
- `/tmp/squeezenet_final.log`
- `/tmp/shufflenet_final.log`

## Fixes Implemented

1. `MatMul`/reshape-path stability
- `ONNX_Execute_Reshape` now handles both `INT64` and `INT32` shape tensors safely.

2. `Split` correctness
- Added `Split` op parsing (`onnx_loader.c`) and execution (`onnx_runtime.c`).
- Added split-shape propagation for command/benchmark paths (`onnx_cmds.c`).

3. `Constant` tensor materialization
- Added `Constant` op enum + execution path.
- Parsed `Constant(value=Tensor)` int64 payloads so downstream `Reshape` receives real shape tensors.

4. `ReduceMean` coverage for shufflenet
- Added support for spatial reduction pattern used in shufflenet (`axes=[2,3]` on NCHW tensors).

## Analysis

- The original `Times212` failure was not an isolated MatMul kernel arithmetic bug; it was part of shape-tensor propagation robustness in the runtime path.
- Once shape-chain correctness was enforced (`Constant -> Reshape -> Split`), shufflenet advanced significantly and exposed the next unsupported op (`ReduceMean` pattern), confirming staged runtime capability gaps rather than corrupted model artifacts.
- Final state is functionally stable for all three required benchmark models in this run.

## Recommendations

1. Add focused ONNX unit tests for:
- `Constant(value=Tensor[int64])`
- `Split` with attribute-driven splits
- `ReduceMean` over explicit axes lists

2. Add one integration test that exercises the exact shufflenet subgraph:
`Reshape -> Transpose -> Reshape -> Split -> ... -> ReduceMean`.

3. Keep runtime shape/operator diagnostics behind a compile-time debug flag for fast future bring-up.
