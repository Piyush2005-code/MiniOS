import re

path = '/Users/piyushsinghbhati/Documents/Programming/MiniOS/src/onnx/onnx_runtime.c'
with open(path, 'r') as f:
    text = f.read()

pattern = r'(if \(\(uint32_t\)B->shape\.dims\[1\] != K\) \{)(\s+HAL_UART_PutString\("\[ONNX\] GEMM: shape mismatch\\n"\);)'
replacement = r'\1\n            char msg[100];\n            mini_snprintf(msg, sizeof(msg), "[ONNX] GEMM mismatch: A[%d,%d] B[%d,%d]\\n", M, K, (uint32_t)B->shape.dims[0], (uint32_t)B->shape.dims[1]);\n            HAL_UART_PutString(msg);\n'

text = re.sub(pattern, replacement, text)

with open(path, 'w') as f:
    f.write(text)
