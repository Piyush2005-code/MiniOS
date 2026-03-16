#include "operator.h"
#include "tensor.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

int operator_matmul(const tensor_t inputs[], int num_inputs,
                    tensor_t outputs[], int num_outputs,
                    const attribute_t attrs[], int num_attrs) {
    (void)attrs; (void)num_attrs;

    if (num_inputs != 2 || num_outputs != 1)
        return -1;

    const tensor_t *A = &inputs[0];
    const tensor_t *B = &inputs[1];
    tensor_t *C = &outputs[0];

    // Assume rank 2, float, dimensions compatible
    if (A->rank != 2 || B->rank != 2 || A->dtype != DT_FLOAT || B->dtype != DT_FLOAT)
        return -2;

    int M = A->dims[0];
    int K = A->dims[1];
    int N = B->dims[1];

    // Check that B's first dim matches K
    if (B->dims[0] != K)
        return -3;

    const float *a = (const float*)A->data;
    const float *b = (const float*)B->data;
    float *c = (float*)C->data;

    // Simple scalar implementation (can be replaced with blocked NEON)
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++) {
                sum += a[i * K + k] * b[k * N + j];
            }
            c[i * N + j] = sum;
        }
    }

    return 0;
}