#include "operator.h"
#include "tensor.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

int operator_relu(const tensor_t inputs[], int num_inputs,
                  tensor_t outputs[], int num_outputs,
                  const attribute_t attrs[], int num_attrs) {
    (void)attrs; (void)num_attrs;

    if (num_inputs != 1 || num_outputs != 1)
        return -1;

    const tensor_t *X = &inputs[0];
    tensor_t *Y = &outputs[0];

    if (X->dtype != DT_FLOAT)
        return -2;

    size_t n = tensor_num_elements(X);
    const float *x = (const float*)X->data;
    float *y = (float*)Y->data;

#ifdef __ARM_NEON
    size_t i = 0;
    for (; i + 3 < n; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vmaxq_f32(vx, vdupq_n_f32(0.0f));
        vst1q_f32(y + i, vy);
    }
    for (; i < n; i++)
        y[i] = (x[i] > 0) ? x[i] : 0.0f;
#else
    for (size_t i = 0; i < n; i++)
        y[i] = (x[i] > 0) ? x[i] : 0.0f;
#endif

    return 0;
}