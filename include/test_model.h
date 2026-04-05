/**
 * @file test_model.h
 * @brief Embedded hand-crafted ONNX model for parser verification
 *
 * Model: Y = X + B
 *   Input  X : [3] float32  (dynamic — supplied at runtime)
 *   Weight B : [3] float32  = {1.0, 2.0, 3.0}  (initializer)
 *   Output Y : [3] float32
 *
 * When X = {2.0, 3.0, 4.0}  →  Y = {3.0, 5.0, 7.0}
 *
 * Protobuf byte layout:
 *
 *  ModelProto (66 bytes total)
 *   ├── Field 1 (ir_version):  varint 7          [08 07]            2 B
 *   └── Field 7 (graph):  LD, 62 B               [3a 3e]
 *        GraphProto (62 bytes)
 *         ├── Field 2 (name="Test"): LD 4         [12 04 ...]        6 B
 *         ├── Field 1 (node): LD 19               [0a 13]           21 B total
 *         │    NodeProto (19 B)
 *         │     ├── input  "X"  [0a 01 58]    3 B
 *         │     ├── input  "B"  [0a 01 42]    3 B
 *         │     ├── output "Y"  [12 01 59]    3 B
 *         │     ├── op_type "Add" [22 03 …]   5 B
 *         │     └── name "add"   [1a 03 …]    5 B
 *         ├── Field 5 (initializer): LD 23    [2a 17]              25 B total
 *         │    TensorProto for B (23 B)
 *         │     ├── dims=1    [08 01]          2 B
 *         │     ├── dims=3    [08 03]          2 B
 *         │     ├── data_type [10 01]          2 B
 *         │     ├── name="B" [42 01 42]       3 B  ← field 8 (Bug 1 fix)
 *         │     └── raw_data  [4a 0c …]      14 B  (field 9, 12 bytes)
 *         ├── Field 11 (input ValueInfoProto for X): [5a 03 0a 01 58]  5 B
 *         └── Field 12 (output ValueInfoProto for Y): [62 03 0a 01 59] 5 B
 */

#ifndef TEST_MODEL_H
#define TEST_MODEL_H

#include "types.h"

static const uint8_t test_onnx_model[] = {
    /* ---- ModelProto ---- */

    /* Field 1: ir_version = 7 */
    0x08, 0x07,

    /* Field 7: graph, wire=2, length=62 (0x3e) */
    0x3a, 0x3e,

        /* ---- GraphProto (62 bytes) ---- */

        /* Field 2: name = "Test" (4 bytes) */
        0x12, 0x04,  'T', 'e', 's', 't',

        /* Field 1: node (NodeProto), wire=2, length=19 (0x13) */
        0x0a, 0x13,

            /* ---- NodeProto (19 bytes) ---- */
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

        /* Field 5: initializer (TensorProto), wire=2, length=23 (0x17) */
        0x2a, 0x17,

            /* ---- TensorProto for B (23 bytes) ---- */
            /* Field 1: dims = 1 */
            0x08, 0x01,
            /* Field 1: dims = 3 */
            0x08, 0x03,
            /* Field 2: data_type = FLOAT (1) */
            0x10, 0x01,
            /* Field 8: name = "B"   (BUG 1 FIX: 0x42 = field 8, wire 2) */
            0x42, 0x01, 'B',
            /* Field 9: raw_data = 12 bytes [1.0f, 2.0f, 3.0f] LE float32 */
            0x4a, 0x0c,
            0x00, 0x00, 0x80, 0x3f,  /* 1.0f */
            0x00, 0x00, 0x00, 0x40,  /* 2.0f */
            0x00, 0x00, 0x40, 0x40,  /* 3.0f */

        /* Field 11: input ValueInfoProto for X (name only, minimal) */
        /* tag: (11<<3)|2 = 0x5a, len=3 */
        0x5a, 0x03,  0x0a, 0x01, 'X',

        /* Field 12: output ValueInfoProto for Y (name only, minimal) */
        /* tag: (12<<3)|2 = 0x62, len=3 */
        0x62, 0x03,  0x0a, 0x01, 'Y',
};

static const uint32_t test_onnx_model_len = sizeof(test_onnx_model);

#endif /* TEST_MODEL_H */
