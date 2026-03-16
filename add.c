#include "operator.h"
#include "tensor.h"
#include <string.h>

int operator_add(const tensor_t inputs[], int num_inputs,
                 tensor_t outputs[], int num_outputs,
                 const attribute_t attrs[], int num_attrs) {
    // Add expects two inputs and one output
    if (num_inputs != 2 || num_outputs != 1)
        return -1;

    const tensor_t *a = &inputs[0];
    const tensor_t *b = &inputs[1];
    tensor_t *c = &outputs[0];

    // Basic checks: same shape, same type, etc.
    if (!tensor_shape_equal(a, b) || a->dtype != DT_FLOAT)
        return -2;

    size_t num_elements = tensor_num_elements(a);
    float *out_data = (float*)c->data;
    const float *a_data = (const float*)a->data;
    const float *b_data = (const float*)b->data;

    // Simple element‑wise addition (can be NEON‑optimised later)
    for (size_t i = 0; i < num_elements; i++) {
        out_data[i] = a_data[i] + b_data[i];
    }
    return 0;
}