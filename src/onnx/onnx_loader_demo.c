/**
 * @file onnx_loader_demo.c
 * @brief Demo of ONNX model loading from embedded data
 */

#include "onnx/onnx_loader.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "hal/uart.h"
#include "kernel/thread.h"
#include "kernel/kmem.h"
#include "test_model.h"
#include "status.h"

#include "model_simple_add.h"
#include "model_complex.h"

static int simple_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

/* Helper to setup arena and run model */
static void ONNX_RunLoadedModel(const char* name, 
                                const uint8_t* model_data, 
                                uint32_t model_len, 
                                float* input_data, 
                                uint32_t input_elements,
                                const char* input_name)
{
    HAL_UART_PutString("\n--- Running ");
    HAL_UART_PutString((char*)name);
    HAL_UART_PutString(" ---\n");

    ONNX_Graph* graph = (ONNX_Graph*)KMEM_Alloc(sizeof(ONNX_Graph), 8);
    if (!graph) {
        HAL_UART_PutString("Failed to allocate graph memory\n");
        return;
    }
    ONNX_Graph_Init(graph, name);

    kmem_arena_t *tensor_arena = KMEM_ArenaCreate(1024 * 64);
    if (!tensor_arena) {
        HAL_UART_PutString("Arena creation failed\n");
        return;
    }
    graph->tensor_arena = tensor_arena;
    graph->tensor_memory_size = KMEM_ArenaGetTotal(tensor_arena);

    /* Load */
    Status status = ONNX_LoadEmbedded(graph, model_data, model_len, ONNX_FORMAT_PROTOBUF);
    if (status != STATUS_OK) {
        HAL_UART_PutString("Load failed\n");
        ONNX_Graph_Cleanup(graph);
        return;
    }
    
    /* Find input tensor */
    ONNX_Tensor* input_tensor = NULL;
    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        if (simple_strcmp(graph->tensors[i].name, input_name) == 0) {
            input_tensor = &graph->tensors[i];
            break;
        }
    }
    
    if (!input_tensor) {
        /* Input tensor might not be in graph yet because loader only parses GraphProto inputs partially.
           Let's just use graph->inputs[0] if available */
        if (graph->num_inputs > 0) {
            input_tensor = graph->inputs[0];
        } else {
            HAL_UART_PutString("No input found\n");
            ONNX_Graph_Cleanup(graph);
            return;
        }
    }
    /* If graph has no registered inputs (e.g. hand-crafted model without Field 11),
       register the located input_tensor as the sole graph input so that
       ONNX_Runtime_Inference sees num_inputs == 1. */
    if (graph->num_inputs == 0 && input_tensor) {
        graph->inputs[0] = input_tensor;
        graph->num_inputs = 1;
    }

    /* Allocate user input */
    if (!input_tensor->data) {
        input_tensor->data_size = input_elements * sizeof(float);
        ONNX_Graph_AllocateTensor(graph, input_tensor);
    }
    
    /* Copy data */
    float* dest = (float*)input_tensor->data;
    for (uint32_t i = 0; i < input_elements; i++) {
        dest[i] = input_data[i];
    }

    /* Schedule and Validate */
    ONNX_Graph_BuildDependencies(graph); // Corrected from _BuildDependencies(graph);
    ONNX_Graph_GenerateSchedule(graph);

    /* --- BASIC DEMO SHAPE INFERENCE --- */
    /* Set input tensor shape to allow downstream inference */
    if (input_elements == 4) {
        input_tensor->shape.ndim = 2;
        input_tensor->shape.dims[0] = 1;
        input_tensor->shape.dims[1] = 4;
        input_tensor->shape.total_elements = 4;
    } else {
        input_tensor->shape.ndim = 1;
        input_tensor->shape.dims[0] = input_elements;
        input_tensor->shape.total_elements = input_elements;
    }
    
    /* Propagate shapes */
    for (uint32_t i = 0; i < graph->schedule_length; i++) {
        ONNX_Node* node = graph->exec_schedule[i];
        if (node->op_type == ONNX_OP_MATMUL && node->num_inputs >= 2 && node->num_outputs >= 1) {
            ONNX_Tensor* a = node->inputs[0];
            ONNX_Tensor* b = node->inputs[1];
            ONNX_Tensor* c = node->outputs[0];
            if (a->shape.ndim == 2 && b->shape.ndim == 2) {
                c->shape.ndim = 2;
                c->shape.dims[0] = a->shape.dims[0];
                c->shape.dims[1] = b->shape.dims[1];
                c->shape.total_elements = c->shape.dims[0] * c->shape.dims[1];
            }
        }
        else if ((node->op_type == ONNX_OP_ADD || node->op_type == ONNX_OP_SUB ||
                  node->op_type == ONNX_OP_MUL || node->op_type == ONNX_OP_DIV) &&
                 node->num_inputs >= 2 && node->num_outputs >= 1) {
            ONNX_Tensor* a = node->inputs[0];
            ONNX_Tensor* b = node->inputs[1];
            ONNX_Tensor* c = node->outputs[0];
            ONNX_Tensor* larger = (a->shape.total_elements > b->shape.total_elements) ? a : b;
            c->shape = larger->shape;
        }
        else if (node->op_type == ONNX_OP_RELU && node->num_inputs >= 1 && node->num_outputs >= 1) {
            node->outputs[0]->shape = node->inputs[0]->shape;
        }
    }
    /* ---------------------------------- */

    ONNX_Graph_Validate(graph);

    /* Allocate all other tensors — update data_size from current shape first */
    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        if (!graph->tensors[i].data) {
            /* Recalculate data_size from current (post-propagation) shape */
            ONNX_Tensor* t = &graph->tensors[i];
            if (t->shape.total_elements > 0) {
                t->data_size = (uint32_t)(t->shape.total_elements * ONNX_GetDataTypeSize(t->dtype));
            }
            ONNX_Graph_AllocateTensor(graph, t);
        }
    }

    /* Run */
    ONNX_InferenceContext runtime;
    ONNX_Runtime_Init(&runtime, graph, 4096);
    
    ONNX_Tensor* inputs[1] = {input_tensor};
    ONNX_Tensor* outputs[1];
    status = ONNX_Runtime_Inference(&runtime, inputs, 1, outputs, 1);
    
    if (status == STATUS_OK && graph->num_outputs > 0) {
        HAL_UART_PutString("Result: [");
        float* out_data = (float*)graph->outputs[0]->data;
        for (uint32_t i = 0; i < graph->outputs[0]->shape.total_elements; i++) {
            if (i > 0) { HAL_UART_PutString(", "); }
            int32_t val = (int32_t)(out_data[i] * 1000.0f);
            if(val < 0) { HAL_UART_PutString("-"); val = -val; }
            HAL_UART_PutDec((uint32_t)(val / 1000));
            HAL_UART_PutString(".");
            uint32_t frac = val % 1000;
            if (frac < 100) HAL_UART_PutString("0");
            if (frac < 10) HAL_UART_PutString("0");
            HAL_UART_PutDec(frac);
        }
        HAL_UART_PutString("]\n");
    } else {
        HAL_UART_PutString("Inference failed\n");
    }

    ONNX_Graph_Cleanup(graph);
}

void ONNX_LoaderDemo(void)
{
    HAL_UART_PutString("\n========================================\n");
    HAL_UART_PutString("   ONNX Model Loader Demo\n");
    HAL_UART_PutString("========================================\n\n");
    
    float x1[3] = {2.0f, 3.0f, 4.0f};
    ONNX_RunLoadedModel("HandCraftedTest", test_onnx_model, test_onnx_model_len, x1, 3, "X");
    THREAD_Yield();
    
    ONNX_RunLoadedModel("SimpleAddPython", simple_add_onnx, simple_add_onnx_len, x1, 3, "X");
    THREAD_Yield();
    
    float x2[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    ONNX_RunLoadedModel("ComplexPython", complex_model_onnx, complex_model_onnx_len, x2, 4, "X");
    THREAD_Yield();
    
    HAL_UART_PutString("\n========================================\n\n");
}
