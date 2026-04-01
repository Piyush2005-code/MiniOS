/**
 * @file onnx_cmds.c
 * @brief ONNX Shell Command Implementations
 *
 * Provides the onnx_info, onnx_run, and onnx_unpack shell commands.
 * Fixed for AlexNet-class models: raised size limits, added LRN support,
 * fixed GEMM transB, and extended shape propagation for Conv/Pool/GEMM.
 */

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

#define MAX_MODEL_SIZE (8 * 1024 * 1024)    /* 8 MB — enough for a tiny AlexNet variant */
#define MAX_ARENA_SIZE (128 * 1024 * 1024)  /* 128 MB — fits small CNN activations + weights */

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
 * @brief Helper to parse a float from string
 */
static float parse_float(const char* str, const char** endptr) {
    float result = 0.0f;
    float fraction = 0.0f;
    int divisor = 1;
    int sign = 1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r' || *str == ',') str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Integer part
    while (*str >= '0' && *str <= '9') {
        result = result * 10.0f + (*str - '0');
        str++;
    }
    
    // Fractional part
    if (*str == '.') {
        str++;
        while (*str >= '0' && *str <= '9') {
            fraction = fraction * 10.0f + (*str - '0');
            divisor *= 10;
            str++;
        }
    }
    
    if (endptr) *endptr = str;
    return sign * (result + fraction / divisor);
}

/**
 * @brief onnx_run <filename> [input_file]
 * 
 * Runs an ONNX model file. If no input_file provided, runs with all 1.0f.
 * If input_file is provided, reads space/comma separated floats from it.
 */
static void cmd_onnx_run(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: onnx_run <model.onnx> [input_data.txt]\n");
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

    /* Provide a proper fallback input shape if the model didn't specify one.
     * Use [1, 3, 32, 32] — a minimal 4-D CNN input — rather than a bare [3],
     * which is meaningless for any conv-based model. */
    if (input_tensor->shape.total_elements == 0) {
        input_tensor->shape.ndim = 4;
        input_tensor->shape.dims[0] = 1;
        input_tensor->shape.dims[1] = 3;
        input_tensor->shape.dims[2] = 32;
        input_tensor->shape.dims[3] = 32;
        input_tensor->shape.total_elements = 1 * 3 * 32 * 32; /* 3072 */
        HAL_UART_PutString("onnx_run: Warning: model has no input shape; using fallback [1,3,32,32]\n");
    }

    /* Allocate user input */
    if (!input_tensor->data) {
        input_tensor->data_size = input_tensor->shape.total_elements * sizeof(float);
        ONNX_Graph_AllocateTensor(&g_graph, input_tensor);
    }
    
    float* dest = (float*)input_tensor->data;
    uint32_t elements_loaded = 0;

    /* Populate input data */
    if (argc >= 3) {
        /* Read from input file */
        ulfs_stat_t in_st;
        if (ULFS_Stat(argv[2], &in_st) == STATUS_OK && in_st.size > 0 && in_st.size < 65536) {
            int in_fd;
            if (ULFS_Open(argv[2], ULFS_O_RDONLY, &in_fd) == STATUS_OK) {
                /* Use end of model buffer temporarily for input string parsing if space permits */
                uint32_t max_read = MAX_MODEL_SIZE - bytes_read - 1;
                if (in_st.size < max_read) {
                    char* txt_buf = (char*)&g_model_buffer[bytes_read];
                    uint32_t txt_read = 0;
                    ULFS_Read(in_fd, (uint8_t*)txt_buf, in_st.size, &txt_read);
                    txt_buf[txt_read] = '\0';
                    
                    const char* ptr = txt_buf;
                    while (*ptr && elements_loaded < input_tensor->shape.total_elements) {
                         while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r' || *ptr == ',') ptr++;
                         if (!*ptr) break;
                         
                         const char* next_ptr;
                         dest[elements_loaded++] = parse_float(ptr, &next_ptr);
                         if (ptr == next_ptr) break; /* parse failed */
                         ptr = next_ptr;
                    }
                }
                ULFS_Close(in_fd);
            }
        }
        
        if (elements_loaded < input_tensor->shape.total_elements) {
            HAL_UART_PutString("onnx_run: Warning: input file provided too few elements (");
            print_dec(elements_loaded);
            HAL_UART_PutString("/");
            print_dec(input_tensor->shape.total_elements);
            HAL_UART_PutString("). Padding with 0.0f.\n");
            
            for (uint32_t i = elements_loaded; i < input_tensor->shape.total_elements; i++) {
                dest[i] = 0.0f;
            }
        } else {
            HAL_UART_PutString("onnx_run: Loaded ");
            print_dec(elements_loaded);
            HAL_UART_PutString(" inputs from ");
            HAL_UART_PutString(argv[2]);
            HAL_UART_PutString("\n");
        }
    } else {
        /* Populate input data with 1.0f default */
        for (uint32_t i = 0; i < input_tensor->shape.total_elements; i++) {
            dest[i] = 1.0f;
        }
        HAL_UART_PutString("onnx_run: Using default input (all 1.0f)\n");
    }

    /* Schedule */
    ONNX_Graph_BuildDependencies(&g_graph);
    ONNX_Graph_GenerateSchedule(&g_graph);

    /* Shape propagation — propagate output shapes before allocating tensors.
     * Without this, intermediate tensors remain 1-element placeholders and
     * the runtime tries to write into zero-sized buffers. */
    for (uint32_t i = 0; i < g_graph.schedule_length; i++) {
        ONNX_Node* node = g_graph.exec_schedule[i];

        /* --- MatMul [M,K]*[K,N]->[M,N] --- */
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
        /* --- GEMM [M,K]*[K,N or N,K]->[M,N] --- */
        else if (node->op_type == ONNX_OP_GEMM && node->num_inputs >= 2 && node->num_outputs >= 1) {
            ONNX_Tensor* a = node->inputs[0];
            ONNX_Tensor* b = node->inputs[1];
            ONNX_Tensor* c = node->outputs[0];
            if (a->shape.ndim >= 2 && b->shape.ndim >= 2) {
                uint64_t K = a->shape.dims[1];
                bool transB = (b->shape.dims[0] != K);
                uint64_t N_out = transB ? b->shape.dims[0] : b->shape.dims[1];
                c->shape.ndim = 2;
                c->shape.dims[0] = a->shape.dims[0];
                c->shape.dims[1] = N_out;
                c->shape.total_elements = c->shape.dims[0] * N_out;
            }
        }
        /* --- Flatten [N,C,H,W]->[N, C*H*W] --- */
        else if (node->op_type == ONNX_OP_FLATTEN && node->num_inputs >= 1 && node->num_outputs >= 1) {
            ONNX_Tensor* a = node->inputs[0];
            ONNX_Tensor* c = node->outputs[0];
            if (a->shape.ndim >= 2) {
                int axis = (int)node->attributes.axis;
                if (axis <= 0) axis = 1; /* ONNX default is axis=1 */
                if ((uint32_t)axis > a->shape.ndim) axis = (int)a->shape.ndim;
                uint64_t dim1 = 1;
                for (int d = 0; d < axis; d++) dim1 *= a->shape.dims[d];
                uint64_t dim2 = 1;
                for (uint32_t d = (uint32_t)axis; d < a->shape.ndim; d++) dim2 *= a->shape.dims[d];
                c->shape.ndim = 2;
                c->shape.dims[0] = dim1;
                c->shape.dims[1] = dim2;
                c->shape.total_elements = dim1 * dim2;
            }
        }
        /* --- Conv2D [N,Cin,H,W] -> [N,Cout,H_out,W_out] --- */
        else if (node->op_type == ONNX_OP_CONV && node->num_inputs >= 2 && node->num_outputs >= 1) {
            ONNX_Tensor* x = node->inputs[0];
            ONNX_Tensor* w = node->inputs[1];
            ONNX_Tensor* y = node->outputs[0];
            if (x->shape.ndim == 4 && w->shape.ndim == 4) {
                uint64_t h_in = x->shape.dims[2];
                uint64_t w_in = x->shape.dims[3];
                uint64_t k_h = (node->attributes.kernel_shape_len >= 1) ?
                                (uint64_t)node->attributes.kernel_shape[0] : w->shape.dims[2];
                uint64_t k_w = (node->attributes.kernel_shape_len >= 2) ?
                                (uint64_t)node->attributes.kernel_shape[1] : w->shape.dims[3];
                uint64_t s_h = (node->attributes.strides_len >= 1) ?
                                (uint64_t)node->attributes.strides[0] : 1;
                uint64_t s_w = (node->attributes.strides_len >= 2) ?
                                (uint64_t)node->attributes.strides[1] : s_h;
                uint64_t p_h = (node->attributes.pads_len >= 1) ?
                                (uint64_t)node->attributes.pads[0] : 0;
                uint64_t p_w = (node->attributes.pads_len >= 2) ?
                                (uint64_t)node->attributes.pads[1] : 0;
                uint64_t h_out = (h_in + 2*p_h - k_h) / s_h + 1;
                uint64_t w_out = (w_in + 2*p_w - k_w) / s_w + 1;
                y->shape.ndim = 4;
                y->shape.dims[0] = x->shape.dims[0];  /* batch N */
                y->shape.dims[1] = w->shape.dims[0];  /* C_out */
                y->shape.dims[2] = h_out;
                y->shape.dims[3] = w_out;
                y->shape.total_elements = y->shape.dims[0] * y->shape.dims[1] * h_out * w_out;
            }
        }
        /* --- MaxPool / AvgPool [N,C,H,W] -> [N,C,H_out,W_out] --- */
        else if ((node->op_type == ONNX_OP_MAXPOOL || node->op_type == ONNX_OP_AVGPOOL) &&
                 node->num_inputs >= 1 && node->num_outputs >= 1) {
            ONNX_Tensor* x = node->inputs[0];
            ONNX_Tensor* y = node->outputs[0];
            if (x->shape.ndim == 4) {
                uint64_t h_in = x->shape.dims[2];
                uint64_t w_in = x->shape.dims[3];
                uint64_t k_h = (node->attributes.kernel_shape_len >= 1) ?
                                (uint64_t)node->attributes.kernel_shape[0] : 2;
                uint64_t k_w = (node->attributes.kernel_shape_len >= 2) ?
                                (uint64_t)node->attributes.kernel_shape[1] : 2;
                uint64_t s_h = (node->attributes.strides_len >= 1) ?
                                (uint64_t)node->attributes.strides[0] : 2;
                uint64_t s_w = (node->attributes.strides_len >= 2) ?
                                (uint64_t)node->attributes.strides[1] : s_h;
                uint64_t p_h = (node->attributes.pads_len >= 1) ?
                                (uint64_t)node->attributes.pads[0] : 0;
                uint64_t p_w = (node->attributes.pads_len >= 2) ?
                                (uint64_t)node->attributes.pads[1] : 0;
                uint64_t h_out = (h_in + 2*p_h - k_h) / s_h + 1;
                uint64_t w_out = (w_in + 2*p_w - k_w) / s_w + 1;
                y->shape.ndim = 4;
                y->shape.dims[0] = x->shape.dims[0];
                y->shape.dims[1] = x->shape.dims[1];
                y->shape.dims[2] = h_out;
                y->shape.dims[3] = w_out;
                y->shape.total_elements = y->shape.dims[0] * y->shape.dims[1] * h_out * w_out;
            }
        }
        /* --- GlobalAveragePool [N,C,H,W] -> [N,C,1,1] --- */
        else if (node->op_type == ONNX_OP_GLOBALAVERAGEPOOL &&
                 node->num_inputs >= 1 && node->num_outputs >= 1) {
            ONNX_Tensor* x = node->inputs[0];
            ONNX_Tensor* y = node->outputs[0];
            if (x->shape.ndim >= 2) {
                y->shape.ndim = x->shape.ndim;
                y->shape.dims[0] = x->shape.dims[0];
                y->shape.dims[1] = x->shape.dims[1];
                for (uint32_t d = 2; d < x->shape.ndim; d++) y->shape.dims[d] = 1;
                y->shape.total_elements = y->shape.dims[0] * y->shape.dims[1];
            }
        }
        /* --- LRN: output same shape as input --- */
        else if (node->op_type == ONNX_OP_LRN &&
                 node->num_inputs >= 1 && node->num_outputs >= 1) {
            node->outputs[0]->shape = node->inputs[0]->shape;
        }
        /* --- Element-wise unary: copy input shape --- */
        else if (node->num_inputs >= 1 && node->num_outputs >= 1 &&
                 (node->op_type == ONNX_OP_RELU    || node->op_type == ONNX_OP_SIGMOID ||
                  node->op_type == ONNX_OP_TANH    || node->op_type == ONNX_OP_SOFTMAX ||
                  node->op_type == ONNX_OP_ABS     || node->op_type == ONNX_OP_NEG     ||
                  node->op_type == ONNX_OP_EXP     || node->op_type == ONNX_OP_LOG     ||
                  node->op_type == ONNX_OP_SQRT    || node->op_type == ONNX_OP_CEIL    ||
                  node->op_type == ONNX_OP_FLOOR   || node->op_type == ONNX_OP_SIN     ||
                  node->op_type == ONNX_OP_COS     || node->op_type == ONNX_OP_IDENTITY||
                  node->op_type == ONNX_OP_CLIP    || node->op_type == ONNX_OP_LEAKYRELU)) {
            node->outputs[0]->shape = node->inputs[0]->shape;
        }
        /* --- Element-wise binary: output = larger of two inputs --- */
        else if (node->num_inputs >= 2 && node->num_outputs >= 1 &&
                 (node->op_type == ONNX_OP_ADD || node->op_type == ONNX_OP_SUB ||
                  node->op_type == ONNX_OP_MUL || node->op_type == ONNX_OP_DIV)) {
            ONNX_Tensor* a = node->inputs[0];
            ONNX_Tensor* b = node->inputs[1];
            ONNX_Tensor* c = node->outputs[0];
            ONNX_Tensor* larger = (a->shape.total_elements > b->shape.total_elements) ? a : b;
            c->shape = larger->shape;
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
