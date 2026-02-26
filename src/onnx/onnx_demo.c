/**
 * @file onnx_demo.c
 * @brief Demo showing how to create and execute an ONNX computation graph
 *
 * This demonstrates:
 * 1. Building a simple computation graph manually (Y = X * W + B)
 * 2. Running inference
 * 3. Custom scheduling and node selection
 */

#include "onnx/onnx_types.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "hal/uart.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Demo: Simple Linear Model (Y = X * W + B)                        */
/* ------------------------------------------------------------------ */

Status ONNX_Demo_SimpleLinear(void)
{
    HAL_UART_PutString("\n");
    HAL_UART_PutString("=============================================\n");
    HAL_UART_PutString("  ONNX Demo: Simple Linear Model\n");
    HAL_UART_PutString("  Y = X * W + B\n");
    HAL_UART_PutString("=============================================\n");
    HAL_UART_PutString("\n");
    
    /* ---- Step 1: Create and initialize graph ---- */
    ONNX_Graph graph;
    Status status = ONNX_Graph_Init(&graph, "SimpleLinearModel");
    if (status != STATUS_OK) {
        HAL_UART_PutString("Failed to initialize graph\n");
        return status;
    }
    
    /* ---- Step 2: Define tensor shapes ---- */
    ONNX_TensorShape shape_X = {
        .ndim = 2,
        .dims = {1, 3},  /* [batch=1, features=3] */
        .total_elements = 3
    };
    
    ONNX_TensorShape shape_W = {
        .ndim = 2,
        .dims = {3, 2},  /* [in_features=3, out_features=2] */
        .total_elements = 6
    };
    
    ONNX_TensorShape shape_B = {
        .ndim = 1,
        .dims = {2, 0, 0, 0, 0, 0, 0, 0},  /* [out_features=2] */
        .total_elements = 2
    };
    
    ONNX_TensorShape shape_XW = {
        .ndim = 2,
        .dims = {1, 2},  /* [batch=1, out_features=2] */
        .total_elements = 2
    };
    
    ONNX_TensorShape shape_Y = shape_XW;
    
    /* ---- Step 3: Create tensors ---- */
    ONNX_Tensor* X = ONNX_Graph_CreateTensor(&graph, "X", ONNX_DTYPE_FLOAT32, &shape_X);
    ONNX_Tensor* W = ONNX_Graph_CreateTensor(&graph, "W", ONNX_DTYPE_FLOAT32, &shape_W);
    ONNX_Tensor* B = ONNX_Graph_CreateTensor(&graph, "B", ONNX_DTYPE_FLOAT32, &shape_B);
    ONNX_Tensor* XW = ONNX_Graph_CreateTensor(&graph, "XW", ONNX_DTYPE_FLOAT32, &shape_XW);
    ONNX_Tensor* Y = ONNX_Graph_CreateTensor(&graph, "Y", ONNX_DTYPE_FLOAT32, &shape_Y);
    
    if (!X || !W || !B || !XW || !Y) {
        HAL_UART_PutString("Failed to create tensors\n");
        return STATUS_ERROR_OUT_OF_MEMORY;
    }
    
    /* Mark W and B as initializers (constants) */
    W->is_initializer = true;
    B->is_initializer = true;
    
    /* ---- Step 4: Set graph inputs and outputs ---- */
    graph.inputs[0] = X;
    graph.num_inputs = 1;
    
    graph.outputs[0] = Y;
    graph.num_outputs = 1;
    
    graph.initializers[0] = W;
    graph.initializers[1] = B;
    graph.num_initializers = 2;
    
    /* ---- Step 5: Create computation nodes ---- */
    
    /* Node 1: MatMul - XW = X * W */
    ONNX_Node* matmul_node = ONNX_Graph_AddNode(&graph, "matmul", ONNX_OP_MATMUL);
    if (!matmul_node) {
        HAL_UART_PutString("Failed to add MatMul node\n");
        return STATUS_ERROR_OUT_OF_MEMORY;
    }
    ONNX_Node_AddInput(matmul_node, X);
    ONNX_Node_AddInput(matmul_node, W);
    ONNX_Node_AddOutput(matmul_node, XW);
    
    /* Node 2: Add - Y = XW + B */
    ONNX_Node* add_node = ONNX_Graph_AddNode(&graph, "add", ONNX_OP_ADD);
    if (!add_node) {
        HAL_UART_PutString("Failed to add Add node\n");
        return STATUS_ERROR_OUT_OF_MEMORY;
    }
    ONNX_Node_AddInput(add_node, XW);
    ONNX_Node_AddInput(add_node, B);
    ONNX_Node_AddOutput(add_node, Y);
    
    /* ---- Step 6: Allocate memory for tensors ---- */
    /* Allocate a simple memory pool (in real implementation, use heap) */
    #define TENSOR_POOL_SIZE (1024 * 64)  /* 64 KB */
    static uint8_t tensor_memory_pool[TENSOR_POOL_SIZE];
    
    graph.tensor_memory_pool = tensor_memory_pool;
    graph.tensor_memory_size = TENSOR_POOL_SIZE;
    graph.tensor_memory_used = 0;
    
    /* Allocate each tensor */
    for (uint32_t i = 0; i < graph.num_tensors; i++) {
        status = ONNX_Graph_AllocateTensor(&graph, &graph.tensors[i]);
        if (status != STATUS_OK) {
            HAL_UART_PutString("Failed to allocate tensor: ");
            HAL_UART_PutString(graph.tensors[i].name);
            HAL_UART_PutString("\n");
            return status;
        }
    }
    
    HAL_UART_PutString("[ONNX] Memory allocated: ");
    HAL_UART_PutDec((uint32_t)graph.tensor_memory_used);
    HAL_UART_PutString(" / ");
    HAL_UART_PutDec((uint32_t)graph.tensor_memory_size);
    HAL_UART_PutString(" bytes\n");
    
    /* ---- Step 7: Initialize weights and biases ---- */
    float* W_data = (float*)W->data;
    W_data[0] = 0.5f;  W_data[1] = -0.3f;
    W_data[2] = 0.2f;  W_data[3] = 0.7f;
    W_data[4] = -0.1f; W_data[5] = 0.4f;
    
    float* B_data = (float*)B->data;
    B_data[0] = 1.0f;
    B_data[1] = -0.5f;
    
    HAL_UART_PutString("[ONNX] Weights and biases initialized\n");
    
    /* ---- Step 8: Print graph structure ---- */
    ONNX_Graph_Print(&graph);
    
    /* ---- Step 9: Build dependencies and generate schedule ---- */
    HAL_UART_PutString("[ONNX] Building dependencies...\n");
    status = ONNX_Graph_BuildDependencies(&graph);
    if (status != STATUS_OK) {
        HAL_UART_PutString("Failed to build dependencies\n");
        return status;
    }
    
    HAL_UART_PutString("[ONNX] Generating execution schedule...\n");
    status = ONNX_Graph_GenerateSchedule(&graph);
    if (status != STATUS_OK) {
        HAL_UART_PutString("Failed to generate schedule\n");
        return status;
    }
    
    HAL_UART_PutString("[ONNX] Execution order:\n");
    for (uint32_t i = 0; i < graph.schedule_length; i++) {
        HAL_UART_PutString("  ");
        HAL_UART_PutDec(i + 1);
        HAL_UART_PutString(". ");
        HAL_UART_PutString(graph.exec_schedule[i]->name);
        HAL_UART_PutString("\n");
    }
    
    /* ---- Step 10: Validate graph ---- */
    status = ONNX_Graph_Validate(&graph);
    if (status != STATUS_OK) {
        HAL_UART_PutString("Graph validation failed\n");
        return status;
    }
    
    /* ---- Step 11: Initialize runtime ---- */
    ONNX_InferenceContext runtime_ctx;
    status = ONNX_Runtime_Init(&runtime_ctx, &graph, 4096);
    if (status != STATUS_OK) {
        HAL_UART_PutString("Failed to initialize runtime\n");
        return status;
    }
    
    /* ---- Step 12: Prepare input data ---- */
    float* X_data = (float*)X->data;
    X_data[0] = 1.0f;
    X_data[1] = 2.0f;
    X_data[2] = 3.0f;
    
    HAL_UART_PutString("\n[ONNX] Input data:\n");
    HAL_UART_PutString("  X = [1.0, 2.0, 3.0]\n");
    
    /* ---- Step 13: Run inference ---- */
    ONNX_Tensor* inputs[1] = {X};
    ONNX_Tensor* outputs[1];
    
    status = ONNX_Runtime_Inference(&runtime_ctx, inputs, 1, outputs, 1);
    if (status != STATUS_OK) {
        HAL_UART_PutString("Inference failed\n");
        return status;
    }
    
    /* ---- Step 14: Print results ---- */
    HAL_UART_PutString("\n[ONNX] Output:\n");
    HAL_UART_PutString("  Y = [");
    float* Y_data = (float*)outputs[0]->data;
    for (uint32_t i = 0; i < outputs[0]->shape.total_elements; i++) {
        if (i > 0) HAL_UART_PutString(", ");
        
        /* Print float (simplified - just integer part) */
        int32_t val = (int32_t)(Y_data[i] * 100.0f);
        if (val < 0) {
            HAL_UART_PutString("-");
            val = -val;
        }
        HAL_UART_PutDec((uint32_t)(val / 100));
        HAL_UART_PutString(".");
        HAL_UART_PutDec((uint32_t)(val % 100));
    }
    HAL_UART_PutString("]\n");
    
    /* ---- Step 15: Demonstrate custom scheduling ---- */
    HAL_UART_PutString("\n[ONNX] Demo: Custom Node Priority\n");
    ONNX_Node_SetPriority(add_node, 200);  /* Higher priority for Add */
    
    status = ONNX_Graph_GenerateCustomSchedule(&graph);
    if (status == STATUS_OK) {
        HAL_UART_PutString("[ONNX] Custom schedule generated\n");
        HAL_UART_PutString("  Note: Dependencies still respected\n");
    }
    
    /* ---- Step 16: Demonstrate selective execution ---- */
    HAL_UART_PutString("\n[ONNX] Demo: Execute up to specific node\n");
    HAL_UART_PutString("[ONNX] Executing up to 'matmul' node only...\n");
    
    /* Reset input */
    X_data[0] = 2.0f;
    X_data[1] = 3.0f;
    X_data[2] = 4.0f;
    
    status = ONNX_Runtime_ExecuteUpTo(&runtime_ctx, "matmul");
    if (status == STATUS_OK) {
        HAL_UART_PutString("[ONNX] Partial execution complete\n");
        HAL_UART_PutString("  Intermediate result (XW) computed\n");
    }
    
    /* ---- Cleanup ---- */
    ONNX_Runtime_Cleanup(&runtime_ctx);
    ONNX_Graph_Cleanup(&graph);
    
    HAL_UART_PutString("\n[ONNX] Demo complete!\n");
    HAL_UART_PutString("=============================================\n\n");
    
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API to run all demos                                       */
/* ------------------------------------------------------------------ */

void ONNX_RunDemos(void)
{
    HAL_UART_PutString("\n");
    HAL_UART_PutString("*********************************************\n");
    HAL_UART_PutString("*  ONNX Runtime Demos                       *\n");
    HAL_UART_PutString("*********************************************\n");
    
    Status status = ONNX_Demo_SimpleLinear();
    
    if (status == STATUS_OK) {
        HAL_UART_PutString("\n[SUCCESS] All ONNX demos passed!\n");
    } else {
        HAL_UART_PutString("\n[FAILED] Demo failed with status: ");
        HAL_UART_PutString(STATUS_ToString(status));
        HAL_UART_PutString("\n");
    }
    
    HAL_UART_PutString("\n");
    HAL_UART_PutString("*********************************************\n\n");
}
