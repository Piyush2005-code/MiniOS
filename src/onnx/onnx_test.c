#include "onnx/onnx_test.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "kernel/kmem.h"
#include "status.h"
#include "types.h"

#include "test_models/simple_add_model.h"
#include "test_models/simple_mul_model.h"
#include "test_models/simple_relu_model.h"
#include "test_models/two_op_model_model.h"
#include "test_models/matmul_model_model.h"

static bool float_close(float a, float b, float tol) {
    float diff = a - b;
    if (diff < 0) diff = -diff;
    return diff <= tol;
}

static void print_pass(const char* id) {
    HAL_UART_PutString("[PASS] ");
    HAL_UART_PutString(id);
    HAL_UART_PutString("\n");
}

static void print_fail(const char* id) {
    HAL_UART_PutString("[FAIL] ");
    HAL_UART_PutString(id);
    HAL_UART_PutString("\n");
}

static int passed_tests = 0;
static int total_tests = 0;

static void check_test(bool condition, const char* id) {
    total_tests++;
    if (condition) {
        passed_tests++;
        print_pass(id);
    } else {
        print_fail(id);
    }
}

/* Common test setup */
static ONNX_Graph test_graph;
static ONNX_InferenceContext test_ctx;
static kmem_arena_t* test_arena;
static uint8_t test_arena_buf[8192];

static void setup_test_graph() {
    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "test_graph");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);
}

/* Layer 1 Unit Tests */

static void ut_add_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_y = {0};
    shape_y.ndim = 1; shape_y.dims[0] = 3; shape_y.total_elements = 3;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &shape_y);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    ((float*)x->data)[0] = 2.0f; ((float*)x->data)[1] = 3.0f; ((float*)x->data)[2] = 4.0f;
    ((float*)y->data)[0] = 1.0f; ((float*)y->data)[1] = 2.0f; ((float*)y->data)[2] = 3.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Add", ONNX_OP_ADD);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddInput(node, y);
    ONNX_Node_AddOutput(node, z);

    ONNX_Execute_Arithmetic(node, &test_ctx);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 3.0f) && (z_data[1] == 5.0f) && (z_data[2] == 7.0f);
    check_test(pass, "ut_add_001");
}

static void ut_sub_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_y = {0};
    shape_y.ndim = 1; shape_y.dims[0] = 3; shape_y.total_elements = 3;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &shape_y);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    ((float*)x->data)[0] = 5.0f; ((float*)x->data)[1] = 3.0f; ((float*)x->data)[2] = 8.0f;
    ((float*)y->data)[0] = 1.0f; ((float*)y->data)[1] = 2.0f; ((float*)y->data)[2] = 3.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Sub", ONNX_OP_SUB);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddInput(node, y);
    ONNX_Node_AddOutput(node, z);

    ONNX_Execute_Arithmetic(node, &test_ctx);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 4.0f) && (z_data[1] == 1.0f) && (z_data[2] == 5.0f);
    check_test(pass, "ut_sub_001");
}

static void ut_mul_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_y = {0};
    shape_y.ndim = 1; shape_y.dims[0] = 3; shape_y.total_elements = 3;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &shape_y);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    ((float*)x->data)[0] = 2.0f; ((float*)x->data)[1] = 3.0f; ((float*)x->data)[2] = 4.0f;
    ((float*)y->data)[0] = 0.5f; ((float*)y->data)[1] = 2.0f; ((float*)y->data)[2] = -1.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Mul", ONNX_OP_MUL);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddInput(node, y);
    ONNX_Node_AddOutput(node, z);

    ONNX_Execute_Arithmetic(node, &test_ctx);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 1.0f) && (z_data[1] == 6.0f) && (z_data[2] == -4.0f);
    check_test(pass, "ut_mul_001");
}

static void ut_div_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_y = {0};
    shape_y.ndim = 1; shape_y.dims[0] = 3; shape_y.total_elements = 3;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &shape_y);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    ((float*)x->data)[0] = 6.0f; ((float*)x->data)[1] = 9.0f; ((float*)x->data)[2] = 4.0f;
    ((float*)y->data)[0] = 2.0f; ((float*)y->data)[1] = 3.0f; ((float*)y->data)[2] = 2.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Div", ONNX_OP_DIV);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddInput(node, y);
    ONNX_Node_AddOutput(node, z);

    ONNX_Execute_Arithmetic(node, &test_ctx);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 3.0f) && (z_data[1] == 3.0f) && (z_data[2] == 2.0f);
    check_test(pass, "ut_div_001");
}

static void ut_matmul_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 2; shape_x.dims[0] = 2; shape_x.dims[1] = 3; shape_x.total_elements = 6;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_y = {0};
    shape_y.ndim = 2; shape_y.dims[0] = 3; shape_y.dims[1] = 2; shape_y.total_elements = 6;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &shape_y);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 2; shape_z.dims[0] = 2; shape_z.dims[1] = 2; shape_z.total_elements = 4;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = 1; x_data[1] = 2; x_data[2] = 3;
    x_data[3] = 4; x_data[4] = 5; x_data[5] = 6;

    float* y_data = (float*)y->data;
    y_data[0] = 1; y_data[1] = 0;
    y_data[2] = 0; y_data[3] = 1;
    y_data[4] = 1; y_data[5] = 0;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "MatMul", ONNX_OP_MATMUL);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddInput(node, y);
    ONNX_Node_AddOutput(node, z);

    /* Need to call ONNX_Runtime_ExecuteNode since matmul is external */
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 4.0f) && (z_data[1] == 2.0f) &&
                (z_data[2] == 10.0f) && (z_data[3] == 5.0f);
    check_test(pass, "ut_matmul_001");
}

/* Need declarations for other extern operations, but ONNX_Runtime_ExecuteNode handles dispatch */

static void ut_relu_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 5; shape_x.total_elements = 5;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 5; shape_z.total_elements = 5;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = -2.0f; x_data[1] = -0.5f; x_data[2] = 0.0f; x_data[3] = 0.5f; x_data[4] = 2.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Relu", ONNX_OP_RELU);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 0.0f) && (z_data[1] == 0.0f) && (z_data[2] == 0.0f) &&
                (z_data[3] == 0.5f) && (z_data[4] == 2.0f);
    check_test(pass, "ut_relu_001");
}

static void ut_sigmoid_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 1; shape_x.total_elements = 1;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 1; shape_z.total_elements = 1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    ((float*)x->data)[0] = 0.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Sigmoid", ONNX_OP_SIGMOID);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    bool pass = float_close(((float*)z->data)[0], 0.5f, 0.001f);
    check_test(pass, "ut_sigmoid_001");
}

static void ut_tanh_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 1; shape_x.total_elements = 1;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 1; shape_z.total_elements = 1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    ((float*)x->data)[0] = 0.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Tanh", ONNX_OP_TANH);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    bool pass = float_close(((float*)z->data)[0], 0.0f, 0.001f);
    check_test(pass, "ut_tanh_001");
}

static void ut_softmax_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = 1.0f; x_data[1] = 2.0f; x_data[2] = 3.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Softmax", ONNX_OP_SOFTMAX);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);
    node->attributes.axis = 0; // Set axis for 1D tensor

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = float_close(z_data[0], 0.090f, 0.01f) &&
                float_close(z_data[1], 0.245f, 0.01f) &&
                float_close(z_data[2], 0.665f, 0.01f);

    float sum = z_data[0] + z_data[1] + z_data[2];
    pass = pass && float_close(sum, 1.0f, 0.01f);

    check_test(pass, "ut_softmax_001");
}

static void ut_leakyrelu_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = -2.0f; x_data[1] = 0.0f; x_data[2] = 2.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "LeakyRelu", ONNX_OP_LEAKYRELU);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);
    node->attributes.alpha = 0.01f;

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = float_close(z_data[0], -0.02f, 0.001f) &&
                float_close(z_data[1], 0.0f, 0.001f) &&
                float_close(z_data[2], 2.0f, 0.001f);

    check_test(pass, "ut_leakyrelu_001");
}

static void ut_abs_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = -3.0f; x_data[1] = 0.0f; x_data[2] = 4.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Abs", ONNX_OP_ABS);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 3.0f) && (z_data[1] == 0.0f) && (z_data[2] == 4.0f);
    check_test(pass, "ut_abs_001");
}

static void ut_neg_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = 1.0f; x_data[1] = -2.0f; x_data[2] = 0.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Neg", ONNX_OP_NEG);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == -1.0f) && (z_data[1] == 2.0f) && (z_data[2] == 0.0f);
    check_test(pass, "ut_neg_001");
}

static void ut_clip_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 4; shape_x.total_elements = 4;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_min_t = {0};
    shape_min_t.ndim = 1; shape_min_t.dims[0] = 1; shape_min_t.total_elements = 1;
    ONNX_Tensor* min_t = ONNX_Graph_CreateTensor(&test_graph, "min", ONNX_DTYPE_FLOAT32, &shape_min_t);
    ONNX_Graph_AllocateTensor(&test_graph, min_t);

    ONNX_TensorShape shape_max_t = {0};
    shape_max_t.ndim = 1; shape_max_t.dims[0] = 1; shape_max_t.total_elements = 1;
    ONNX_Tensor* max_t = ONNX_Graph_CreateTensor(&test_graph, "max", ONNX_DTYPE_FLOAT32, &shape_max_t);
    ONNX_Graph_AllocateTensor(&test_graph, max_t);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 4; shape_z.total_elements = 4;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = -5.0f; x_data[1] = 0.0f; x_data[2] = 3.0f; x_data[3] = 10.0f;

    ((float*)min_t->data)[0] = 0.0f;
    ((float*)max_t->data)[0] = 5.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Clip", ONNX_OP_CLIP);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddInput(node, min_t);
    ONNX_Node_AddInput(node, max_t);
    
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 0.0f) && (z_data[1] == 0.0f) &&
                (z_data[2] == 3.0f) && (z_data[3] == 5.0f);
    check_test(pass, "ut_clip_001");
}

static void ut_identity_001() {
    setup_test_graph();

    ONNX_TensorShape shape_x = {0};
    shape_x.ndim = 1; shape_x.dims[0] = 3; shape_x.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &shape_x);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape shape_z = {0};
    shape_z.ndim = 1; shape_z.dims[0] = 3; shape_z.total_elements = 3;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &shape_z);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* x_data = (float*)x->data;
    x_data[0] = 1.0f; x_data[1] = 2.0f; x_data[2] = 3.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Identity", ONNX_OP_IDENTITY);
    ONNX_Node_AddInput(node, x);
    ONNX_Node_AddOutput(node, z);

    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* z_data = (float*)z->data;
    bool pass = (z_data[0] == 1.0f) && (z_data[1] == 2.0f) && (z_data[2] == 3.0f);
    check_test(pass, "ut_identity_001");
}

/* Layer 2 Integration Tests */

static void it_infer_001() {
    if (SIMPLE_RELU_ONNX_LEN == 0) {
        check_test(false, "it_infer_001 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "it_infer_001");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);

    Status s = ONNX_LoadEmbedded(&test_graph, SIMPLE_RELU_ONNX, SIMPLE_RELU_ONNX_LEN, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        HAL_UART_PutString("it_infer_001 Load failed with status ");
        HAL_UART_PutDec(s);
        HAL_UART_PutString("\n");
        check_test(false, "it_infer_001 (Load failed)");
        return;
    }

    ONNX_Tensor* in = test_graph.num_inputs > 0 ? test_graph.inputs[0] : NULL;
    ONNX_Tensor* out = test_graph.num_outputs > 0 ? test_graph.outputs[0] : NULL;

    if (!in || !out) {
        check_test(false, "it_infer_001 (No input/output)");
        return;
    }

    /* Allocate input if not already allocated (not an initializer) */
    if (!in->data) {
        ONNX_Graph_AllocateTensor(&test_graph, in);
    }
    /* Allocate output */
    if (out->shape.total_elements == 0) {
        out->shape.ndim = in->shape.ndim;
        for (uint32_t i = 0; i < in->shape.ndim && i < ONNX_MAX_DIMS; i++) {
            out->shape.dims[i] = in->shape.dims[i];
        }
        out->shape.total_elements = in->shape.total_elements;
        out->data_size = out->shape.total_elements * sizeof(float);
    }
    if (!out->data) {
        ONNX_Graph_AllocateTensor(&test_graph, out);
    }

    /* Set input data */
    if (in->data && in->shape.total_elements >= 3) {
        float* in_data = (float*)in->data;
        in_data[0] = 2.0f; in_data[1] = 3.0f; in_data[2] = 4.0f;
    }

    ONNX_Tensor* inputs[] = { in };
    ONNX_Tensor* outputs[] = { out };

    s = ONNX_Runtime_Inference(&test_ctx, inputs, 1, outputs, 1);

    if (s == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 2.0f, 0.01f) &&
                    float_close(out_data[1], 3.0f, 0.01f) &&
                    float_close(out_data[2], 4.0f, 0.01f);
        check_test(pass, "it_infer_001");
    } else {
        if (s != STATUS_OK) {
            HAL_UART_PutString("it_infer_001 failed with status ");
            HAL_UART_PutDec(s);
            HAL_UART_PutString("\n");
        }
        check_test(false, "it_infer_001");
    }
}

static void it_infer_002() {
    if (SIMPLE_ADD_ONNX_LEN == 0) {
        check_test(false, "it_infer_002 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "it_infer_002");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);

    Status s = ONNX_LoadEmbedded(&test_graph, SIMPLE_ADD_ONNX, SIMPLE_ADD_ONNX_LEN, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        HAL_UART_PutString("it_infer_002 Load failed with status ");
        HAL_UART_PutDec(s);
        HAL_UART_PutString("\n");
        check_test(false, "it_infer_002 (Load failed)");
        return;
    }

    ONNX_Tensor* in_x = test_graph.num_inputs > 0 ? test_graph.inputs[0] : NULL;
    ONNX_Tensor* in_y = test_graph.num_inputs > 1 ? test_graph.inputs[1] : NULL;
    ONNX_Tensor* out = test_graph.num_outputs > 0 ? test_graph.outputs[0] : NULL;

    ONNX_Tensor* inputs[2] = { in_x, in_y };
    ONNX_Tensor* outputs[1] = { out };

    // Make sure we allocate data for input and output if needed
    if (in_x) ONNX_Graph_AllocateTensor(&test_graph, in_x);
    if (in_y) ONNX_Graph_AllocateTensor(&test_graph, in_y);
    // don't allocate out before inference, maybe the runtime does it? Or maybe out->shape is not fully set?
    if (out) {
        if (out->shape.total_elements == 0) {
             out->shape.ndim = 1;
             out->shape.dims[0] = 3;
             out->shape.total_elements = 3;
             out->data_size = 3 * sizeof(float);
        }
        ONNX_Graph_AllocateTensor(&test_graph, out);
    }

    if (in_x && in_x->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in_x->data;
        in_data[0] = 2.0f; in_data[1] = 3.0f; in_data[2] = 4.0f;
    }
    if (in_y && in_y->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in_y->data;
        in_data[0] = 1.0f; in_data[1] = 1.0f; in_data[2] = 1.0f;
    }

    Status s_infer = ONNX_Runtime_Inference(&test_ctx, inputs, 2, outputs, 1);

    if (s_infer == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 3.0f, 0.01f) &&
                    float_close(out_data[1], 4.0f, 0.01f) &&
                    float_close(out_data[2], 5.0f, 0.01f);
        if (!pass) {
            HAL_UART_PutString("it_infer_002 results: ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[0]);
            HAL_UART_PutString(" ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[1]);
            HAL_UART_PutString(" ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[2]);
            HAL_UART_PutString("\n");
        }
        check_test(pass, "it_infer_002");
    } else {
        if (!out) { HAL_UART_PutString("it_infer_002 failed: out is NULL\n"); }
        else if (!out->data) { HAL_UART_PutString("it_infer_002 failed: out->data is NULL\n"); }
        check_test(false, "it_infer_002");
    }
}

static void it_infer_003() {
    if (TWO_OP_MODEL_ONNX_LEN == 0) {
        check_test(false, "it_infer_003 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "it_infer_003");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);

    Status s = ONNX_LoadEmbedded(&test_graph, TWO_OP_MODEL_ONNX, TWO_OP_MODEL_ONNX_LEN, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "it_infer_003 (Load failed)");
        return;
    }

    ONNX_Tensor* in_x = test_graph.num_inputs > 0 ? test_graph.inputs[0] : NULL;
    ONNX_Tensor* in_y = test_graph.num_inputs > 1 ? test_graph.inputs[1] : NULL;
    ONNX_Tensor* out = test_graph.num_outputs > 0 ? test_graph.outputs[0] : NULL;

    ONNX_Tensor* inputs[2] = { in_x, in_y };
    ONNX_Tensor* outputs[1] = { out };

    // Make sure we allocate data for input and output if needed
    if (in_x) ONNX_Graph_AllocateTensor(&test_graph, in_x);
    if (in_y) ONNX_Graph_AllocateTensor(&test_graph, in_y);
    if (out) {
        if (out->shape.total_elements == 0) {
             out->shape.ndim = 1;
             out->shape.dims[0] = 3;
             out->shape.total_elements = 3;
             out->data_size = 3 * sizeof(float);
        }
        ONNX_Graph_AllocateTensor(&test_graph, out);
    }

    if (in_x && in_x->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in_x->data;
        in_data[0] = 2.0f; in_data[1] = 3.0f; in_data[2] = 4.0f;
    }
    if (in_y && in_y->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in_y->data;
        in_data[0] = 3.0f; in_data[1] = 2.0f; in_data[2] = 1.0f;
    }

    Status s_infer = ONNX_Runtime_Inference(&test_ctx, inputs, 2, outputs, 1);

    if (s_infer == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 10.0f, 0.05f) &&
                    float_close(out_data[1], 10.0f, 0.05f) &&
                    float_close(out_data[2], 10.0f, 0.05f);
        if (!pass) {
            HAL_UART_PutString("it_infer_003 results: ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[0]);
            HAL_UART_PutString(" ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[1]);
            HAL_UART_PutString(" ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[2]);
            HAL_UART_PutString("\n");
        }
        check_test(pass, "it_infer_003");
    } else {
        if (!out) { HAL_UART_PutString("it_infer_003 failed: out is NULL\n"); }
        else if (!out->data) { HAL_UART_PutString("it_infer_003 failed: out->data is NULL\n"); }
        check_test(false, "it_infer_003");
    }
}

static void it_infer_004() {
    // Run it_infer_003 context again
    if (TWO_OP_MODEL_ONNX_LEN == 0) {
        check_test(false, "it_infer_004 (Model missing)");
        return;
    }

    ONNX_Tensor* in_x = test_graph.num_inputs > 0 ? test_graph.inputs[0] : NULL;
    ONNX_Tensor* in_y = test_graph.num_inputs > 1 ? test_graph.inputs[1] : NULL;
    ONNX_Tensor* out = test_graph.num_outputs > 0 ? test_graph.outputs[0] : NULL;

    ONNX_Tensor* inputs[2] = { in_x, in_y };
    ONNX_Tensor* outputs[1] = { out };

    // Make sure we allocate data for input and output if needed
    if (in_x) ONNX_Graph_AllocateTensor(&test_graph, in_x);
    if (in_y) ONNX_Graph_AllocateTensor(&test_graph, in_y);
    // don't allocate out before inference, maybe the runtime does it? Or maybe out->shape is not fully set?
    if (out) {
        if (out->shape.total_elements == 0) {
             out->shape.ndim = 1;
             out->shape.dims[0] = 3;
             out->shape.total_elements = 3;
             out->data_size = 3 * sizeof(float);
        }
        ONNX_Graph_AllocateTensor(&test_graph, out);
    }

    if (in_x && in_x->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in_x->data;
        in_data[0] = 2.0f; in_data[1] = 3.0f; in_data[2] = 4.0f;
    }
    if (in_y && in_y->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in_y->data;
        in_data[0] = 3.0f; in_data[1] = 2.0f; in_data[2] = 1.0f;
    }

    Status s_infer = ONNX_Runtime_Inference(&test_ctx, inputs, 2, outputs, 1);

    if (s_infer == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 10.0f, 0.05f) &&
                    float_close(out_data[1], 10.0f, 0.05f) &&
                    float_close(out_data[2], 10.0f, 0.05f);
        if (!pass) {
            HAL_UART_PutString("it_infer_004 results: ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[0]);
            HAL_UART_PutString(" ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[1]);
            HAL_UART_PutString(" ");
            HAL_UART_PutHex(*(uint32_t*)&out_data[2]);
            HAL_UART_PutString("\n");
        }
        check_test(pass, "it_infer_004");
    } else {
        if (!out) { HAL_UART_PutString("it_infer_004 failed: out is NULL\n"); }
        else if (!out->data) { HAL_UART_PutString("it_infer_004 failed: out->data is NULL\n"); }
        check_test(false, "it_infer_004");
    }
}

static void it_timer_001() {
    bool found_time = false;
    for (uint32_t i = 0; i < test_graph.schedule_length; i++) {
        if (test_graph.exec_schedule[i]->exec_time_us > 0) {
            found_time = true;
            break;
        }
    }
    check_test(found_time, "it_timer_001");
}

/* Layer 2 Component Tests */

static void ct_parse_001() {
    if (SIMPLE_MUL_ONNX_LEN == 0) {
        check_test(false, "ct_parse_001 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "ct_parse_001");
    test_graph.tensor_arena = test_arena;

    Status s = ONNX_LoadEmbedded(&test_graph, SIMPLE_MUL_ONNX, SIMPLE_MUL_ONNX_LEN, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "ct_parse_001 (Load failed)");
        return;
    }

    bool nodes_ok = (test_graph.num_nodes == 1);
    bool tensors_ok = (test_graph.num_tensors >= 3);

    bool b_ok = false;
    for (uint32_t i = 0; i < test_graph.num_tensors; i++) {
        ONNX_Tensor* t = &test_graph.tensors[i];
        if (t->name[0] == 'X' && t->name[1] == '\0') {
            b_ok = true;
            break;
        }
    }

    check_test(nodes_ok && tensors_ok && b_ok, "ct_parse_001");
}

static void ct_parse_002() {
    if (SIMPLE_ADD_ONNX_LEN == 0) {
        check_test(false, "ct_parse_002 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "ct_parse_002");
    test_graph.tensor_arena = test_arena;

    Status s = ONNX_LoadEmbedded(&test_graph, SIMPLE_ADD_ONNX, SIMPLE_ADD_ONNX_LEN, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "ct_parse_002 (Load failed)");
        return;
    }

    check_test(test_graph.ir_version == 12, "ct_parse_002");
}

static void ct_graph_001() {
    if (MATMUL_MODEL_ONNX_LEN == 0) {
        check_test(false, "ct_graph_001 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "ct_graph_001");
    test_graph.tensor_arena = test_arena;

    Status s = ONNX_LoadEmbedded(&test_graph, MATMUL_MODEL_ONNX, MATMUL_MODEL_ONNX_LEN, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "ct_graph_001 (Load failed)");
        return;
    }

    bool len_ok = (test_graph.schedule_length == 1);
    bool matmul_ok = false;

    for (uint32_t i = 0; i < test_graph.num_nodes; i++) {
        if (test_graph.nodes[i].op_type == ONNX_OP_MATMUL) {
            matmul_ok = (test_graph.nodes[i].exec_order == 0);
            break;
        }
    }

    check_test(len_ok && matmul_ok, "ct_graph_001");
}

/* ---- New unit tests for previously untested operators ---- */

static void ut_exp_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 3; s.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 0.0f; xd[1] = 1.0f; xd[2] = -1.0f;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Exp", ONNX_OP_EXP);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = float_close(zd[0], 1.0f, 0.01f) && float_close(zd[1], 2.718f, 0.05f) && float_close(zd[2], 0.368f, 0.05f);
    check_test(pass, "ut_exp_001");
}

static void ut_log_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 2; s.total_elements = 2;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 1.0f; xd[1] = 2.718f;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Log", ONNX_OP_LOG);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = float_close(zd[0], 0.0f, 0.1f) && float_close(zd[1], 1.0f, 0.2f);
    check_test(pass, "ut_log_001");
}

static void ut_sqrt_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 3; s.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 4.0f; xd[1] = 9.0f; xd[2] = 16.0f;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Sqrt", ONNX_OP_SQRT);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = float_close(zd[0], 2.0f, 0.001f) && float_close(zd[1], 3.0f, 0.001f) && float_close(zd[2], 4.0f, 0.001f);
    check_test(pass, "ut_sqrt_001");
}

static void ut_ceil_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 3; s.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 1.2f; xd[1] = -1.7f; xd[2] = 2.0f;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Ceil", ONNX_OP_CEIL);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = (zd[0] == 2.0f) && (zd[1] == -1.0f) && (zd[2] == 2.0f);
    check_test(pass, "ut_ceil_001");
}

static void ut_floor_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 3; s.total_elements = 3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 1.7f; xd[1] = -1.2f; xd[2] = 2.0f;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Floor", ONNX_OP_FLOOR);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = (zd[0] == 1.0f) && (zd[1] == -2.0f) && (zd[2] == 2.0f);
    check_test(pass, "ut_floor_001");
}

static void ut_sin_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 2; s.total_elements = 2;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 0.0f; xd[1] = 1.5707963f; /* pi/2 */
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Sin", ONNX_OP_SIN);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = float_close(zd[0], 0.0f, 0.01f) && float_close(zd[1], 1.0f, 0.05f);
    check_test(pass, "ut_sin_001");
}

static void ut_cos_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim = 1; s.dims[0] = 2; s.total_elements = 2;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0] = 0.0f; xd[1] = 3.1415926f; /* pi */
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Cos", ONNX_OP_COS);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = float_close(zd[0], 1.0f, 0.01f) && float_close(zd[1], -1.0f, 0.25f);
    check_test(pass, "ut_cos_001");
}

static void ut_reshape_001() {
    setup_test_graph();
    /* Input: 1D [6], Shape tensor: [2, 3] */
    ONNX_TensorShape s_in = {0}; s_in.ndim = 1; s_in.dims[0] = 6; s_in.total_elements = 6;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s_in);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    /* Shape tensor holds int64 values [2, 3] */
    ONNX_TensorShape s_shape = {0}; s_shape.ndim = 1; s_shape.dims[0] = 2; s_shape.total_elements = 2;
    ONNX_Tensor* shape_t = ONNX_Graph_CreateTensor(&test_graph, "shape", ONNX_DTYPE_INT64, &s_shape);
    shape_t->data_size = 2 * sizeof(int64_t);
    ONNX_Graph_AllocateTensor(&test_graph, shape_t);

    ONNX_TensorShape s_out = {0}; s_out.ndim = 2; s_out.dims[0] = 2; s_out.dims[1] = 3; s_out.total_elements = 6;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s_out);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* xd = (float*)x->data;
    xd[0]=1; xd[1]=2; xd[2]=3; xd[3]=4; xd[4]=5; xd[5]=6;
    int64_t* sd = (int64_t*)shape_t->data; sd[0] = 2; sd[1] = 3;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Reshape", ONNX_OP_RESHAPE);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddInput(node, shape_t); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* zd = (float*)z->data;
    bool pass = (zd[0]==1) && (zd[2]==3) && (zd[5]==6) && (z->shape.ndim == 2) && (z->shape.dims[0]==2) && (z->shape.dims[1]==3);
    check_test(pass, "ut_reshape_001");
}

static void ut_flatten_001() {
    setup_test_graph();
    /* Input: 2D [2,3] */
    ONNX_TensorShape s = {0}; s.ndim = 2; s.dims[0] = 2; s.dims[1] = 3; s.total_elements = 6;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* xd = (float*)x->data;
    xd[0]=1; xd[1]=2; xd[2]=3; xd[3]=4; xd[4]=5; xd[5]=6;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Flatten", ONNX_OP_FLATTEN);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    node->attributes.axis = 1;
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* zd = (float*)z->data;
    /* Flatten with axis=1: [2,3] -> [2, 3] (dim0 product=2, dim1 product=3) */
    bool pass = (z->shape.ndim == 2) && (z->shape.dims[0]==2) && (z->shape.dims[1]==3)
                && (zd[0]==1) && (zd[5]==6);
    check_test(pass, "ut_flatten_001");
}

static void ut_transpose_001() {
    setup_test_graph();
    /* Input: [2, 3] matrix */
    ONNX_TensorShape s_in = {0}; s_in.ndim = 2; s_in.dims[0] = 2; s_in.dims[1] = 3; s_in.total_elements = 6;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s_in);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape s_out = {0}; s_out.ndim = 2; s_out.dims[0] = 3; s_out.dims[1] = 2; s_out.total_elements = 6;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s_out);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    /* [[1,2,3],[4,5,6]] -> [[1,4],[2,5],[3,6]] */
    float* xd = (float*)x->data;
    xd[0]=1; xd[1]=2; xd[2]=3; xd[3]=4; xd[4]=5; xd[5]=6;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Transpose", ONNX_OP_TRANSPOSE);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* zd = (float*)z->data;
    bool pass = (zd[0]==1) && (zd[1]==4) && (zd[2]==2) && (zd[3]==5) && (zd[4]==3) && (zd[5]==6);
    check_test(pass, "ut_transpose_001");
}

static void ut_batchnorm_001() {
    setup_test_graph();
    /* X: [1, 2, 1, 1] - batch=1, channels=2, spatial=1x1 */
    ONNX_TensorShape sx = {0}; sx.ndim = 4; sx.dims[0]=1; sx.dims[1]=2; sx.dims[2]=1; sx.dims[3]=1; sx.total_elements=2;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &sx);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape sc = {0}; sc.ndim = 1; sc.dims[0] = 2; sc.total_elements = 2;
    ONNX_Tensor* scale = ONNX_Graph_CreateTensor(&test_graph, "scale", ONNX_DTYPE_FLOAT32, &sc);
    ONNX_Graph_AllocateTensor(&test_graph, scale);
    ONNX_Tensor* bias = ONNX_Graph_CreateTensor(&test_graph, "bias", ONNX_DTYPE_FLOAT32, &sc);
    ONNX_Graph_AllocateTensor(&test_graph, bias);
    ONNX_Tensor* mean = ONNX_Graph_CreateTensor(&test_graph, "mean", ONNX_DTYPE_FLOAT32, &sc);
    ONNX_Graph_AllocateTensor(&test_graph, mean);
    ONNX_Tensor* var = ONNX_Graph_CreateTensor(&test_graph, "var", ONNX_DTYPE_FLOAT32, &sc);
    ONNX_Graph_AllocateTensor(&test_graph, var);

    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &sx);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    /* x=[5, 10], scale=[1,1], bias=[0,0], mean=[5,10], var=[1,1] -> y=[0,0] */
    ((float*)x->data)[0] = 5.0f; ((float*)x->data)[1] = 10.0f;
    ((float*)scale->data)[0] = 1.0f; ((float*)scale->data)[1] = 1.0f;
    ((float*)bias->data)[0] = 0.0f; ((float*)bias->data)[1] = 0.0f;
    ((float*)mean->data)[0] = 5.0f; ((float*)mean->data)[1] = 10.0f;
    ((float*)var->data)[0] = 1.0f; ((float*)var->data)[1] = 1.0f;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "BatchNorm", ONNX_OP_BATCHNORM);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddInput(node, scale);
    ONNX_Node_AddInput(node, bias); ONNX_Node_AddInput(node, mean);
    ONNX_Node_AddInput(node, var); ONNX_Node_AddOutput(node, y);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* yd = (float*)y->data;
    bool pass = float_close(yd[0], 0.0f, 0.01f) && float_close(yd[1], 0.0f, 0.01f);
    check_test(pass, "ut_batchnorm_001");
}

static void ut_gemm_001() {
    setup_test_graph();
    /* A[2,3] * B[3,2] + C[2] = Y[2,2] with alpha=1, beta=1 */
    ONNX_TensorShape sa = {0}; sa.ndim=2; sa.dims[0]=2; sa.dims[1]=3; sa.total_elements=6;
    ONNX_Tensor* a = ONNX_Graph_CreateTensor(&test_graph, "A", ONNX_DTYPE_FLOAT32, &sa);
    ONNX_Graph_AllocateTensor(&test_graph, a);

    ONNX_TensorShape sb = {0}; sb.ndim=2; sb.dims[0]=3; sb.dims[1]=2; sb.total_elements=6;
    ONNX_Tensor* b = ONNX_Graph_CreateTensor(&test_graph, "B", ONNX_DTYPE_FLOAT32, &sb);
    ONNX_Graph_AllocateTensor(&test_graph, b);

    ONNX_TensorShape sc = {0}; sc.ndim=1; sc.dims[0]=2; sc.total_elements=2;
    ONNX_Tensor* c = ONNX_Graph_CreateTensor(&test_graph, "C", ONNX_DTYPE_FLOAT32, &sc);
    ONNX_Graph_AllocateTensor(&test_graph, c);

    ONNX_TensorShape sy = {0}; sy.ndim=2; sy.dims[0]=2; sy.dims[1]=2; sy.total_elements=4;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &sy);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    /* A = [[1,0,0],[0,1,0]], B = [[1,2],[3,4],[5,6]], C = [10,20] */
    float* ad = (float*)a->data; ad[0]=1; ad[1]=0; ad[2]=0; ad[3]=0; ad[4]=1; ad[5]=0;
    float* bd = (float*)b->data; bd[0]=1; bd[1]=2; bd[2]=3; bd[3]=4; bd[4]=5; bd[5]=6;
    float* cd = (float*)c->data; cd[0]=10; cd[1]=20;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Gemm", ONNX_OP_GEMM);
    ONNX_Node_AddInput(node, a); ONNX_Node_AddInput(node, b);
    ONNX_Node_AddInput(node, c); ONNX_Node_AddOutput(node, y);
    node->attributes.alpha = 1.0f; node->attributes.beta = 1.0f;
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    /* Y = 1*A*B + 1*C = [[1,2],[3,4]] + [10,20] = [[11,22],[13,24]] */
    float* yd = (float*)y->data;
    bool pass = float_close(yd[0], 11.0f, 0.01f) && float_close(yd[1], 22.0f, 0.01f)
             && float_close(yd[2], 13.0f, 0.01f) && float_close(yd[3], 24.0f, 0.01f);
    check_test(pass, "ut_gemm_001");
}

static void ut_concat_001() {
    setup_test_graph();
    /* Concat two [3] vectors -> [6] */
    ONNX_TensorShape s3 = {0}; s3.ndim=1; s3.dims[0]=3; s3.total_elements=3;
    ONNX_Tensor* a = ONNX_Graph_CreateTensor(&test_graph, "A", ONNX_DTYPE_FLOAT32, &s3);
    ONNX_Graph_AllocateTensor(&test_graph, a);
    ONNX_Tensor* b = ONNX_Graph_CreateTensor(&test_graph, "B", ONNX_DTYPE_FLOAT32, &s3);
    ONNX_Graph_AllocateTensor(&test_graph, b);

    ONNX_TensorShape s6 = {0}; s6.ndim=1; s6.dims[0]=6; s6.total_elements=6;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s6);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* ad = (float*)a->data; ad[0]=1; ad[1]=2; ad[2]=3;
    float* bd = (float*)b->data; bd[0]=4; bd[1]=5; bd[2]=6;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Concat", ONNX_OP_CONCAT);
    ONNX_Node_AddInput(node, a); ONNX_Node_AddInput(node, b); ONNX_Node_AddOutput(node, z);
    node->attributes.axis = 0;
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* zd = (float*)z->data;
    bool pass = (zd[0]==1) && (zd[1]==2) && (zd[2]==3) && (zd[3]==4) && (zd[4]==5) && (zd[5]==6);
    check_test(pass, "ut_concat_001");
}

static void ut_gavgpool_001() {
    setup_test_graph();
    /* Input: [1, 1, 2, 2] -> global avg -> [1, 1, 1, 1] */
    ONNX_TensorShape si = {0}; si.ndim=4; si.dims[0]=1; si.dims[1]=1; si.dims[2]=2; si.dims[3]=2; si.total_elements=4;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &si);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape so = {0}; so.ndim=4; so.dims[0]=1; so.dims[1]=1; so.dims[2]=1; so.dims[3]=1; so.total_elements=1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &so);
    ONNX_Graph_AllocateTensor(&test_graph, z);

    float* xd = (float*)x->data; xd[0]=1; xd[1]=2; xd[2]=3; xd[3]=4;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "GlobalAveragePool", ONNX_OP_GLOBALAVERAGEPOOL);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    float* zd = (float*)z->data;
    bool pass = float_close(zd[0], 2.5f, 0.01f); /* (1+2+3+4)/4 */
    check_test(pass, "ut_gavgpool_001");
}

static void ut_squeeze_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=1; xd[1]=2; xd[2]=3;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Squeeze", ONNX_OP_SQUEEZE);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = (zd[0]==1) && (zd[1]==2) && (zd[2]==3);
    check_test(pass, "ut_squeeze_001");
}

static void ut_unsqueeze_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=7; xd[1]=8; xd[2]=9;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Unsqueeze", ONNX_OP_UNSQUEEZE);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = (zd[0]==7) && (zd[1]==8) && (zd[2]==9);
    check_test(pass, "ut_unsqueeze_001");
}

static void ut_cast_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=1.5f; xd[1]=2.5f; xd[2]=3.5f;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Cast", ONNX_OP_CAST);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    float* zd = (float*)z->data;
    bool pass = (zd[0]==1.5f) && (zd[1]==2.5f) && (zd[2]==3.5f);
    check_test(pass, "ut_cast_001");
}

static void ut_reducesum_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_TensorShape s1 = {0}; s1.ndim=1; s1.dims[0]=1; s1.total_elements=1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s1);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=1; xd[1]=2; xd[2]=3;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "ReduceSum", ONNX_OP_REDUCESUM);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    bool pass = float_close(((float*)z->data)[0], 6.0f, 0.01f);
    check_test(pass, "ut_reducesum_001");
}

static void ut_reducemean_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_TensorShape s1 = {0}; s1.ndim=1; s1.dims[0]=1; s1.total_elements=1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s1);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=2; xd[1]=4; xd[2]=6;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "ReduceMean", ONNX_OP_REDUCEMEAN);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    bool pass = float_close(((float*)z->data)[0], 4.0f, 0.01f);
    check_test(pass, "ut_reducemean_001");
}

static void ut_reducemax_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_TensorShape s1 = {0}; s1.ndim=1; s1.dims[0]=1; s1.total_elements=1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s1);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=1; xd[1]=5; xd[2]=3;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "ReduceMax", ONNX_OP_REDUCEMAX);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    bool pass = float_close(((float*)z->data)[0], 5.0f, 0.01f);
    check_test(pass, "ut_reducemax_001");
}

static void ut_reducemin_001() {
    setup_test_graph();
    ONNX_TensorShape s = {0}; s.ndim=1; s.dims[0]=3; s.total_elements=3;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &s);
    ONNX_Graph_AllocateTensor(&test_graph, x);
    ONNX_TensorShape s1 = {0}; s1.ndim=1; s1.dims[0]=1; s1.total_elements=1;
    ONNX_Tensor* z = ONNX_Graph_CreateTensor(&test_graph, "Z", ONNX_DTYPE_FLOAT32, &s1);
    ONNX_Graph_AllocateTensor(&test_graph, z);
    float* xd = (float*)x->data; xd[0]=4; xd[1]=1; xd[2]=7;
    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "ReduceMin", ONNX_OP_REDUCEMIN);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, z);
    ONNX_Runtime_ExecuteNode(&test_ctx, node);
    bool pass = float_close(((float*)z->data)[0], 1.0f, 0.01f);
    check_test(pass, "ut_reducemin_001");
}

static void ut_conv_001() {
    setup_test_graph();
    /* X: [1,1,3,3], W: [1,1,2,2], no bias -> Y: [1,1,2,2] */
    ONNX_TensorShape sx = {0}; sx.ndim=4; sx.dims[0]=1; sx.dims[1]=1; sx.dims[2]=3; sx.dims[3]=3; sx.total_elements=9;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &sx);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape sw = {0}; sw.ndim=4; sw.dims[0]=1; sw.dims[1]=1; sw.dims[2]=2; sw.dims[3]=2; sw.total_elements=4;
    ONNX_Tensor* w = ONNX_Graph_CreateTensor(&test_graph, "W", ONNX_DTYPE_FLOAT32, &sw);
    ONNX_Graph_AllocateTensor(&test_graph, w);

    ONNX_TensorShape sy = {0}; sy.ndim=4; sy.dims[0]=1; sy.dims[1]=1; sy.dims[2]=2; sy.dims[3]=2; sy.total_elements=4;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &sy);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    /* X = [[1,2,3],[4,5,6],[7,8,9]], W = [[1,0],[0,1]] (identity-like kernel) */
    float* xd = (float*)x->data;
    xd[0]=1; xd[1]=2; xd[2]=3; xd[3]=4; xd[4]=5; xd[5]=6; xd[6]=7; xd[7]=8; xd[8]=9;
    float* wd = (float*)w->data; wd[0]=1; wd[1]=0; wd[2]=0; wd[3]=1;

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "Conv", ONNX_OP_CONV);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddInput(node, w); ONNX_Node_AddOutput(node, y);
    node->attributes.kernel_shape[0] = 2; node->attributes.kernel_shape[1] = 2;
    node->attributes.kernel_shape_len = 2;
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    /* Conv result: [[1*1+2*0+4*0+5*1, 2*1+3*0+5*0+6*1], [4*1+5*0+7*0+8*1, 5*1+6*0+8*0+9*1]]
       = [[6, 8], [12, 14]] */
    float* yd = (float*)y->data;
    bool pass = float_close(yd[0], 6.0f, 0.01f) && float_close(yd[1], 8.0f, 0.01f)
             && float_close(yd[2], 12.0f, 0.01f) && float_close(yd[3], 14.0f, 0.01f);
    check_test(pass, "ut_conv_001");
}

static void ut_maxpool_001() {
    setup_test_graph();
    /* X: [1,1,4,4], kernel 2x2, stride 2 -> Y: [1,1,2,2] */
    ONNX_TensorShape sx = {0}; sx.ndim=4; sx.dims[0]=1; sx.dims[1]=1; sx.dims[2]=4; sx.dims[3]=4; sx.total_elements=16;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &sx);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape sy = {0}; sy.ndim=4; sy.dims[0]=1; sy.dims[1]=1; sy.dims[2]=2; sy.dims[3]=2; sy.total_elements=4;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &sy);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    float* xd = (float*)x->data;
    /* [[1,2,3,4],[5,6,7,8],[9,10,11,12],[13,14,15,16]] */
    for (int i = 0; i < 16; i++) xd[i] = (float)(i + 1);

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "MaxPool", ONNX_OP_MAXPOOL);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, y);
    node->attributes.kernel_shape[0] = 2; node->attributes.kernel_shape[1] = 2;
    node->attributes.kernel_shape_len = 2;
    node->attributes.strides[0] = 2; node->attributes.strides[1] = 2;
    node->attributes.strides_len = 2;
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    /* MaxPool 2x2 stride 2: max of each 2x2 block = [6, 8, 14, 16] */
    float* yd = (float*)y->data;
    bool pass = float_close(yd[0], 6.0f, 0.01f) && float_close(yd[1], 8.0f, 0.01f)
             && float_close(yd[2], 14.0f, 0.01f) && float_close(yd[3], 16.0f, 0.01f);
    check_test(pass, "ut_maxpool_001");
}

static void ut_avgpool_001() {
    setup_test_graph();
    /* X: [1,1,4,4], kernel 2x2, stride 2 -> Y: [1,1,2,2] */
    ONNX_TensorShape sx = {0}; sx.ndim=4; sx.dims[0]=1; sx.dims[1]=1; sx.dims[2]=4; sx.dims[3]=4; sx.total_elements=16;
    ONNX_Tensor* x = ONNX_Graph_CreateTensor(&test_graph, "X", ONNX_DTYPE_FLOAT32, &sx);
    ONNX_Graph_AllocateTensor(&test_graph, x);

    ONNX_TensorShape sy = {0}; sy.ndim=4; sy.dims[0]=1; sy.dims[1]=1; sy.dims[2]=2; sy.dims[3]=2; sy.total_elements=4;
    ONNX_Tensor* y = ONNX_Graph_CreateTensor(&test_graph, "Y", ONNX_DTYPE_FLOAT32, &sy);
    ONNX_Graph_AllocateTensor(&test_graph, y);

    float* xd = (float*)x->data;
    for (int i = 0; i < 16; i++) xd[i] = (float)(i + 1);

    ONNX_Node* node = ONNX_Graph_AddNode(&test_graph, "AvgPool", ONNX_OP_AVGPOOL);
    ONNX_Node_AddInput(node, x); ONNX_Node_AddOutput(node, y);
    node->attributes.kernel_shape[0] = 2; node->attributes.kernel_shape[1] = 2;
    node->attributes.kernel_shape_len = 2;
    node->attributes.strides[0] = 2; node->attributes.strides[1] = 2;
    node->attributes.strides_len = 2;
    ONNX_Runtime_ExecuteNode(&test_ctx, node);

    /* AvgPool 2x2 stride 2: avg of each 2x2 block = [3.5, 5.5, 11.5, 13.5] */
    float* yd = (float*)y->data;
    bool pass = float_close(yd[0], 3.5f, 0.01f) && float_close(yd[1], 5.5f, 0.01f)
             && float_close(yd[2], 11.5f, 0.01f) && float_close(yd[3], 13.5f, 0.01f);
    check_test(pass, "ut_avgpool_001");
}

void ONNX_RunAllTests(void) {
    passed_tests = 0;
    total_tests = 0;

    HAL_UART_PutString("--- ONNX TEST SUITE ---\n");

    /* Layer 1: Existing unit tests */
    ut_add_001();
    ut_sub_001();
    ut_mul_001();
    ut_div_001();
    ut_matmul_001();
    ut_relu_001();
    ut_sigmoid_001();
    ut_tanh_001();
    ut_softmax_001();
    ut_leakyrelu_001();
    ut_abs_001();
    ut_neg_001();
    ut_clip_001();
    ut_identity_001();

    /* Layer 1: New unit tests */
    ut_exp_001();
    ut_log_001();
    ut_sqrt_001();
    ut_ceil_001();
    ut_floor_001();
    ut_sin_001();
    ut_cos_001();
    ut_reshape_001();
    ut_flatten_001();
    ut_transpose_001();
    ut_batchnorm_001();
    ut_gemm_001();
    ut_concat_001();
    ut_gavgpool_001();
    ut_squeeze_001();
    ut_unsqueeze_001();
    ut_cast_001();
    ut_reducesum_001();
    ut_reducemean_001();
    ut_reducemax_001();
    ut_reducemin_001();
    ut_conv_001();
    ut_maxpool_001();
    ut_avgpool_001();

    /* Layer 2: Integration tests */
    it_infer_001();
    it_infer_002();
    it_infer_003();
    it_infer_004();
    it_timer_001();

    /* Layer 3: Component tests */
    ct_parse_001();
    ct_parse_002();
    ct_graph_001();

    HAL_UART_PutString("[SUMMARY] ");
    HAL_UART_PutDec(passed_tests);
    HAL_UART_PutString("/");
    HAL_UART_PutDec(total_tests);
    HAL_UART_PutString(" passed\n");
}
