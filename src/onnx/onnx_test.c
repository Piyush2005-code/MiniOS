#include "onnx/onnx_test.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "kernel/kmem.h"
#include "status.h"
#include "types.h"

/* Helper embedded models - We'll declare these as extern for now, assuming they will be linked in or we provide dummy data if they don't exist yet */
extern const unsigned char test_onnx_model[];
extern const unsigned int test_onnx_model_len;

extern const unsigned char simple_add_onnx[];
extern const unsigned int simple_add_onnx_len;

extern const unsigned char complex_model_onnx[];
extern const unsigned int complex_model_onnx_len;


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
static uint8_t test_arena_buf[4096];

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

/* Stubs for testing framework compilation if models are missing */
const unsigned char __attribute__((weak)) test_onnx_model[1] = {0};
const unsigned int __attribute__((weak)) test_onnx_model_len = 0;
const unsigned char __attribute__((weak)) simple_add_onnx[1] = {0};
const unsigned int __attribute__((weak)) simple_add_onnx_len = 0;
const unsigned char __attribute__((weak)) complex_model_onnx[1] = {0};
const unsigned int __attribute__((weak)) complex_model_onnx_len = 0;

static void it_infer_001() {
    if (test_onnx_model_len == 0) {
        check_test(false, "it_infer_001 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "it_infer_001");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);

    Status s = ONNX_LoadEmbedded(&test_graph, test_onnx_model, test_onnx_model_len, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "it_infer_001 (Load failed)");
        return;
    }

    ONNX_Tensor* in = test_graph.inputs[0];
    ONNX_Tensor* out = test_graph.outputs[0];

    if (in && in->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in->data;
        in_data[0] = 2.0f; in_data[1] = 3.0f; in_data[2] = 4.0f;
    }

    ONNX_Tensor* inputs[] = { in };
    ONNX_Tensor* outputs[] = { out };

    s = ONNX_Runtime_Inference(&test_ctx, inputs, 1, outputs, 1);

    if (s == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 3.0f, 0.01f) &&
                    float_close(out_data[1], 5.0f, 0.01f) &&
                    float_close(out_data[2], 7.0f, 0.01f);
        check_test(pass, "it_infer_001");
    } else {
        check_test(false, "it_infer_001");
    }
}

static void it_infer_002() {
    if (simple_add_onnx_len == 0) {
        check_test(false, "it_infer_002 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "it_infer_002");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);

    Status s = ONNX_LoadEmbedded(&test_graph, simple_add_onnx, simple_add_onnx_len, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "it_infer_002 (Load failed)");
        return;
    }

    ONNX_Tensor* in = test_graph.inputs[0];
    ONNX_Tensor* out = test_graph.outputs[0];

    if (in && in->data_size >= 3 * sizeof(float)) {
        float* in_data = (float*)in->data;
        in_data[0] = 2.0f; in_data[1] = 3.0f; in_data[2] = 4.0f;
    }

    ONNX_Tensor* inputs[] = { in };
    ONNX_Tensor* outputs[] = { out };

    s = ONNX_Runtime_Inference(&test_ctx, inputs, 1, outputs, 1);

    if (s == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 3.0f, 0.01f) &&
                    float_close(out_data[1], 5.0f, 0.01f) &&
                    float_close(out_data[2], 7.0f, 0.01f);
        check_test(pass, "it_infer_002");
    } else {
        check_test(false, "it_infer_002");
    }
}

static void it_infer_003() {
    if (complex_model_onnx_len == 0) {
        check_test(false, "it_infer_003 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "it_infer_003");
    test_graph.tensor_arena = test_arena;
    ONNX_Runtime_Init(&test_ctx, &test_graph, 0);

    Status s = ONNX_LoadEmbedded(&test_graph, complex_model_onnx, complex_model_onnx_len, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "it_infer_003 (Load failed)");
        return;
    }

    ONNX_Tensor* in = test_graph.inputs[0];
    ONNX_Tensor* out = test_graph.outputs[0];

    if (in && in->data_size >= 4 * sizeof(float)) {
        float* in_data = (float*)in->data;
        in_data[0] = 1.0f; in_data[1] = 2.0f; in_data[2] = 3.0f; in_data[3] = 4.0f;
    }

    ONNX_Tensor* inputs[] = { in };
    ONNX_Tensor* outputs[] = { out };

    s = ONNX_Runtime_Inference(&test_ctx, inputs, 1, outputs, 1);

    if (s == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 1.300f, 0.05f) &&
                    float_close(out_data[1], 1.500f, 0.05f) &&
                    float_close(out_data[2], 0.800f, 0.05f);
        check_test(pass, "it_infer_003");
    } else {
        check_test(false, "it_infer_003");
    }
}

static void it_infer_004() {
    // Run it_infer_003 context again
    if (complex_model_onnx_len == 0) {
        check_test(false, "it_infer_004 (Model missing)");
        return;
    }

    ONNX_Tensor* in = test_graph.inputs[0];
    ONNX_Tensor* out = test_graph.outputs[0];

    ONNX_Tensor* inputs[] = { in };
    ONNX_Tensor* outputs[] = { out };

    Status s = ONNX_Runtime_Inference(&test_ctx, inputs, 1, outputs, 1);

    if (s == STATUS_OK && out && out->data) {
        float* out_data = (float*)out->data;
        bool pass = float_close(out_data[0], 1.300f, 0.05f) &&
                    float_close(out_data[1], 1.500f, 0.05f) &&
                    float_close(out_data[2], 0.800f, 0.05f);
        check_test(pass, "it_infer_004");
    } else {
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
    if (test_onnx_model_len == 0) {
        check_test(false, "ct_parse_001 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "ct_parse_001");
    test_graph.tensor_arena = test_arena;

    Status s = ONNX_LoadEmbedded(&test_graph, test_onnx_model, test_onnx_model_len, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "ct_parse_001 (Load failed)");
        return;
    }

    bool nodes_ok = (test_graph.num_nodes == 1);
    bool tensors_ok = (test_graph.num_tensors >= 3);

    bool b_ok = false;
    for (uint32_t i = 0; i < test_graph.num_tensors; i++) {
        ONNX_Tensor* t = &test_graph.tensors[i];
        if (t->name[0] == 'B' && t->name[1] == '\0') {
            if (t->is_initializer && t->data) {
                float* data = (float*)t->data;
                b_ok = float_close(data[0], 1.0f, 0.001f) &&
                       float_close(data[1], 2.0f, 0.001f) &&
                       float_close(data[2], 3.0f, 0.001f);
            }
            break;
        }
    }

    check_test(nodes_ok && tensors_ok && b_ok, "ct_parse_001");
}

static void ct_parse_002() {
    if (simple_add_onnx_len == 0) {
        check_test(false, "ct_parse_002 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "ct_parse_002");
    test_graph.tensor_arena = test_arena;

    Status s = ONNX_LoadEmbedded(&test_graph, simple_add_onnx, simple_add_onnx_len, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "ct_parse_002 (Load failed)");
        return;
    }

    check_test(test_graph.ir_version == 12, "ct_parse_002");
}

static void ct_graph_001() {
    if (complex_model_onnx_len == 0) {
        check_test(false, "ct_graph_001 (Model missing)");
        return;
    }

    test_arena = KMEM_ArenaCreate(sizeof(test_arena_buf));
    ONNX_Graph_Init(&test_graph, "ct_graph_001");
    test_graph.tensor_arena = test_arena;

    Status s = ONNX_LoadEmbedded(&test_graph, complex_model_onnx, complex_model_onnx_len, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        check_test(false, "ct_graph_001 (Load failed)");
        return;
    }

    bool len_ok = (test_graph.schedule_length == 3);
    bool matmul_ok = false;

    for (uint32_t i = 0; i < test_graph.num_nodes; i++) {
        if (test_graph.nodes[i].op_type == ONNX_OP_MATMUL) {
            matmul_ok = (test_graph.nodes[i].exec_order == 0);
            break;
        }
    }

    check_test(len_ok && matmul_ok, "ct_graph_001");
}

void ONNX_RunAllTests(void) {
    passed_tests = 0;
    total_tests = 0;

    HAL_UART_PutString("--- ONNX TEST SUITE ---\n");

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

    it_infer_001();
    it_infer_002();
    it_infer_003();
    it_infer_004();
    it_timer_001();

    ct_parse_001();
    ct_parse_002();
    ct_graph_001();

    HAL_UART_PutString("[SUMMARY] ");
    HAL_UART_PutDec(passed_tests);
    HAL_UART_PutString("/");
    HAL_UART_PutDec(total_tests);
    HAL_UART_PutString(" passed\n");
}
