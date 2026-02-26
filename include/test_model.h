#ifndef TEST_MODEL_H
#define TEST_MODEL_H

#include "types.h"

/* Minimal ONNX protobuf: Y = X + B */
static const uint8_t test_onnx_model[] = {
    /* Field 1: ir_version = 7 */
    0x08, 0x07,
    
    /* Field 7: graph */
    0x3a, 0x30,  /* field 7, length = 48 bytes */
    
    /* === GraphProto (48 bytes) === */
    
    /* Field 2: name = "Test" */
    0x12, 0x04, 'T', 'e', 's', 't',
    
    /* Field 1: node */
    0x0a, 0x13,  /* field 1, length = 19 bytes */
        0x0a, 0x01, 'X',  /* input */
        0x0a, 0x01, 'B',  /* input */
        0x12, 0x01, 'Y',  /* output */
        0x22, 0x03, 'A', 'd', 'd',  /* op_type */
        0x1a, 0x03, 'a', 'd', 'd',  /* name */
    
    /* Field 5: initializer */
    0x2a, 0x17,  /* field 5, length = 23 bytes */
        0x08, 0x01,  /* dims = 1 */
        0x08, 0x03,  /* dims = 3 */
        0x10, 0x01,  /* dtype = FLOAT */
        0x1a, 0x01, 'B',  /* name */
        0x4a, 0x0c,  /* raw_data (12 bytes) */
        0x00, 0x00, 0x80, 0x3f,  /* 1.0f */
        0x00, 0x00, 0x00, 0x40,  /* 2.0f */
        0x00, 0x00, 0x40, 0x40,  /* 3.0f */
};

static const uint32_t test_onnx_model_len = sizeof(test_onnx_model);

#endif
