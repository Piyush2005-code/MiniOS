#include "operator.h"
#include "tensor.h"
#include "attr_utils.h"

int operator_gemm(const tensor_t inputs[], int num_inputs,
                  tensor_t outputs[], int num_outputs,
                  const attribute_t attrs[], int num_attrs) {
    if (num_inputs != 3 || num_outputs != 1) // A, B, C
        return -1;

    const tensor_t *A = &inputs[0];
    const tensor_t *B = &inputs[1];
    const tensor_t *C = &inputs[2];
    tensor_t *Y = &outputs[0];

    if (A->dtype != DT_FLOAT || B->dtype != DT_FLOAT || C->dtype != DT_FLOAT)
        return -2;

    // Attributes
    double alpha = 1.0, beta = 1.0;
    int64_t transA = 0, transB = 0;
    find_float_attr(attrs, num_attrs, "alpha", &alpha);
    find_float_attr(attrs, num_attrs, "beta", &beta);
    find_int_attr(attrs, num_attrs, "transA", &transA);
    find_int_attr(attrs, num_attrs, "transB", &transB);

    // Dimensions after optional transpose
    int64_t M = transA ? A->dims[1] : A->dims[0];
    int64_t K = transA ? A->dims[0] : A->dims[1];
    int64_t N = transB ? B->dims[0] : B->dims[1];

    if ((transB ? B->dims[1] : B->dims[0]) != K)
        return -3; // incompatible shapes

    const float *a = (const float*)A->data;
    const float *b = (const float*)B->data;
    const float *c = (const float*)C->data;
    float *y = (float*)Y->data;

    // Simple scalar gemm: Y = alpha * A * B + beta * C
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++) {
                float a_ik = transA ? a[k * M + i] : a[i * K + k];
                float b_kj = transB ? b[j * K + k] : b[k * N + j];
                sum += a_ik * b_kj;
            }
            y[i * N + j] = alpha * sum + beta * c[i * N + j];
        }
    }

    return 0;
}