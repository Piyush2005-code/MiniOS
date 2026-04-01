/**
 * @file onnx_cmds.c
 * @brief ONNX Shell Command Implementations
 */

#include "onnx/onnx_cmds.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "onnx/onnx_cmds.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "kernel/cmd.h"
#include "kernel/ulfs.h"
#include "kernel/kmem.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "lib/string.h"

extern unsigned char simple_add_onnx[];
extern unsigned int simple_add_onnx_len;

#define MAX_MODEL_SIZE (2 * 1024 * 1024)
#define MAX_ARENA_SIZE (1024 * 512)

static uint8_t g_model_buffer[MAX_MODEL_SIZE];
static ONNX_Graph g_graph;
static kmem_arena_t* g_tensor_arena = NULL;


static void print_dec(uint32_t v)
{
    HAL_UART_PutDec(v);
}


/**
 * @brief onnx_info <filename>
 * 
 * Load model into memory and print its topology.
 */
static void cmd_onnx_info(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: onnx_info <model.onnx>\n");
        return;
    }

    ulfs_stat_t st;
    Status s = ULFS_Stat(argv[1], &st);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_info: file not found or stat failed\n");
        return;
    }

    if (st.size == 0 || st.size > MAX_MODEL_SIZE) {
        HAL_UART_PutString("onnx_info: invalid file size (must be >0 and <2MB)\n");
        return;
    }

    int fd;
    s = ULFS_Open(argv[1], ULFS_O_RDONLY, &fd);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_info: failed to open file\n");
        return;
    }

    uint32_t bytes_read = 0;
    s = ULFS_Read(fd, g_model_buffer, st.size, &bytes_read);
    ULFS_Close(fd);

    if (s != STATUS_OK || bytes_read != st.size) {
        HAL_UART_PutString("onnx_info: read failed\n");
        return;
    }

    ONNX_Graph_Init(&g_graph, argv[1]);

    /* Load just to get topological info */
    s = ONNX_LoadEmbedded(&g_graph, g_model_buffer, bytes_read, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_info: model parsing failed\n");
        ONNX_Graph_Cleanup(&g_graph);
        return;
    }

    HAL_UART_PutString("\n========================================\n");
    HAL_UART_PutString("Model Info: ");
    HAL_UART_PutString(argv[1]);
    HAL_UART_PutString("\n========================================\n");
    HAL_UART_PutString("Size       : "); print_dec(bytes_read); HAL_UART_PutString(" B\n");
    HAL_UART_PutString("Operators  : "); print_dec(g_graph.num_nodes); HAL_UART_PutString("\n");
    HAL_UART_PutString("Tensors    : "); print_dec(g_graph.num_tensors); HAL_UART_PutString("\n");
    HAL_UART_PutString("Inputs     : "); print_dec(g_graph.num_inputs); HAL_UART_PutString("\n");
    HAL_UART_PutString("Outputs    : "); print_dec(g_graph.num_outputs); HAL_UART_PutString("\n");

    /* Optionally, print input shapes if known */
    for (uint32_t i = 0; i < g_graph.num_inputs; i++) {
        ONNX_Tensor* inp = g_graph.inputs[i];
        HAL_UART_PutString("  Input ["); print_dec(i); HAL_UART_PutString("]: '");
        HAL_UART_PutString(inp->name);
        HAL_UART_PutString("' shape=(");
        for(uint32_t d=0; d<inp->shape.ndim; d++) {
            if(d>0) HAL_UART_PutString(",");
            print_dec(inp->shape.dims[d]);
        }
        HAL_UART_PutString(")\n");
    }

    ONNX_Graph_Cleanup(&g_graph);
}


/**
 * @brief onnx_run <filename> [input_csv]
 * 
 * Runs an ONNX model file. If no input provided, runs with all zeros.
 * For now, only supports models with 1 input and 1 output for simplicity, or 
 * uses default shapes for the demo models.
 */
static void cmd_onnx_run(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: onnx_run <model.onnx>\n");
        return;
    }

    ulfs_stat_t st;
    Status s = ULFS_Stat(argv[1], &st);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_run: file not found or stat failed\n");
        return;
    }

    if (st.size == 0 || st.size > MAX_MODEL_SIZE) {
        HAL_UART_PutString("onnx_run: invalid file size\n");
        return;
    }

    int fd;
    s = ULFS_Open(argv[1], ULFS_O_RDONLY, &fd);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_run: failed to open file\n");
        return;
    }

    uint32_t bytes_read = 0;
    ULFS_Read(fd, g_model_buffer, st.size, &bytes_read);
    ULFS_Close(fd);

    ONNX_Graph_Init(&g_graph, argv[1]);

    if (!g_tensor_arena) {
        g_tensor_arena = KMEM_ArenaCreate(MAX_ARENA_SIZE); 
        if (!g_tensor_arena) {
            HAL_UART_PutString("onnx_run: Arena creation failed\n");
            return;
        }
    } else {
        KMEM_ArenaReset(g_tensor_arena);
    }
    
    g_graph.tensor_arena = g_tensor_arena;
    g_graph.tensor_memory_size = KMEM_ArenaGetTotal(g_tensor_arena);

    /* Load */
    s = ONNX_LoadEmbedded(&g_graph, g_model_buffer, bytes_read, ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_run: Load failed\n");
        ONNX_Graph_Cleanup(&g_graph);
        return;
    }
    
    ONNX_Tensor* input_tensor = NULL;
    if (g_graph.num_inputs > 0) {
        input_tensor = g_graph.inputs[0];
    } else {
        HAL_UART_PutString("onnx_run: No input tensor found in model\n");
        ONNX_Graph_Cleanup(&g_graph);
        return;
    }

    /* Provide a dummy input shape if the model didn't specify one */
    if (input_tensor->shape.total_elements == 0) {
        input_tensor->shape.ndim = 1;
        input_tensor->shape.dims[0] = 3;
        input_tensor->shape.total_elements = 3;
    }

    /* Allocate user input */
    if (!input_tensor->data) {
        input_tensor->data_size = input_tensor->shape.total_elements * sizeof(float);
        ONNX_Graph_AllocateTensor(&g_graph, input_tensor);
    }
    
    /* Populate input data with 1.0f */
    float* dest = (float*)input_tensor->data;
    for (uint32_t i = 0; i < input_tensor->shape.total_elements; i++) {
        dest[i] = 1.0f;
    }

    /* Schedule */
    ONNX_Graph_BuildDependencies(&g_graph);
    ONNX_Graph_GenerateSchedule(&g_graph);

    /* Very basic shape propagation */
    for (uint32_t i = 0; i < g_graph.schedule_length; i++) {
        ONNX_Node* node = g_graph.exec_schedule[i];
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

    ONNX_Graph_Validate(&g_graph);

    /* Allocate remaining tensors */
    for (uint32_t i = 0; i < g_graph.num_tensors; i++) {
        if (!g_graph.tensors[i].data) {
            ONNX_Tensor* t = &g_graph.tensors[i];
            if (t->shape.total_elements > 0) {
                t->data_size = (uint32_t)(t->shape.total_elements * ONNX_GetDataTypeSize(t->dtype));
            }
            ONNX_Graph_AllocateTensor(&g_graph, t);
        }
    }

    /* Run */
    ONNX_InferenceContext runtime;
    ONNX_Runtime_Init(&runtime, &g_graph, 4096);
    
    ONNX_Tensor* inputs[1] = {input_tensor};
    ONNX_Tensor* outputs[1];
    
    uint64_t start_time = HAL_Timer_GetTicks();
    s = ONNX_Runtime_Inference(&runtime, inputs, 1, outputs, 1);
    uint64_t elapsed_us = HAL_Timer_GetElapsedUs(start_time);
    
    if (s == STATUS_OK && g_graph.num_outputs > 0) {
        HAL_UART_PutString("Inference successful in ");
        print_dec((uint32_t)elapsed_us);
        HAL_UART_PutString(" us\n");

        HAL_UART_PutString("Result (showing first 10 elems): [");
        float* out_data = (float*)g_graph.outputs[0]->data;
        uint32_t elems_to_show = g_graph.outputs[0]->shape.total_elements;
        if (elems_to_show > 10) elems_to_show = 10;

        for (uint32_t i = 0; i < elems_to_show; i++) {
            if (i > 0) { HAL_UART_PutString(", "); }
            int32_t val = (int32_t)(out_data[i] * 1000.0f);
            if(val < 0) { HAL_UART_PutString("-"); val = -val; }
            print_dec((uint32_t)(val / 1000));
            HAL_UART_PutString(".");
            uint32_t frac = val % 1000;
            if (frac < 100) HAL_UART_PutString("0");
            if (frac < 10) HAL_UART_PutString("0");
            print_dec(frac);
        }
        if (g_graph.outputs[0]->shape.total_elements > 10) {
            HAL_UART_PutString(", ...");
        }
        HAL_UART_PutString("]\n");
    } else {
        HAL_UART_PutString("Inference failed! Status: ");
        HAL_UART_PutString(STATUS_ToString(s));
        HAL_UART_PutString("\n");
        if (g_graph.num_outputs == 0) {
            HAL_UART_PutString("(Graph has zero outputs defined)\n");
        }
    }

    ONNX_Graph_Cleanup(&g_graph);
}

/**
 * @brief onnx_unpack <filename>
 * 
 * Writes the built-in simple_add model to the filesystem for testing.
 */
static void cmd_onnx_unpack(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: onnx_unpack <dest.onnx>\n");
        return;
    }

    int fd;
    Status s = ULFS_Open(argv[1], ULFS_O_CREAT | ULFS_O_TRUNC | ULFS_O_WRONLY, &fd);
    if (s != STATUS_OK) {
        HAL_UART_PutString("onnx_unpack: failed to create file\n");
        return;
    }

    uint32_t written = 0;
    s = ULFS_Write(fd, simple_add_onnx, simple_add_onnx_len, &written);
    ULFS_Close(fd);

    if (s == STATUS_OK && written == simple_add_onnx_len) {
        HAL_UART_PutString("onnx_unpack: successfully wrote ");
        print_dec(written);
        HAL_UART_PutString(" bytes to ");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("\n");
        ULFS_Sync();
    } else {
        HAL_UART_PutString("onnx_unpack: write failed\n");
    }
}

Status ONNX_RegisterCommands(void)
{
    Status s;

    s = CMD_Register("onnx_info", "Print ONNX model metadata", cmd_onnx_info);
    if (s != STATUS_OK) return s;

    s = CMD_Register("onnx_run", "Run ONNX model inference", cmd_onnx_run);
    if (s != STATUS_OK) return s;

    s = CMD_Register("onnx_unpack", "Write built-in model to file", cmd_onnx_unpack);
    if (s != STATUS_OK) return s;

    return STATUS_OK;
}
