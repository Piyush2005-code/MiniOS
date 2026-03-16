#include "operator.h"
#include "tensor.h"
#include <math.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

int operator_softmax(const tensor_t inputs[], int num_inputs,
                     tensor_t outputs[], int num_outputs,
                     const attribute_t attrs[], int num_attrs) {
    (void)attrs; (void)num_attrs;

    if (num_inputs != 1 || num_outputs != 1)
        return -1;

    const tensor_t *X = &inputs[0];
    tensor_t *Y = &outputs[0];

    if (X->dtype != DT_FLOAT)
        return -2;

    // Softmax over the last dimension (axis = -1)
    int axis = X->rank - 1;

    size_t outer_size = 1;
    for (int i = 0; i < axis; i++)
        outer_size *= X->dims[i];
    size_t inner_size = X->dims[axis];
    size_t total = outer_size * inner_size;

    const float *x = (const float*)X->data;
    float *y = (float*)Y->data;

    for (size_t i = 0; i < outer_size; i++) {
        // Find max for numerical stability
        float max_val = x[i * inner_size];
        for (size_t j = 1; j < inner_size; j++) {
            float val = x[i * inner_size + j];
            if (val > max_val) max_val = val;
        }

        // Compute exp and sum
        float sum = 0.0f;
        for (size_t j = 0; j < inner_size; j++) {
            float e = expf(x[i * inner_size + j] - max_val);
            y[i * inner_size + j] = e;
            sum += e;
        }

        // Normalize
        float inv_sum = 1.0f / sum;
        for (size_t j = 0; j < inner_size; j++) {
            y[i * inner_size + j] *= inv_sum;
        }
    }

    return 0;
}