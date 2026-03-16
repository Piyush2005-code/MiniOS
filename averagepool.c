#include "operator.h"
#include "tensor.h"
#include "attr_utils.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

int operator_averagepool(const tensor_t inputs[], int num_inputs,
                         tensor_t outputs[], int num_outputs,
                         const attribute_t attrs[], int num_attrs) {
    if (num_inputs != 1 || num_outputs != 1)
        return -1;

    const tensor_t *X = &inputs[0];
    tensor_t *Y = &outputs[0];

    if (X->rank != 4 || X->dtype != DT_FLOAT)
        return -2;

    // Extract attributes (similar to conv)
    int64_t *kernel_shape = NULL;
    uint32_t kernel_count = 0;
    int64_t *strides = NULL;
    uint32_t strides_count = 0;
    int64_t *pads = NULL;
    uint32_t pads_count = 0;
    const char *auto_pad = NULL;

    find_ints_attr(attrs, num_attrs, "kernel_shape", (const int64_t**)&kernel_shape, &kernel_count);
    find_ints_attr(attrs, num_attrs, "strides", (const int64_t**)&strides, &strides_count);
    find_ints_attr(attrs, num_attrs, "pads", (const int64_t**)&pads, &pads_count);
    auto_pad = find_string_attr(attrs, num_attrs, "auto_pad");

    if (!kernel_shape || kernel_count < 2) return -3;

    int64_t kH = kernel_shape[0];
    int64_t kW = kernel_shape[1];
    int64_t stride_h = (strides && strides_count >= 1) ? strides[0] : kH;
    int64_t stride_w = (strides && strides_count >= 2) ? strides[1] : kW;
    int64_t pad_top = (pads && pads_count >= 1) ? pads[0] : 0;
    int64_t pad_left = (pads && pads_count >= 2) ? pads[1] : 0;
    int64_t pad_bottom = (pads && pads_count >= 3) ? pads[2] : pad_top;
    int64_t pad_right = (pads && pads_count >= 4) ? pads[3] : pad_left;

    int64_t N = X->dims[0];
    int64_t C = X->dims[1];
    int64_t H = X->dims[2];
    int64_t W_in = X->dims[3];

    int64_t Hout = (H + pad_top + pad_bottom - kH) / stride_h + 1;
    int64_t Wout = (W_in + pad_left + pad_right - kW) / stride_w + 1;

    const float *x = (const float*)X->data;
    float *y = (float*)Y->data;

    for (int n = 0; n < N; n++) {
        for (int c = 0; c < C; c++) {
            for (int oh = 0; oh < Hout; oh++) {
                for (int ow = 0; ow < Wout; ow++) {
                    float sum = 0.0f;
                    int count = 0;
                    for (int kh = 0; kh < kH; kh++) {
                        for (int kw = 0; kw < kW; kw++) {
                            int ih = oh * stride_h + kh - pad_top;
                            int iw = ow * stride_w + kw - pad_left;
                            if (ih >= 0 && ih < H && iw >= 0 && iw < W_in) {
                                int idx = n * (C * H * W_in) + c * (H * W_in) + ih * W_in + iw;
                                sum += x[idx];
                                count++;
                            }
                        }
                    }
                    int y_idx = n * (C * Hout * Wout) + c * (Hout * Wout) + oh * Wout + ow;
                    y[y_idx] = (count > 0) ? sum / count : 0.0f;
                }
            }
        }
    }
    return 0;
}