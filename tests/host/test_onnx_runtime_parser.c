#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "status.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_runtime.h"

void setUp(void) {}
void tearDown(void) {}

void test_CT_ONNX_PARSE_001_custom_binary_invalid_magic_rejected(void) {
    ONNX_Graph g;
    ONNX_CustomHeader h;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "g1"));

    memset(&h, 0, sizeof(h));
    h.magic = 0xDEADBEEFu;
    h.version = ONNX_CUSTOM_VERSION;
    h.tensor_data_offset = sizeof(ONNX_CustomHeader);

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_GRAPH,
                          ONNX_LoadCustomBinary(&g, (const uint8_t *)&h, sizeof(h)));
}

void test_CT_ONNX_PARSE_002_custom_binary_node_limit_rejected(void) {
    ONNX_Graph g;
    ONNX_CustomHeader h;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "g2"));

    memset(&h, 0, sizeof(h));
    h.magic = ONNX_CUSTOM_MAGIC;
    h.version = ONNX_CUSTOM_VERSION;
    h.num_nodes = ONNX_MAX_NODES + 1u;
    h.tensor_data_offset = sizeof(ONNX_CustomHeader);

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_GRAPH,
                          ONNX_LoadCustomBinary(&g, (const uint8_t *)&h, sizeof(h)));
}

void test_CT_ONNX_PARSE_003_custom_binary_minimal_model_loads(void) {
    ONNX_Graph g;
    ONNX_CustomHeader h;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "g3"));

    memset(&h, 0, sizeof(h));
    h.magic = ONNX_CUSTOM_MAGIC;
    h.version = ONNX_CUSTOM_VERSION;
    h.num_nodes = 0;
    h.num_tensors = 0;
    h.num_inputs = 0;
    h.num_outputs = 0;
    h.tensor_data_offset = sizeof(ONNX_CustomHeader);

    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          ONNX_LoadCustomBinary(&g, (const uint8_t *)&h, sizeof(h)));
    TEST_ASSERT_EQUAL_UINT32(0u, g.num_nodes);
    TEST_ASSERT_EQUAL_UINT32(0u, g.schedule_length);
}

void test_UT_ONNX_PARSE_004_loadembedded_unsupported_format(void) {
    ONNX_Graph g;
    uint8_t buf[8] = {0};

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "g4"));

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_NOT_SUPPORTED,
                          ONNX_LoadEmbedded(&g, buf, sizeof(buf), (ONNX_Format)255));
}

void test_UT_ONNX_RUNTIME_005_init_argument_validation(void) {
    ONNX_InferenceContext ctx;
    ONNX_Graph g;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt0"));

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          ONNX_Runtime_Init(NULL, &g, 0));
    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          ONNX_Runtime_Init(&ctx, NULL, 0));
}

void test_UT_ONNX_RUNTIME_006_control_flag_toggles(void) {
    ONNX_Runtime_SetVerbose(false);
    TEST_ASSERT_FALSE(ONNX_Runtime_GetVerbose());
    ONNX_Runtime_SetVerbose(true);
    TEST_ASSERT_TRUE(ONNX_Runtime_GetVerbose());

    ONNX_Runtime_SetYieldBetweenNodes(false);
    TEST_ASSERT_FALSE(ONNX_Runtime_GetYieldBetweenNodes());
    ONNX_Runtime_SetYieldBetweenNodes(true);
    TEST_ASSERT_TRUE(ONNX_Runtime_GetYieldBetweenNodes());

    ONNX_Runtime_SetNodeProfiling(false);
    TEST_ASSERT_FALSE(ONNX_Runtime_GetNodeProfiling());
    ONNX_Runtime_SetNodeProfiling(true);
    TEST_ASSERT_TRUE(ONNX_Runtime_GetNodeProfiling());

    ONNX_Runtime_SetPrepareNodeOutputs(false);
    TEST_ASSERT_FALSE(ONNX_Runtime_GetPrepareNodeOutputs());
    ONNX_Runtime_SetPrepareNodeOutputs(true);
    TEST_ASSERT_TRUE(ONNX_Runtime_GetPrepareNodeOutputs());
}

void test_CT_ONNX_RUNTIME_007_execute_unsupported_operator_rejected(void) {
    ONNX_Graph g;
    ONNX_InferenceContext ctx;
    ONNX_Node *n;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt1"));
    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Runtime_Init(&ctx, &g, 0));

    n = ONNX_Graph_AddNode(&g, "unknown", (ONNX_OperatorType)255);
    TEST_ASSERT_NOT_NULL(n);

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_UNSUPPORTED_OPERATOR,
                          ONNX_Runtime_ExecuteNode(&ctx, n));
}

void test_IT_ONNX_RUNTIME_008_inference_input_count_mismatch_rejected(void) {
    ONNX_Graph g;
    ONNX_InferenceContext ctx;
    ONNX_TensorShape shape;
    ONNX_Tensor *input;
    ONNX_Tensor *inputs[1];

    memset(&shape, 0, sizeof(shape));
    shape.ndim = 1;
    shape.dims[0] = 1;
    shape.total_elements = 1;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt2"));
    input = ONNX_Graph_CreateTensor(&g, "input0", ONNX_DTYPE_FLOAT32, &shape);
    TEST_ASSERT_NOT_NULL(input);
    g.inputs[0] = input;
    g.num_inputs = 1;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Runtime_Init(&ctx, &g, 0));

    inputs[0] = input;
    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          ONNX_Runtime_Inference(&ctx, inputs, 0, NULL, 0));
}

void test_CT_ONNX_PARSE_009_custom_binary_unsupported_version_rejected(void) {
    ONNX_Graph g;
    ONNX_CustomHeader h;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "g9"));

    memset(&h, 0, sizeof(h));
    h.magic = ONNX_CUSTOM_MAGIC;
    h.version = ONNX_CUSTOM_VERSION + 1u;
    h.tensor_data_offset = sizeof(ONNX_CustomHeader);

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_NOT_SUPPORTED,
                          ONNX_LoadCustomBinary(&g, (const uint8_t *)&h, sizeof(h)));
}

void test_CT_ONNX_PARSE_010_custom_binary_corrupt_metadata_bounds_rejected(void) {
    ONNX_Graph g;
    ONNX_CustomHeader h;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "g10"));

    memset(&h, 0, sizeof(h));
    h.magic = ONNX_CUSTOM_MAGIC;
    h.version = ONNX_CUSTOM_VERSION;
    h.num_tensors = 1;
    h.tensor_data_offset = sizeof(ONNX_CustomHeader);

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_GRAPH,
                          ONNX_LoadCustomBinary(&g, (const uint8_t *)&h, sizeof(h)));
}

void test_UT_ONNX_RUNTIME_011_inference_output_count_mismatch_rejected(void) {
    ONNX_Graph g;
    ONNX_InferenceContext ctx;
    ONNX_TensorShape shape;
    ONNX_Tensor *out0;
    ONNX_Tensor *outputs[1];

    memset(&shape, 0, sizeof(shape));
    shape.ndim = 1;
    shape.dims[0] = 1;
    shape.total_elements = 1;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt3"));
    out0 = ONNX_Graph_CreateTensor(&g, "out0", ONNX_DTYPE_FLOAT32, &shape);
    TEST_ASSERT_NOT_NULL(out0);
    g.outputs[0] = out0;
    g.num_outputs = 1;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Runtime_Init(&ctx, &g, 0));

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          ONNX_Runtime_Inference(&ctx, NULL, 0, outputs, 0));
}

void test_UT_ONNX_RUNTIME_012_execute_upto_missing_node_rejected(void) {
    ONNX_Graph g;
    ONNX_InferenceContext ctx;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt4"));
    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Runtime_Init(&ctx, &g, 0));

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          ONNX_Runtime_ExecuteUpTo(&ctx, "missing_node"));
}

void test_UT_ONNX_RUNTIME_013_execute_upto_prefix_name_not_accepted(void) {
    ONNX_Graph g;
    ONNX_InferenceContext ctx;
    ONNX_Node *n;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt5"));
    n = ONNX_Graph_AddNode(&g, "node_exact", ONNX_OP_ADD);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Runtime_Init(&ctx, &g, 0));

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          ONNX_Runtime_ExecuteUpTo(&ctx, "node"));
}

void test_UT_ONNX_RUNTIME_014_execute_upto_fails_when_target_not_in_schedule(void) {
    ONNX_Graph g;
    ONNX_InferenceContext ctx;
    ONNX_Node *n;

    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Graph_Init(&g, "rt6"));
    n = ONNX_Graph_AddNode(&g, "scheduled_gap", ONNX_OP_ADD);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(STATUS_OK, ONNX_Runtime_Init(&ctx, &g, 0));

    TEST_ASSERT_NOT_EQUAL(STATUS_OK,
                          ONNX_Runtime_ExecuteUpTo(&ctx, "scheduled_gap"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_CT_ONNX_PARSE_001_custom_binary_invalid_magic_rejected);
    RUN_TEST(test_CT_ONNX_PARSE_002_custom_binary_node_limit_rejected);
    RUN_TEST(test_CT_ONNX_PARSE_003_custom_binary_minimal_model_loads);
    RUN_TEST(test_UT_ONNX_PARSE_004_loadembedded_unsupported_format);
    RUN_TEST(test_UT_ONNX_RUNTIME_005_init_argument_validation);
    RUN_TEST(test_UT_ONNX_RUNTIME_006_control_flag_toggles);
    RUN_TEST(test_CT_ONNX_RUNTIME_007_execute_unsupported_operator_rejected);
    RUN_TEST(test_IT_ONNX_RUNTIME_008_inference_input_count_mismatch_rejected);
    RUN_TEST(test_CT_ONNX_PARSE_009_custom_binary_unsupported_version_rejected);
    RUN_TEST(test_CT_ONNX_PARSE_010_custom_binary_corrupt_metadata_bounds_rejected);
    RUN_TEST(test_UT_ONNX_RUNTIME_011_inference_output_count_mismatch_rejected);
    RUN_TEST(test_UT_ONNX_RUNTIME_012_execute_upto_missing_node_rejected);
    RUN_TEST(test_UT_ONNX_RUNTIME_013_execute_upto_prefix_name_not_accepted);
    RUN_TEST(test_UT_ONNX_RUNTIME_014_execute_upto_fails_when_target_not_in_schedule);
    return UNITY_END();
}
