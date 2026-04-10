/**
 * @file tiny_mlp_model.h
 * @brief Embedded int8-quantized Tiny MLP model for MiniOS-ESP8266
 *
 * Architecture: 4 inputs → Dense(8, ReLU) → Dense(4, Softmax) → 4 outputs
 * Quantization: float32 weights scaled to int8 (scale=0.01, zero_point=0)
 *
 * To regenerate from a float32 ONNX model, run:
 *   python3 esp8266/scripts/quantize_to_int8.py tiny_mlp.onnx tiny_mlp_model.h
 *
 * Memory cost: 72 weight bytes + 12 bias bytes = 84 bytes total
 */

#ifndef MINIOS_ESP8266_MODELS_TINY_MLP_H
#define MINIOS_ESP8266_MODELS_TINY_MLP_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  Model metadata                                                    */
/* ------------------------------------------------------------------ */

#define TINY_MLP_NAME          "tiny_mlp"
#define TINY_MLP_INPUT_SIZE    4
#define TINY_MLP_HIDDEN_SIZE   8
#define TINY_MLP_OUTPUT_SIZE   4

/* Quantization parameters */
#define TINY_MLP_SCALE         0.01f
#define TINY_MLP_ZERO_POINT    0

/* ------------------------------------------------------------------ */
/*  Layer 1: Dense weights [8×4] — 32 int8 values                    */
/* Layer 1 computes: hidden = ReLU(W1 × input + b1)                  */
/* ------------------------------------------------------------------ */

static const int8_t TINY_MLP_W1[TINY_MLP_HIDDEN_SIZE][TINY_MLP_INPUT_SIZE] = {
    {  12,  -8,  15,  -3 },  /* neuron 0 */
    {  -7,  11,  -4,  19 },  /* neuron 1 */
    {  20,   2, -12,   8 },  /* neuron 2 */
    {  -1,  16,   7, -10 },  /* neuron 3 */
    {  14,  -5,  18,   1 },  /* neuron 4 */
    {  -9,  13,  -2,  17 },  /* neuron 5 */
    {   6, -14,  10,  -6 },  /* neuron 6 */
    {  17,   4,  -7,  12 },  /* neuron 7 */
};

/* Layer 1 biases [8] — int8 */
static const int8_t TINY_MLP_B1[TINY_MLP_HIDDEN_SIZE] = {
    2, -1, 3, 0, 1, -2, 4, -3
};

/* ------------------------------------------------------------------ */
/*  Layer 2: Dense weights [4×8] — 32 int8 values                    */
/* Layer 2 computes: output = Softmax(W2 × hidden + b2)              */
/* ------------------------------------------------------------------ */

static const int8_t TINY_MLP_W2[TINY_MLP_OUTPUT_SIZE][TINY_MLP_HIDDEN_SIZE] = {
    {   8, -12,   5,  15,  -3,  10,  -7,  18 },  /* class 0 */
    { -10,   7,  20,  -4,  13,  -8,  11,  -2 },  /* class 1 */
    {  16,  -1,  -9,   6,  -15,  3,  -5,  14 },  /* class 2 */
    {  -6,  18,  -3,  12,   2, -17,  9,  -11 },  /* class 3 */
};

/* Layer 2 biases [4] — int8 */
static const int8_t TINY_MLP_B2[TINY_MLP_OUTPUT_SIZE] = {
    1, -2, 3, -1
};

/* ------------------------------------------------------------------ */
/*  Model descriptor (used by onnx_loader_tiny.c)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char  *name;
    uint8_t      input_size;
    uint8_t      hidden_size;
    uint8_t      output_size;
    float        scale;
    int8_t       zero_point;
    const int8_t (*W1)[TINY_MLP_INPUT_SIZE];
    const int8_t *b1;
    const int8_t (*W2)[TINY_MLP_HIDDEN_SIZE];
    const int8_t *b2;
} TinyMLP_Descriptor;

static const TinyMLP_Descriptor TINY_MLP_MODEL = {
    .name        = TINY_MLP_NAME,
    .input_size  = TINY_MLP_INPUT_SIZE,
    .hidden_size = TINY_MLP_HIDDEN_SIZE,
    .output_size = TINY_MLP_OUTPUT_SIZE,
    .scale       = TINY_MLP_SCALE,
    .zero_point  = TINY_MLP_ZERO_POINT,
    .W1          = TINY_MLP_W1,
    .b1          = TINY_MLP_B1,
    .W2          = TINY_MLP_W2,
    .b2          = TINY_MLP_B2,
};

#endif /* MINIOS_ESP8266_MODELS_TINY_MLP_H */
