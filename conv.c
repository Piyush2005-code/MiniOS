#include "operator.h"
#include "tensor.h"
#include "attr_utils.h"
#include <string.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

int operator_conv(const tensor_t inputs[], int num_inputs,
                  tensor_t outputs[], int num_outputs,
                  const attribute_t attrs[], int num_attrs) {
    if (num_inputs != 2 || num_outputs != 1)
        return -1; // Expect X and W

    const tensor_t *X = &inputs[0];  // input tensor (N x C x H x W)
    const tensor_t *W = &inputs[1];  // weights (M x C x kH x kW)
    tensor_t *Y = &outputs[0];       // output (N x M x Hout x Wout)

    if (X->rank != 4 || W->rank != 4 || X->dtype != DT_FLOAT || W->dtype != DT_FLOAT)
        return -2;

    // Extract attributes (with defaults from ONNX spec)
    int64_t *pads = NULL;
    uint32_t pads_count = 0;
    int64_t *strides = NULL;
    uint32_t strides_count = 0;
    int64_t *dilations = NULL;
    uint32_t dilations_count = 0;
    int64_t group = 1;
    const char *auto_pad = NULL;

    find_ints_attr(attrs, num_attrs, "pads", (const int64_t**)&pads, &pads_count);
    find_ints_attr(attrs, num_attrs, "strides", (const int64_t**)&strides, &strides_count);
    find_ints_attr(attrs, num_attrs, "dilations", (const int64_t**)&dilations, &dilations_count);
    find_int_attr(attrs, num_attrs, "group", &group);
    auto_pad = find_string_attr(attrs, num_attrs, "auto_pad");

    // Default values (if attributes missing)
    int64_t stride_h = (strides && strides_count >= 1) ? strides[0] : 1;
    int64_t stride_w = (strides && strides_count >= 2) ? strides[1] : stride_h;
    int64_t pad_top = (pads && pads_count >= 1) ? pads[0] : 0;
    int64_t pad_left = (pads && pads_count >= 2) ? pads[1] : pad_top;
    int64_t pad_bottom = (pads && pads_count >= 3) ? pads[2] : pad_top;
    int64_t pad_right = (pads && pads_count >= 4) ? pads[3] : pad_left;
    int64_t dilation_h = (dilations && dilations_count >= 1) ? dilations[0] : 1;
    int64_t dilation_w = (dilations && dilations_count >= 2) ? dilations[1] : dilation_h;

    // Input dimensions
    int64_t N = X->dims[0];
    int64_t C = X->dims[1];
    int64_t H = X->dims[2];
    int64_t W_in = X->dims[3];
    int64_t M = W->dims[0];      // number of output channels
    int64_t kH = W->dims[2];
    int64_t kW = W->dims[3];

    // Compute output dimensions (simplified, ignoring auto_pad)
    int64_t Hout = (H + pad_top + pad_bottom - (dilation_h * (kH - 1) - 1)) / stride_h;
    int64_t Wout = (W_in + pad_left + pad_right - (dilation_w * (kW - 1) - 1)) / stride_w;

    const float *x = (const float*)X->data;
    const float *w = (const float*)W->data;
    float *y = (float*)Y->data;

    // Direct convolution loops
    for (int n = 0; n < N; n++) {
        for (int m = 0; m < M; m++) {
            for (int oh = 0; oh < Hout; oh++) {
                for (int ow = 0; ow < Wout; ow++) {
                    float sum = 0.0f;
                    for (int c = 0; c < C / group; c++) { // simplified group handling
                        for (int kh = 0; kh < kH; kh++) {
                            for (int kw = 0; kw < kW; kw++) {
                                int ih = oh * stride_h + kh * dilation_h - pad_top;
                                int iw = ow * stride_w + kw * dilation_w - pad_left;
                                if (ih >= 0 && ih < H && iw >= 0 && iw < W_in) {
                                    int x_idx = n * (C * H * W_in) +
                                                (c * group) * H * W_in + // group offset (simplified)
                                                ih * W_in + iw;
                                    int w_idx = m * (C * kH * kW) +
                                                (c * group) * kH * kW +
                                                kh * kW + kw;
                                    sum += x[x_idx] * w[w_idx];
                                }
                            }
                        }
                    }
                    int y_idx = n * (M * Hout * Wout) + m * (Hout * Wout) + oh * Wout + ow;
                    y[y_idx] = sum;
                }
            }
        }
    }
    return 0;
}