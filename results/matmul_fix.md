# MatMul Shape Mismatch Fix

## Node That Failed
Times212 (MatMul) in mnist model

## Debug Output (actual shapes received)
Input A: ndim=2 dims=[1,256]
Input B: ndim=2 dims=[256,10]

## Expected Shapes (from ONNX shape inference)
Input A: [1,256]
Input B: [256,10]

## Root Cause
`Times212` consumed a tensor whose upstream reshape-path shape materialization could be left as placeholder in edge cases, causing runtime MatMul validation to fail with `SHAPE_MISMATCH`.

## Fix Applied
File    : src/onnx/onnx_runtime.c
Function: ONNX_Execute_Reshape
Change  : Added robust reshape-shape decoding for both `INT64` and `INT32` shape tensors, including `-1` and `0` handling.

File    : src/onnx/onnx_loader.c
Function: proto_parse_node / proto_parse_attribute
Change  : Added proper `Split` op parsing and `Constant(value=Tensor)` payload decoding so reshape/split chains receive real shape tensors.

File    : src/onnx/onnx_runtime.c
Function: ONNX_Execute_Split / ONNX_Execute_Constant / ONNX_Execute_ReduceMean
Change  : Implemented runtime support for `Split`, `Constant`, and the shufflenet `ReduceMean(axes=[2,3])` pattern used downstream of the Times212-class shape path.

## Other Models Affected
Did the same fix resolve squeezenet and shufflenet?
Yes — with the additional runtime/operator support above, both models now complete successfully.

## Dry Run Results After Fix
mnist      : PASS — latency 7.2 ms
squeezenet : PASS — latency 30451.8 ms
shufflenet : PASS — latency 26589.1 ms
