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
#include "kernel/daemon.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "hal/arch.h"
#include "net/sfu.h"
#include "initfs_data.h"
#include "kernel/thread.h"
#include "lib/string.h"

extern unsigned char simple_add_onnx[];
extern unsigned int simple_add_onnx_len;

#define MAX_MODEL_SIZE (120 * 1024 * 1024)  /* 120 MB — enough for ResNet-50 */
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

    /* ONNX_LoadEmbedded already builds dependencies + schedule.
     * Keep this fallback for compatibility if a loader path skips it. */
    if (g_graph.schedule_length == 0) {
        ONNX_Graph_BuildDependencies(&g_graph);
        ONNX_Graph_GenerateSchedule(&g_graph);
    }

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
        /* --- Split: split one tensor along axis into multiple outputs --- */
        else if (node->op_type == ONNX_OP_SPLIT && node->num_inputs >= 1 && node->num_outputs >= 1) {
            ONNX_Tensor* in = node->inputs[0];
            int ax = (int)node->attributes.axis;
            if (ax < 0) ax += (int)in->shape.ndim;
            if (ax >= 0 && (uint32_t)ax < in->shape.ndim) {
                uint64_t axis_dim = in->shape.dims[ax];
                uint64_t each = (node->num_outputs > 0) ? (axis_dim / node->num_outputs) : 0;
                for (uint32_t o = 0; o < node->num_outputs; o++) {
                    ONNX_Tensor* out = node->outputs[o];
                    out->shape = in->shape;
                    if (node->attributes.kernel_shape_len == node->num_outputs) {
                        out->shape.dims[ax] = (uint64_t)node->attributes.kernel_shape[o];
                    } else {
                        out->shape.dims[ax] = each;
                    }
                    out->shape.total_elements = 1;
                    for (uint32_t d = 0; d < out->shape.ndim; d++) {
                        out->shape.total_elements *= out->shape.dims[d];
                    }
                }
            }
        }
        /* --- Concat: sum concat axis dimension across inputs --- */
        else if (node->op_type == ONNX_OP_CONCAT && node->num_inputs >= 2 && node->num_outputs >= 1) {
            ONNX_Tensor* first = node->inputs[0];
            ONNX_Tensor* co = node->outputs[0];
            co->shape = first->shape;
            int ax = (int)node->attributes.axis;
            if (ax < 0) ax += (int)first->shape.ndim;
            if (ax >= 0 && (uint32_t)ax < first->shape.ndim) {
                uint64_t cat_dim = 0;
                for (uint32_t j = 0; j < node->num_inputs; j++) {
                    if (node->inputs[j]->shape.ndim > (uint32_t)ax)
                        cat_dim += node->inputs[j]->shape.dims[ax];
                }
                co->shape.dims[ax] = (uint32_t)cat_dim;
                co->shape.total_elements = 0;
                for (uint32_t d = 0; d < co->shape.ndim; d++) {
                    if (co->shape.total_elements == 0) co->shape.total_elements = co->shape.dims[d];
                    else co->shape.total_elements *= co->shape.dims[d];
                }
            }
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


/* =========================================================================
 * onnx_bench [model.onnx] ...
 *
 * Benchmarks one or more ONNX models and prints a comparison table.
 * No arguments = benchmark all known /storage models automatically.
 * =========================================================================*/
#define BENCH_MAX_MODELS 16

typedef struct {
    char     name[64];
    uint32_t file_bytes;
    uint32_t num_nodes;
    uint32_t num_tensors;
    uint64_t param_count;
    uint64_t peak_mem_kb;
    uint64_t latency_us;
    bool     ok;
} BenchResult;

static void bench_dec_w(uint64_t v, uint32_t width)
{
    char buf[20]; uint32_t len = 0;
    if (v == 0) { buf[len++] = '0'; }
    else {
        uint64_t tmp = v;
        while (tmp > 0) { buf[len++] = (char)('0'+(tmp%10)); tmp /= 10; }
        for (uint32_t l=0,r=len-1; l<r; l++,r--) { char c=buf[l]; buf[l]=buf[r]; buf[r]=c; }
    }
    for (uint32_t i=len; i<width; i++) HAL_UART_PutString(" ");
    buf[len]='\0'; HAL_UART_PutString(buf);
}

static void bench_print_params(uint64_t p)
{
    if (p>=1000000ULL){
        bench_dec_w(p/1000000,3); HAL_UART_PutString(".");
        HAL_UART_PutDec((uint32_t)((p%1000000)/100000)); HAL_UART_PutString(" M");
    } else if (p>=1000ULL){
        bench_dec_w(p/1000,3); HAL_UART_PutString(".");
        HAL_UART_PutDec((uint32_t)((p%1000)/100)); HAL_UART_PutString(" K");
    } else { bench_dec_w(p,5); HAL_UART_PutString("  "); }
}

static void bench_print_latency(uint64_t us)
{
    if (us>=1000){
        bench_dec_w(us/1000,6); HAL_UART_PutString(".");
        HAL_UART_PutDec((uint32_t)((us%1000)/100)); HAL_UART_PutString(" ms");
    } else { bench_dec_w(us,6); HAL_UART_PutString(" us"); }
}

static void bench_print_mem(uint64_t kb)
{
    if (kb>=1024){
        bench_dec_w(kb/1024,4); HAL_UART_PutString(".");
        HAL_UART_PutDec((uint32_t)((kb%1024)*10/1024)); HAL_UART_PutString(" MB");
    } else { bench_dec_w(kb,6); HAL_UART_PutString(" KB"); }
}

static void bench_print_name(const char* s)
{
    uint32_t i=0;
    while (s[i]&&i<28){ HAL_UART_PutChar(s[i]); i++; }
    for (;i<28;i++) HAL_UART_PutString(" ");
}

static void bench_propagate(void)
{
    for (uint32_t i=0;i<g_graph.schedule_length;i++){
        ONNX_Node* nd=g_graph.exec_schedule[i];
        if (nd->op_type==ONNX_OP_CONV&&nd->num_inputs>=2&&nd->num_outputs>=1){
            ONNX_Tensor *x=nd->inputs[0],*w=nd->inputs[1],*y=nd->outputs[0];
            if (x->shape.ndim==4&&w->shape.ndim==4){
                uint64_t hi=x->shape.dims[2],wi2=x->shape.dims[3];
                uint64_t kh=nd->attributes.kernel_shape_len>=1?(uint64_t)nd->attributes.kernel_shape[0]:w->shape.dims[2];
                uint64_t kw=nd->attributes.kernel_shape_len>=2?(uint64_t)nd->attributes.kernel_shape[1]:w->shape.dims[3];
                uint64_t sh=nd->attributes.strides_len>=1?(uint64_t)nd->attributes.strides[0]:1;
                uint64_t sw=nd->attributes.strides_len>=2?(uint64_t)nd->attributes.strides[1]:sh;
                uint64_t ph=nd->attributes.pads_len>=1?(uint64_t)nd->attributes.pads[0]:0;
                uint64_t pw=nd->attributes.pads_len>=2?(uint64_t)nd->attributes.pads[1]:0;
                y->shape.ndim=4;
                y->shape.dims[0]=x->shape.dims[0]; y->shape.dims[1]=w->shape.dims[0];
                y->shape.dims[2]=(hi+2*ph-kh)/sh+1; y->shape.dims[3]=(wi2+2*pw-kw)/sw+1;
                y->shape.total_elements=y->shape.dims[0]*y->shape.dims[1]*y->shape.dims[2]*y->shape.dims[3];
            }
        } else if ((nd->op_type==ONNX_OP_MAXPOOL||nd->op_type==ONNX_OP_AVGPOOL)&&nd->num_inputs>=1&&nd->num_outputs>=1){
            ONNX_Tensor *x=nd->inputs[0],*y=nd->outputs[0];
            if (x->shape.ndim==4){
                uint64_t hi=x->shape.dims[2],wi2=x->shape.dims[3];
                uint64_t kh=nd->attributes.kernel_shape_len>=1?(uint64_t)nd->attributes.kernel_shape[0]:2;
                uint64_t kw=nd->attributes.kernel_shape_len>=2?(uint64_t)nd->attributes.kernel_shape[1]:2;
                uint64_t sh=nd->attributes.strides_len>=1?(uint64_t)nd->attributes.strides[0]:2;
                uint64_t sw=nd->attributes.strides_len>=2?(uint64_t)nd->attributes.strides[1]:sh;
                uint64_t ph=nd->attributes.pads_len>=1?(uint64_t)nd->attributes.pads[0]:0;
                uint64_t pw=nd->attributes.pads_len>=2?(uint64_t)nd->attributes.pads[1]:0;
                y->shape.ndim=4;
                y->shape.dims[0]=x->shape.dims[0]; y->shape.dims[1]=x->shape.dims[1];
                y->shape.dims[2]=(hi+2*ph-kh)/sh+1; y->shape.dims[3]=(wi2+2*pw-kw)/sw+1;
                y->shape.total_elements=y->shape.dims[0]*y->shape.dims[1]*y->shape.dims[2]*y->shape.dims[3];
            }
        } else if (nd->op_type==ONNX_OP_GLOBALAVERAGEPOOL&&nd->num_inputs>=1&&nd->num_outputs>=1){
            ONNX_Tensor *x=nd->inputs[0],*y=nd->outputs[0];
            if (x->shape.ndim>=2){
                y->shape.ndim=x->shape.ndim;
                y->shape.dims[0]=x->shape.dims[0]; y->shape.dims[1]=x->shape.dims[1];
                for (uint32_t d=2;d<x->shape.ndim;d++) y->shape.dims[d]=1;
                y->shape.total_elements=y->shape.dims[0]*y->shape.dims[1];
            }
        } else if (nd->op_type==ONNX_OP_GEMM&&nd->num_inputs>=2&&nd->num_outputs>=1){
            ONNX_Tensor *a=nd->inputs[0],*b=nd->inputs[1],*c=nd->outputs[0];
            if (a->shape.ndim>=2&&b->shape.ndim>=2){
                uint64_t K=a->shape.dims[1];
                bool tB=(b->shape.dims[0]!=K);
                uint64_t N=tB?b->shape.dims[0]:b->shape.dims[1];
                c->shape.ndim=2; c->shape.dims[0]=a->shape.dims[0]; c->shape.dims[1]=N;
                c->shape.total_elements=c->shape.dims[0]*N;
            }
        } else if (nd->op_type==ONNX_OP_FLATTEN&&nd->num_inputs>=1&&nd->num_outputs>=1){
            ONNX_Tensor *a=nd->inputs[0],*c=nd->outputs[0];
            if (a->shape.ndim>=2){
                int ax=(int)nd->attributes.axis; if (ax<=0) ax=1;
                if ((uint32_t)ax>a->shape.ndim) ax=(int)a->shape.ndim;
                uint64_t d1=1,d2=1;
                for (int d=0;d<ax;d++) d1*=a->shape.dims[d];
                for (uint32_t d=(uint32_t)ax;d<a->shape.ndim;d++) d2*=a->shape.dims[d];
                c->shape.ndim=2; c->shape.dims[0]=d1; c->shape.dims[1]=d2;
                c->shape.total_elements=d1*d2;
            }
        } else if (nd->op_type==ONNX_OP_LRN&&nd->num_inputs>=1&&nd->num_outputs>=1){
            nd->outputs[0]->shape=nd->inputs[0]->shape;
        } else if (nd->num_inputs>=1&&nd->num_outputs>=1&&
                   (nd->op_type==ONNX_OP_RELU||nd->op_type==ONNX_OP_SIGMOID||
                    nd->op_type==ONNX_OP_TANH||nd->op_type==ONNX_OP_SOFTMAX||
                    nd->op_type==ONNX_OP_LEAKYRELU||nd->op_type==ONNX_OP_IDENTITY||
                    nd->op_type==ONNX_OP_ABS||nd->op_type==ONNX_OP_NEG||nd->op_type==ONNX_OP_CLIP)){
            nd->outputs[0]->shape=nd->inputs[0]->shape;
        } else if (nd->num_inputs>=2&&nd->num_outputs>=1&&
                   (nd->op_type==ONNX_OP_ADD||nd->op_type==ONNX_OP_MUL||
                    nd->op_type==ONNX_OP_SUB||nd->op_type==ONNX_OP_DIV)){
            ONNX_Tensor *a=nd->inputs[0],*b=nd->inputs[1],*c=nd->outputs[0];
            c->shape=(a->shape.total_elements>b->shape.total_elements)?a->shape:b->shape;
        } else if (nd->op_type==ONNX_OP_SPLIT && nd->num_inputs>=1 && nd->num_outputs>=1) {
            ONNX_Tensor* in = nd->inputs[0];
            int ax = (int)nd->attributes.axis;
            if (ax < 0) ax += (int)in->shape.ndim;
            if (ax >= 0 && (uint32_t)ax < in->shape.ndim) {
                uint64_t axis_dim = in->shape.dims[ax];
                uint64_t each = (nd->num_outputs > 0) ? (axis_dim / nd->num_outputs) : 0;
                for (uint32_t o = 0; o < nd->num_outputs; o++) {
                    ONNX_Tensor* out = nd->outputs[o];
                    out->shape = in->shape;
                    if (nd->attributes.kernel_shape_len == nd->num_outputs) {
                        out->shape.dims[ax] = (uint64_t)nd->attributes.kernel_shape[o];
                    } else {
                        out->shape.dims[ax] = each;
                    }
                    out->shape.total_elements = 1;
                    for (uint32_t d = 0; d < out->shape.ndim; d++) {
                        out->shape.total_elements *= out->shape.dims[d];
                    }
                }
            }
        } else if (nd->op_type==ONNX_OP_CONCAT && nd->num_inputs>=2 && nd->num_outputs>=1) {
            /* Concat: output shape matches inputs on all dims except the concat axis,
             * where it equals the sum of that dim across all inputs. */
            ONNX_Tensor* first = nd->inputs[0];
            ONNX_Tensor* out = nd->outputs[0];
            out->shape = first->shape;
            int ax = (int)nd->attributes.axis;
            if (ax < 0) ax += (int)first->shape.ndim;
            if (ax >= 0 && (uint32_t)ax < first->shape.ndim) {
                uint64_t cat_dim = 0;
                for (uint32_t j = 0; j < nd->num_inputs; j++) {
                    if (nd->inputs[j]->shape.ndim > (uint32_t)ax)
                        cat_dim += nd->inputs[j]->shape.dims[ax];
                }
                out->shape.dims[ax] = (uint32_t)cat_dim;
                out->shape.total_elements = 0;
                for (uint32_t d = 0; d < out->shape.ndim; d++) {
                    if (out->shape.total_elements == 0) out->shape.total_elements = out->shape.dims[d];
                    else out->shape.total_elements *= out->shape.dims[d];
                }
            }
        }
    }
}

static void bench_one(const char* path, BenchResult* res)
{
    const char* base=path;
    for (const char* p=path;*p;p++) if(*p=='/') base=p+1;
    uint32_t ni=0;
    while (base[ni]&&ni<63){res->name[ni]=base[ni];ni++;} res->name[ni]='\0'; res->ok=false;

    ulfs_stat_t st;
    if (ULFS_Stat(path,&st)!=STATUS_OK||st.size==0||st.size>MAX_MODEL_SIZE){
        HAL_UART_PutString("    [SKIP] ");HAL_UART_PutString(res->name);
        HAL_UART_PutString(" (not found / too large)\n");return;}
    res->file_bytes=(uint32_t)st.size;

    int fd;
    if (ULFS_Open(path,ULFS_O_RDONLY,&fd)!=STATUS_OK){
        HAL_UART_PutString("    [SKIP] ");HAL_UART_PutString(res->name);
        HAL_UART_PutString(" (open failed)\n");return;}
    uint32_t bytes_read=0;
    Status rs=ULFS_Read(fd,g_model_buffer,st.size,&bytes_read);
    ULFS_Close(fd);
    if (rs!=STATUS_OK||bytes_read!=st.size){
        HAL_UART_PutString("    [SKIP] ");HAL_UART_PutString(res->name);
        HAL_UART_PutString(" (read error)\n");return;}

    if (g_tensor_arena){ KMEM_ArenaReset(g_tensor_arena); }
    g_tensor_arena=KMEM_ArenaCreate(MAX_ARENA_SIZE);
    if (!g_tensor_arena){
        HAL_UART_PutString("    [SKIP] ");HAL_UART_PutString(res->name);
        HAL_UART_PutString(" (arena alloc failed)\n");return;}

    ONNX_Graph_Init(&g_graph,res->name);
    g_graph.tensor_arena=g_tensor_arena;
    Status s=ONNX_LoadEmbedded(&g_graph,g_model_buffer,bytes_read,ONNX_FORMAT_PROTOBUF);
    if (s!=STATUS_OK){
        HAL_UART_PutString("    [FAIL] ");HAL_UART_PutString(res->name);
        HAL_UART_PutString(" (parse error)\n");ONNX_Graph_Cleanup(&g_graph);return;}

    res->num_nodes=g_graph.num_nodes; res->num_tensors=g_graph.num_tensors;
    res->param_count=0;
    for (uint32_t i=0;i<g_graph.num_initializers;i++)
        if (g_graph.initializers[i]) res->param_count+=g_graph.initializers[i]->shape.total_elements;

    if (g_graph.schedule_length == 0) {
        ONNX_Graph_BuildDependencies(&g_graph);
        ONNX_Graph_GenerateSchedule(&g_graph);
    }
    bench_propagate();

    if (g_graph.num_inputs>0){
        ONNX_Tensor* inp=g_graph.inputs[0];
        if (inp->shape.total_elements==0){
            inp->shape.ndim=4;inp->shape.dims[0]=1;inp->shape.dims[1]=3;
            inp->shape.dims[2]=32;inp->shape.dims[3]=32;inp->shape.total_elements=3072;}
        ONNX_DataType dt=inp->dtype?inp->dtype:ONNX_DTYPE_FLOAT32;
        inp->data_size=inp->shape.total_elements*ONNX_GetDataTypeSize(dt);
        if (!inp->data&&inp->data_size>0) ONNX_Graph_AllocateTensor(&g_graph,inp);
    }
    for (uint32_t i=0;i<g_graph.num_tensors;i++){
        ONNX_Tensor* t=&g_graph.tensors[i];
        if (!t->is_initializer&&!t->data&&t->data_size>0) ONNX_Graph_AllocateTensor(&g_graph,t);
    }

    ONNX_InferenceContext ctx;
    ctx.graph=&g_graph;ctx.workspace=NULL;ctx.workspace_size=0;
    ctx.total_inferences=0;ctx.total_time_us=0;

    ONNX_Tensor *out_ptrs[ONNX_MAX_OUTPUTS],*in_ptrs[ONNX_MAX_INPUTS];
    uint32_t n_in=g_graph.num_inputs<ONNX_MAX_INPUTS?g_graph.num_inputs:ONNX_MAX_INPUTS;
    for (uint32_t i=0;i<n_in;i++) in_ptrs[i]=g_graph.inputs[i];

    ONNX_Runtime_Inference(&ctx,in_ptrs,n_in,out_ptrs,g_graph.num_outputs);  /* warmup */
    uint64_t t0=HAL_Timer_GetTicks();
    s=ONNX_Runtime_Inference(&ctx,in_ptrs,n_in,out_ptrs,g_graph.num_outputs);
    res->latency_us=HAL_Timer_GetElapsedUs(t0);
    res->peak_mem_kb=KMEM_ArenaGetUsed(g_tensor_arena)/1024;
    res->ok=(s==STATUS_OK);
    ONNX_Graph_Cleanup(&g_graph);
}

static const char* g_bench_models[]={
    "/storage/tiny_mlp.onnx","/storage/resnet_micro.onnx",
    "/storage/conv_bn_net.onnx","/storage/lenet5.onnx",
    "/storage/alexnet_tiny.onnx","/storage/vgg_nano.onnx",
    "/storage/transformer_tiny.onnx",
};
#define G_BENCH_COUNT 7U

static void cmd_onnx_bench(int argc, char *argv[])
{
    BenchResult results[BENCH_MAX_MODELS];
    uint32_t count=0;

    if (argc>=2){
        for (int i=1;i<argc&&count<BENCH_MAX_MODELS;i++){
            HAL_UART_PutString("  >> ");HAL_UART_PutString(argv[i]);HAL_UART_PutString(" ...\n");
            bench_one(argv[i],&results[count++]); THREAD_Yield();}
    } else {
        HAL_UART_PutString("No models specified -- benchmarking all /storage models:\n");
        for (uint32_t i=0;i<G_BENCH_COUNT&&count<BENCH_MAX_MODELS;i++){
            HAL_UART_PutString("  >> ");HAL_UART_PutString(g_bench_models[i]);HAL_UART_PutString(" ...\n");
            bench_one(g_bench_models[i],&results[count++]); THREAD_Yield();}
    }
    if (count==0){HAL_UART_PutString("onnx_bench: no models to benchmark\n");return;}

    HAL_UART_PutString("\n=================================================================\n");
    HAL_UART_PutString(" ONNX Benchmark Results\n");
    HAL_UART_PutString("=================================================================\n");
    HAL_UART_PutString(" Model                       Nodes  Tensors    Params  "
                       "Peak Mem    Latency\n");
    HAL_UART_PutString(" ---------------------------  -----  -------  --------  "
                       "--------  ---------\n");
    for (uint32_t i=0;i<count;i++){
        BenchResult* r=&results[i];
        HAL_UART_PutString(" "); bench_print_name(r->name);
        if (!r->ok){HAL_UART_PutString("  [FAILED]\n");continue;}
        bench_dec_w(r->num_nodes,  5); HAL_UART_PutString("  ");
        bench_dec_w(r->num_tensors,7); HAL_UART_PutString("  ");
        bench_print_params(r->param_count);  HAL_UART_PutString("  ");
        bench_print_mem(r->peak_mem_kb);     HAL_UART_PutString("  ");
        bench_print_latency(r->latency_us);  HAL_UART_PutString("\n");
    }
    HAL_UART_PutString("=================================================================\n");
    HAL_UART_PutString(" Models   : "); bench_dec_w(count,1); HAL_UART_PutString("\n");
    HAL_UART_PutString(" Params   = total weight+bias elements (all initialisers)\n");
    HAL_UART_PutString(" Peak Mem = arena bytes consumed after inference\n");
    HAL_UART_PutString(" Latency  = single forward pass (after 1 warmup run)\n");
    HAL_UART_PutString("=================================================================\n\n");
}


static int parse_int(const char* ptr) {
    int r = 0;
    while (*ptr >= '0' && *ptr <= '9') { r = r * 10 + (*ptr - '0'); ptr++; }
    return r;
}

static int _strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

#define BENCH_JSON_BUF_SIZE (4096 + 200 * 16)  /* header ~4 KB + up to 200 latency entries */
#define BENCH_MAX_LATENCIES 200

static void cmd_run_bench(int argc, char *argv[])
{
    const char* model_path = NULL;
    const char* input_path = NULL;
    const char* out_path = NULL;
    int runs = 1;
    int warmup = 0;

    static char mpath[128], ipath[128], opath[128];

    if (argc == 2 && argv[1][0] != '-') {
        const char *name = argv[1];
        char *p = mpath;
        const char *mp = "bench/models/"; while(*mp) *p++=*mp++;
        const char *n = name; while(*n) *p++=*n++;
        *p++='.'; *p++='o'; *p++='n'; *p++='n'; *p++='x'; *p='\0';

        p = ipath;
        const char *ip = "bench/inputs/"; while(*ip) *p++=*ip++;
        n = name; while(*n) *p++=*n++;
        *p++='.'; *p++='b'; *p++='i'; *p++='n'; *p='\0';

        p = opath;
        const char *op = "bench/results/"; while(*op) *p++=*op++;
        n=name; while(*n) *p++=*n++;
        *p++='.'; *p++='j'; *p++='s'; *p++='o'; *p++='n'; *p='\0';

        model_path = mpath;
        input_path = ipath;
        out_path = opath;
    } else {
        for (int i = 1; i < argc; i++) {
            if (_strcmp(argv[i], "--model") == 0 && i + 1 < argc) model_path = argv[++i];
            else if (_strcmp(argv[i], "--input") == 0 && i + 1 < argc) input_path = argv[++i];
            else if (_strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs = parse_int(argv[++i]);
            else if (_strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = parse_int(argv[++i]);
            else if (_strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        }

        bool path_too_long = false;

        if (model_path) {
            uint32_t j = 0;
            const char* s = model_path;
            while (*s && j < sizeof(mpath) - 1) mpath[j++] = *s++;
            mpath[j] = '\0';
            if (*s) path_too_long = true;
            model_path = mpath;
        }

        if (input_path) {
            uint32_t j = 0;
            const char* s = input_path;
            while (*s && j < sizeof(ipath) - 1) ipath[j++] = *s++;
            ipath[j] = '\0';
            if (*s) path_too_long = true;
            input_path = ipath;
        }

        if (out_path) {
            uint32_t j = 0;
            const char* s = out_path;
            while (*s && j < sizeof(opath) - 1) opath[j++] = *s++;
            opath[j] = '\0';
            if (*s) path_too_long = true;
            out_path = opath;
        }

        if (path_too_long) {
            HAL_UART_PutString("run_bench: path too long\n");
            return;
        }
    }

    if (!model_path || !input_path || !out_path) {
        HAL_UART_PutString("Usage: /bench/run_bench <name>\n");
        return;
    }

    if (runs > BENCH_MAX_LATENCIES) runs = BENCH_MAX_LATENCIES;

    static char json_buf[BENCH_JSON_BUF_SIZE]; int json_len = 0;
    #define JPRINT(str) { const char* s=(str); while(*s && json_len < (int)sizeof(json_buf) - 2) json_buf[json_len++] = *s++; }

    if (!g_tensor_arena) g_tensor_arena = KMEM_ArenaCreate(MAX_ARENA_SIZE);
    else KMEM_ArenaReset(g_tensor_arena);

    uint64_t load_t0 = HAL_Timer_GetTicks();
    const uint8_t* model_data_ptr = NULL;
    uint32_t model_bytes_len = 0;
    ONNX_Format model_format = ONNX_FORMAT_PROTOBUF;
    const char* load_model_path = model_path;
    bool tried_custom_sidecar = false;
    char custom_model_path[128];
    custom_model_path[0] = '\0';

    /* Prefer a pre-converted sidecar custom model: <name>.mio */
    {
        uint32_t ml = 0;
        while (model_path[ml] && ml < sizeof(custom_model_path) - 1) {
            custom_model_path[ml] = model_path[ml];
            ml++;
        }
        custom_model_path[ml] = '\0';

        if (!model_path[ml] && ml >= 5 &&
            custom_model_path[ml - 5] == '.' &&
            custom_model_path[ml - 4] == 'o' &&
            custom_model_path[ml - 3] == 'n' &&
            custom_model_path[ml - 2] == 'n' &&
            custom_model_path[ml - 1] == 'x') {
            custom_model_path[ml - 4] = 'm';
            custom_model_path[ml - 3] = 'i';
            custom_model_path[ml - 2] = 'o';
            custom_model_path[ml - 1] = '\0';
            tried_custom_sidecar = true;
        }
    }
    int fd;
    
    extern const initfs_entry_t initfs_entries[];
#ifndef INITFS_NUM_ENTRIES
#define INITFS_NUM_ENTRIES 15U
#endif

    if (tried_custom_sidecar) {
        for (uint32_t i = 0; i < INITFS_NUM_ENTRIES; i++) {
            if (_strcmp(initfs_entries[i].path, custom_model_path + (custom_model_path[0] == '/' ? 1 : 0)) == 0) {
                model_data_ptr = initfs_entries[i].data;
                model_bytes_len = initfs_entries[i].size;
                load_model_path = custom_model_path;
                break;
            }
        }
    }

    if (!model_data_ptr) {
        for (uint32_t i = 0; i < INITFS_NUM_ENTRIES; i++) {
            if (_strcmp(initfs_entries[i].path, model_path + (model_path[0] == '/' ? 1 : 0)) == 0) {
                model_data_ptr = initfs_entries[i].data;
                model_bytes_len = initfs_entries[i].size;
                load_model_path = model_path;
                break;
            }
        }
    }

    if (!model_data_ptr) {
        if (tried_custom_sidecar) {
            ulfs_stat_t cst;
            if (ULFS_Stat(custom_model_path, &cst) == STATUS_OK && cst.size > 0 && cst.size <= MAX_MODEL_SIZE) {
                if (ULFS_Open(custom_model_path, ULFS_O_RDONLY, &fd) == STATUS_OK) {
                    uint32_t bytes_read = 0;
                    ULFS_Read(fd, g_model_buffer, cst.size, &bytes_read);
                    ULFS_Close(fd);
                    if (bytes_read == cst.size) {
                        model_data_ptr = g_model_buffer;
                        model_bytes_len = bytes_read;
                        load_model_path = custom_model_path;
                    }
                }
            }
        }
    }

    if (!model_data_ptr) {
        ulfs_stat_t st;
        if (ULFS_Stat(model_path, &st) != STATUS_OK || st.size > MAX_MODEL_SIZE || st.size == 0) {
            HAL_UART_PutString("run_bench: model stat failed\n"); return;
        }
        if (ULFS_Open(model_path, ULFS_O_RDONLY, &fd) != STATUS_OK) return;
        uint32_t bytes_read = 0; ULFS_Read(fd, g_model_buffer, st.size, &bytes_read); ULFS_Close(fd);
        model_data_ptr = g_model_buffer;
        model_bytes_len = bytes_read;
        load_model_path = model_path;
    }

    if (model_bytes_len >= sizeof(ONNX_CustomHeader)) {
        ONNX_CustomHeader hdr;
        memcpy(&hdr, model_data_ptr, sizeof(hdr));
        if (hdr.magic == ONNX_CUSTOM_MAGIC && hdr.version == ONNX_CUSTOM_VERSION) {
            model_format = ONNX_FORMAT_CUSTOM_BINARY;
        }
    }

    ONNX_Graph_Init(&g_graph, "bench");
    g_graph.tensor_arena = g_tensor_arena;
    Status load_status = ONNX_LoadEmbedded(&g_graph, model_data_ptr, model_bytes_len, model_format);
    if (load_status != STATUS_OK && model_format == ONNX_FORMAT_CUSTOM_BINARY) {
        /* Fallback: tolerate stale/malformed sidecars by retrying protobuf path. */
        ONNX_Graph_Cleanup(&g_graph);
        KMEM_ArenaReset(g_tensor_arena);
        ONNX_Graph_Init(&g_graph, "bench");
        g_graph.tensor_arena = g_tensor_arena;
        model_format = ONNX_FORMAT_PROTOBUF;
        load_status = ONNX_LoadEmbedded(&g_graph, model_data_ptr, model_bytes_len, model_format);
    }
    if (load_status != STATUS_OK) {
        HAL_UART_PutString("run_bench: Load failed for ");
        HAL_UART_PutString(load_model_path);
        HAL_UART_PutString("\n");
        return;
    }

    if (g_graph.schedule_length == 0) {
        ONNX_Graph_BuildDependencies(&g_graph);
        ONNX_Graph_GenerateSchedule(&g_graph);
    }
    bench_propagate();

    if (g_graph.num_inputs > 0) {
        ONNX_Tensor* inp = g_graph.inputs[0];
        if (inp->shape.total_elements == 0) {
            inp->shape.ndim = 4; inp->shape.dims[0] = 1; inp->shape.dims[1] = 3; 
            inp->shape.dims[2] = 224; inp->shape.dims[3] = 224; 
            inp->shape.total_elements = 1 * 3 * 224 * 224;
        }
        inp->data_size = inp->shape.total_elements * sizeof(float);
        ONNX_Graph_AllocateTensor(&g_graph, inp);
        
        const uint8_t* in_data_ptr = NULL;
        uint32_t in_bytes_len = 0;
        for (uint32_t i=0; i<INITFS_NUM_ENTRIES; i++) {
            if (_strcmp(initfs_entries[i].path, input_path + (input_path[0]=='/'?1:0)) == 0) {
                in_data_ptr = initfs_entries[i].data;
                in_bytes_len = initfs_entries[i].size;
                break;
            }
        }

        if (in_data_ptr) {
            if (in_bytes_len <= inp->data_size) {
                for (uint32_t j=0; j<in_bytes_len; j++) ((uint8_t*)inp->data)[j] = in_data_ptr[j];
            }
        } else {
            ulfs_stat_t st;
            if (ULFS_Stat(input_path, &st) == STATUS_OK && st.size <= inp->data_size) {
                int fd; if (ULFS_Open(input_path, ULFS_O_RDONLY, &fd) == STATUS_OK) {
                    uint32_t br = 0; ULFS_Read(fd, inp->data, st.size, &br); ULFS_Close(fd);
                }
            }
        }
    }

    /* Do not eagerly allocate non-initializer tensors here.
     * Many intermediates still carry placeholder shapes/sizes at this stage;
     * allocating them early can cause undersized buffers and memory corruption.
     * The runtime prepares/allocates outputs with shape-aware logic during warmup. */

    uint64_t load_ms = HAL_Timer_GetElapsedUs(load_t0) / 1000;
    ONNX_InferenceContext ctx; ctx.graph = &g_graph; ctx.workspace = NULL; ctx.workspace_size = 0;
    ONNX_Tensor *out_ptrs[ONNX_MAX_OUTPUTS], *in_ptrs[ONNX_MAX_INPUTS];
    uint32_t n_in = g_graph.num_inputs < ONNX_MAX_INPUTS ? g_graph.num_inputs : ONNX_MAX_INPUTS;
    for (uint32_t i=0;i<n_in;i++) in_ptrs[i] = g_graph.inputs[i];

    bool prev_runtime_verbose = ONNX_Runtime_GetVerbose();
    bool prev_runtime_yield = ONNX_Runtime_GetYieldBetweenNodes();
    bool prev_runtime_node_profiling = ONNX_Runtime_GetNodeProfiling();
    bool prev_runtime_prepare_outputs = ONNX_Runtime_GetPrepareNodeOutputs();
    bool prev_daemon_telemetry = DAEMON_GetTelemetryEnabled();
    bool prev_sfu_tick = SFU_GetTickEnabled();
    bool prev_felix_mode = SCHED_GetFelixMode();
    bool bench_tuning_applied = true;
    bool debug_single_run = (runs <= 1);
    ONNX_Runtime_SetVerbose(debug_single_run ? true : false);
    ONNX_Runtime_SetYieldBetweenNodes(false);
    ONNX_Runtime_SetNodeProfiling(debug_single_run ? true : false);
    ONNX_Runtime_SetPrepareNodeOutputs(true);
    DAEMON_SetTelemetryEnabled(false);
    SFU_SetTickEnabled(false);
    SCHED_SetFelixMode(true);

    Status inf_s = STATUS_OK;
    uint64_t bench_irq_flags = arch_irq_save();
    bool bench_irqs_disabled = true;

    for (int i = 0; i < warmup; i++) {
        inf_s = ONNX_Runtime_Inference(&ctx, in_ptrs, n_in, out_ptrs, g_graph.num_outputs);
        if (inf_s != STATUS_OK) {
            break;
        }
    }

    /* If warmup ran, timed loop can skip repeated output-prep checks.
     * For warmup=0, keep prep enabled for the first timed pass and disable after it. */
    bool disable_prepare_after_first_timed = (warmup == 0);
    if (!disable_prepare_after_first_timed) {
        ONNX_Runtime_SetPrepareNodeOutputs(false);
    }
    if (debug_single_run) {
        /* Keep profile focused on the timed pass, not warmup setup. */
        ONNX_Runtime_ResetStats(&ctx);
    }

    if (inf_s != STATUS_OK) {
        if (bench_irqs_disabled) {
            arch_irq_restore(bench_irq_flags);
            bench_irqs_disabled = false;
        }
        if (bench_tuning_applied) {
            ONNX_Runtime_SetVerbose(prev_runtime_verbose);
            ONNX_Runtime_SetYieldBetweenNodes(prev_runtime_yield);
            ONNX_Runtime_SetNodeProfiling(prev_runtime_node_profiling);
            ONNX_Runtime_SetPrepareNodeOutputs(prev_runtime_prepare_outputs);
            DAEMON_SetTelemetryEnabled(prev_daemon_telemetry);
            SFU_SetTickEnabled(prev_sfu_tick);
            SCHED_SetFelixMode(prev_felix_mode);
        }
        HAL_UART_PutString("run_bench: warmup inference failed: ");
        HAL_UART_PutString(STATUS_ToString(inf_s));
        HAL_UART_PutString("\n");
        ONNX_Graph_Cleanup(&g_graph);
        return;
    }

    uint32_t mem_kb = KMEM_ArenaGetUsed(g_tensor_arena) / 1024;
    /* Build model basename (strip directory prefix) */
    const char* bn = model_path; for (const char* p = model_path; *p; p++) if (*p == '/') bn = p + 1;
    /* Strip .onnx extension for the "model" field */
    char model_stem[64]; uint32_t si=0;
    while (bn[si] && bn[si]!='.' && si<63) { model_stem[si]=bn[si]; si++; } model_stem[si]='\0';
    /* Begin JSON */
    JPRINT("{\n  \"model\": \""); JPRINT(model_stem);
    JPRINT("\",\n  \"os\": \"minios\",\n  \"runs\": ");
    { char nb[12]; int nl=0; int rv=runs; if(rv==0){nb[nl++]='0';} else{int t=rv; while(t){nb[nl++]='0'+(t%10); t/=10;} for(int m=0,r=nl-1;m<r;m++,r--){char c=nb[m];nb[m]=nb[r];nb[r]=c;}} nb[nl]='\0'; JPRINT(nb); }
    JPRINT(",\n  \"warmup\": ");
    { char nb[12]; int nl=0; int wv=warmup; if(wv==0){nb[nl++]='0';} else{int t=wv; while(t){nb[nl++]='0'+(t%10); t/=10;} for(int m=0,r=nl-1;m<r;m++,r--){char c=nb[m];nb[m]=nb[r];nb[r]=c;}} nb[nl]='\0'; JPRINT(nb); }
    JPRINT(",\n  \"latencies_ms\": [");

    static uint32_t latencies_us[BENCH_MAX_LATENCIES];
    for (int i = 0; i < runs; i++) {
        uint64_t t0 = HAL_Timer_GetTicks();
        inf_s = ONNX_Runtime_Inference(&ctx, in_ptrs, n_in, out_ptrs, g_graph.num_outputs);
        if (inf_s != STATUS_OK) {
            break;
        }
        if (disable_prepare_after_first_timed) {
            ONNX_Runtime_SetPrepareNodeOutputs(false);
            disable_prepare_after_first_timed = false;
        }
        latencies_us[i] = (uint32_t)HAL_Timer_GetElapsedUs(t0);
    }

    if (bench_irqs_disabled) {
        arch_irq_restore(bench_irq_flags);
        bench_irqs_disabled = false;
    }

    if (bench_tuning_applied) {
        ONNX_Runtime_SetVerbose(prev_runtime_verbose);
        ONNX_Runtime_SetYieldBetweenNodes(prev_runtime_yield);
        ONNX_Runtime_SetNodeProfiling(prev_runtime_node_profiling);
        ONNX_Runtime_SetPrepareNodeOutputs(prev_runtime_prepare_outputs);
        DAEMON_SetTelemetryEnabled(prev_daemon_telemetry);
        SFU_SetTickEnabled(prev_sfu_tick);
        SCHED_SetFelixMode(prev_felix_mode);
    }

    if (debug_single_run) {
        ONNX_Runtime_PrintProfile(&ctx);
    }

    if (inf_s != STATUS_OK) {
        HAL_UART_PutString("run_bench: timed inference failed: ");
        HAL_UART_PutString(STATUS_ToString(inf_s));
        HAL_UART_PutString("\n");
        ONNX_Graph_Cleanup(&g_graph);
        return;
    }

    for (int i = 0; i < runs; i++) {
        uint32_t us = latencies_us[i];
        uint32_t ms = us / 1000;
        uint32_t frac = (us % 1000) / 100;
        char num[16]; int m_l=0; if(ms==0) num[m_l++]='0'; else{uint32_t t=ms; while(t){num[m_l++]='0'+(t%10); t/=10;}}
        for(int m=m_l-1; m>=0; m--) json_buf[json_len++] = num[m];
        json_buf[json_len++] = '.'; json_buf[json_len++] = '0' + frac;
        if (i < runs - 1) JPRINT(", ");
    }

    JPRINT("],\n  \"peak_rss_kb\": ");
    { char num[16]; int l2=0; if(mem_kb==0) num[l2++]='0'; else{uint32_t t=mem_kb; while(t){num[l2++]='0'+(t%10); t/=10;}}
    for(int m=l2-1; m>=0; m--) json_buf[json_len++] = num[m]; }
    JPRINT(",\n  \"model_load_ms\": ");
    { char num[16]; int l1=0; if(load_ms==0) num[l1++]='0'; else{uint64_t t=load_ms; while(t){num[l1++]='0'+(t%10); t/=10;}}
    for(int m=l1-1; m>=0; m--) json_buf[json_len++] = num[m]; }
    JPRINT(".0\n}\n");

    Status out_s = ULFS_Open(out_path, ULFS_O_CREAT | ULFS_O_TRUNC | ULFS_O_WRONLY, &fd);
    if (out_s == STATUS_OK) {
        uint32_t w = 0; ULFS_Write(fd, (uint8_t*)json_buf, json_len, &w);
        ULFS_Close(fd); ULFS_Sync();
        HAL_UART_PutString("run_bench: results written to "); HAL_UART_PutString(out_path); HAL_UART_PutString("\n");
    } else {
        HAL_UART_PutString("run_bench: failed output (status=");
        HAL_UART_PutDec((uint32_t)out_s);
        HAL_UART_PutString(", path=");
        HAL_UART_PutString(out_path);
        HAL_UART_PutString(")\n");
    }

    ONNX_Graph_Cleanup(&g_graph);
}

Status ONNX_RegisterCommands(void)
{
    Status s;

    s = CMD_Register("onnx_info",  "Print ONNX model metadata",              cmd_onnx_info);
    if (s != STATUS_OK) return s;

    s = CMD_Register("onnx_run",   "Run ONNX model inference",               cmd_onnx_run);
    if (s != STATUS_OK) return s;

    s = CMD_Register("onnx_unpack","Write built-in model to file",           cmd_onnx_unpack);
    if (s != STATUS_OK) return s;

    s = CMD_Register("onnx_bench", "Benchmark and compare ONNX models",      cmd_onnx_bench);
    if (s != STATUS_OK) return s;

    s = CMD_Register("/bench/run_bench", "CLI Benchmark Runner",             cmd_run_bench);
    if (s != STATUS_OK) return s;

    return STATUS_OK;
}
