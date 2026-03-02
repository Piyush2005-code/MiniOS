/**
 * @file test_model.h
 * @brief Embedded ONNX model for testing
 * 
 * This is a manually created minimal ONNX model containing:
 * - Single Add operation: Y = X + B
 * - Input X: [1, 3] float32
 * - Bias B: [1, 3] float32 = [1.0, 2.0, 3.0]
 * - Output Y: [1, 3] float32
 * 
 * Protobuf structure (simplified):
 * ModelProto {
 *   ir_version: 7
 *   graph: GraphProto {
 *     node: [NodeProto { op_type: "Add", input: ["X", "B"], output: ["Y"] }]
 *     name: "TestModel"
 *     initializer: [TensorProto for B]
 *     input: [ValueInfoProto for X]
 *     output: [ValueInfoProto for Y]
 *   }
 * }
 */

#ifndef TEST_MODEL_H
#define TEST_MODEL_H

#include "types.h"

/* Minimal ONNX protobuf for testing
 * 
 * This is a hand-crafted protobuf with:
 * - Field 1 (ir_version): varint = 7
 * - Field 7 (graph): length-delimited GraphProto
 *   - Field 2 (name): string = "Test"
 *   - Field 1 (node): NodeProto
 *     - Field 4 (op_type): string = "Add"
 *     - Field 1 (input): string = "X"
 *     - Field 1 (input): string = "B"
 *     - Field 2 (output): string = "Y"
 *   - Field 5 (initializer): TensorProto for B
 *     - Field 1 (dims): [1, 3]
 *     - Field 2 (data_type): FLOAT (1)
 *     - Field 3 (name): "B"
 *     - Field 9 (raw_data): [1.0, 2.0, 3.0] as float32
 */

static const uint8_t test_onnx_model[] = {
    /* Field 1: ir_version = 7 */
    0x08, 0x07,
    
    /* Field 7: graph (length-delimited) */
    0x3a,  /* Field 7, wire type 2 (length-delimited) */
    0x50,  /* Length = 80 bytes (approximate) */
    
        /* GraphProto contents: */
        
        /* Field 2: name = "Test" */
        0x12, 0x04,  /* Field 2, wire type 2, length 4 */
        'T', 'e', 's', 't',
        
        /* Field 1: node (NodeProto) */
        0x0a,  /* Field 1, wire type 2 (length-delimited) */
        0x1a,  /* Length = 26 bytes */
        
            /* NodeProto contents: */
            
            /* Field 1: input = "X" */
            0x0a, 0x01, 'X',
            
            /* Field 1: input = "B" */
            0x0a, 0x01, 'B',
            
            /* Field 2: output = "Y" */
            0x12, 0x01, 'Y',
            
            /* Field 4: op_type = "Add" */
            0x22, 0x03, 'A', 'd', 'd',
            
            /* Field 3: name = "add" */
            0x1a, 0x03, 'a', 'd', 'd',
        
        /* Field 5: initializer (TensorProto for B) */
        0x2a,  /* Field 5, wire type 2 (length-delimited) */
        0x20,  /* Length = 32 bytes */
        
            /* TensorProto contents: */
            
            /* Field 1: dims = [1] */
            0x08, 0x01,
            
            /* Field 1: dims = [3] */
            0x08, 0x03,
            
            /* Field 2: data_type = FLOAT (1) */
            0x10, 0x01,
            
            /* Field 3: name = "B" */
            0x1a, 0x01, 'B',
            
            /* Field 9: raw_data = 12 bytes of float32 */
            0x4a, 0x0c,
            0x00, 0x00, 0x80, 0x3f,  /* 1.0f */
            0x00, 0x00, 0x00, 0x40,  /* 2.0f */
            0x00, 0x00, 0x40, 0x40,  /* 3.0f */
};

static const uint32_t test_onnx_model_len = sizeof(test_onnx_model);

#endif /* TEST_MODEL_H */
