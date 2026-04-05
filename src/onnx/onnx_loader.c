/**
 * @file onnx_loader.c
 * @brief ONNX model loading implementation
 *
 * Implements lightweight model loading without external dependencies.
 */

#include "onnx/onnx_loader.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "kernel/kmem.h"
#include "lib/string.h"
#include "status.h"

#define ONNX_CUSTOM_INVALID_INDEX 0xFFFFFFFFU

typedef struct __attribute__((packed)) {
    char name[ONNX_MAX_NAME_LEN];
    uint32_t dtype;
    uint32_t ndim;
    uint64_t dims[ONNX_MAX_DIMS];
    uint32_t is_initializer;
    uint32_t reserved0;
    uint64_t data_offset;
    uint64_t data_size;
} ONNX_CustomTensorDef;

typedef struct __attribute__((packed)) {
    char name[ONNX_MAX_NAME_LEN];
    uint32_t op_type;
    uint32_t num_inputs;
    uint32_t num_outputs;
    uint32_t input_indices[ONNX_MAX_INPUTS];
    uint32_t output_indices[ONNX_MAX_OUTPUTS];

    uint32_t kernel_shape_len;
    int64_t kernel_shape[ONNX_MAX_ATTR_INTS];
    uint32_t strides_len;
    int64_t strides[ONNX_MAX_ATTR_INTS];
    uint32_t pads_len;
    int64_t pads[ONNX_MAX_ATTR_INTS];
    uint32_t dilations_len;
    int64_t dilations[ONNX_MAX_ATTR_INTS];

    int64_t axis;
    int64_t group;
    float alpha;
    float beta;
    uint32_t fuse_relu;
    int64_t keepdims;
    uint32_t perm_len;
    int64_t perm[ONNX_MAX_ATTR_INTS];
} ONNX_CustomNodeDef;

_Static_assert(sizeof(ONNX_CustomHeader) == 32U, "Unexpected ONNX_CustomHeader size");
_Static_assert(sizeof(ONNX_CustomTensorDef) == 160U, "Unexpected ONNX_CustomTensorDef size");
_Static_assert(sizeof(ONNX_CustomNodeDef) == 900U, "Unexpected ONNX_CustomNodeDef size");

/* Forward declarations used by both protobuf and custom loaders. */
static uint32_t onnx_fuse_conv_batchnorm(ONNX_Graph* graph);
static uint32_t onnx_fuse_conv_relu(ONNX_Graph* graph);

static void onnx_copy_name(char dst[ONNX_MAX_NAME_LEN], const char* src)
{
    uint32_t i = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i < (ONNX_MAX_NAME_LEN - 1U) && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t onnx_attr_len_clamp(uint32_t len)
{
    return (len <= ONNX_MAX_ATTR_INTS) ? len : ONNX_MAX_ATTR_INTS;
}

static bool onnx_tensor_is_in_list(ONNX_Tensor* tensor, ONNX_Tensor** list, uint32_t count)
{
    if (!tensor || !list) return false;

    for (uint32_t i = 0; i < count; i++) {
        if (list[i] == tensor) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Custom Binary Format Loader                                       */
/* ------------------------------------------------------------------ */

Status ONNX_LoadCustomBinary(ONNX_Graph* graph,
                              const uint8_t* data,
                              uint64_t size)
{
    if (!graph || !data || size < sizeof(ONNX_CustomHeader)) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    ONNX_CustomHeader header;
    memcpy(&header, data, sizeof(header));

    /* Validate magic number */
    if (header.magic != ONNX_CUSTOM_MAGIC) {
        HAL_UART_PutString("[ONNX] Error: Invalid magic number\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }

    /* Check version */
    if (header.version != ONNX_CUSTOM_VERSION) {
        HAL_UART_PutString("[ONNX] Error: Unsupported version\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    if (header.num_nodes > ONNX_MAX_NODES ||
        header.num_tensors > ONNX_MAX_TENSORS ||
        header.num_inputs > ONNX_MAX_INPUTS ||
        header.num_outputs > ONNX_MAX_OUTPUTS) {
        HAL_UART_PutString("[ONNX] Error: Custom model exceeds graph limits\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }

    uint64_t tensor_defs_bytes = (uint64_t)header.num_tensors * (uint64_t)sizeof(ONNX_CustomTensorDef);
    uint64_t node_defs_bytes = (uint64_t)header.num_nodes * (uint64_t)sizeof(ONNX_CustomNodeDef);
    uint64_t input_indices_bytes = (uint64_t)header.num_inputs * sizeof(uint32_t);
    uint64_t output_indices_bytes = (uint64_t)header.num_outputs * sizeof(uint32_t);

    uint64_t meta_bytes = sizeof(ONNX_CustomHeader) + tensor_defs_bytes + node_defs_bytes +
                          input_indices_bytes + output_indices_bytes;

    if (meta_bytes > size ||
        header.tensor_data_offset < meta_bytes ||
        header.tensor_data_offset > size) {
        HAL_UART_PutString("[ONNX] Error: Corrupt custom metadata bounds\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }

    HAL_UART_PutString("[ONNX] Loading custom binary format...\n");
    HAL_UART_PutString("  Nodes: ");
    HAL_UART_PutDec(header.num_nodes);
    HAL_UART_PutString("\n");
    HAL_UART_PutString("  Tensors: ");
    HAL_UART_PutDec(header.num_tensors);
    HAL_UART_PutString("\n");

    const uint8_t* cursor = data + sizeof(ONNX_CustomHeader);

    for (uint32_t i = 0; i < header.num_tensors; i++) {
        ONNX_CustomTensorDef tdef;
        memcpy(&tdef, cursor, sizeof(tdef));
        cursor += sizeof(tdef);

        if (tdef.ndim > ONNX_MAX_DIMS) {
            HAL_UART_PutString("[ONNX] Error: Tensor ndim exceeds ONNX_MAX_DIMS\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }

        ONNX_DataType dtype = (ONNX_DataType)tdef.dtype;
        uint32_t dtype_size = ONNX_GetDataTypeSize(dtype);
        if (dtype_size == 0U) {
            dtype = ONNX_DTYPE_FLOAT32;
            dtype_size = ONNX_GetDataTypeSize(dtype);
        }

        ONNX_TensorShape shape;
        memset(&shape, 0, sizeof(shape));

        shape.ndim = tdef.ndim;
        uint64_t total_elements = 1;
        if (shape.ndim == 0) {
            shape.ndim = 1;
            shape.dims[0] = 1;
        } else {
            for (uint32_t d = 0; d < shape.ndim; d++) {
                uint64_t dim = tdef.dims[d];
                if (dim == 0) dim = 1;
                shape.dims[d] = dim;
                total_elements *= dim;
            }
        }
        if (shape.ndim == 1 && shape.dims[0] == 1) {
            total_elements = 1;
        }
        shape.total_elements = total_elements;

        char tensor_name[ONNX_MAX_NAME_LEN];
        onnx_copy_name(tensor_name, tdef.name);
        if (tensor_name[0] == '\0') {
            HAL_UART_PutString("[ONNX] Error: Tensor with empty name in custom binary\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }

        ONNX_Tensor* tensor = ONNX_Graph_CreateTensor(graph, tensor_name, dtype, &shape);
        if (!tensor) {
            return STATUS_ERROR_OUT_OF_MEMORY;
        }

        tensor->is_initializer = (tdef.is_initializer != 0U);
        tensor->data_size = (tdef.data_size != 0U) ? tdef.data_size : (shape.total_elements * dtype_size);

        if (tensor->is_initializer) {
            if (graph->num_initializers >= ONNX_MAX_TENSORS) {
                return STATUS_ERROR_INVALID_GRAPH;
            }
            graph->initializers[graph->num_initializers++] = tensor;
        }

        if (tensor->is_initializer && tdef.data_size > 0U) {
            uint64_t src_begin = header.tensor_data_offset + tdef.data_offset;
            uint64_t src_end = src_begin + tdef.data_size;

            if (src_begin > size || src_end > size || src_end < src_begin) {
                HAL_UART_PutString("[ONNX] Error: Initializer data out of bounds\n");
                return STATUS_ERROR_INVALID_GRAPH;
            }

            if (!graph->tensor_arena) {
                HAL_UART_PutString("[ONNX] Error: tensor arena required for custom initializers\n");
                return STATUS_ERROR_NOT_INITIALIZED;
            }

            tensor->data = KMEM_ArenaAlloc(graph->tensor_arena,
                                           tensor->data_size,
                                           KMEM_TENSOR_ALIGN);
            if (!tensor->data) {
                return STATUS_ERROR_OUT_OF_MEMORY;
            }

            memcpy(tensor->data, data + src_begin, (size_t)tensor->data_size);
            graph->tensor_memory_used = KMEM_ArenaGetUsed(graph->tensor_arena);
        }
    }

    for (uint32_t i = 0; i < header.num_nodes; i++) {
        ONNX_CustomNodeDef ndef;
        memcpy(&ndef, cursor, sizeof(ndef));
        cursor += sizeof(ndef);

        ONNX_OperatorType op_type = (ONNX_OperatorType)ndef.op_type;
        if (op_type >= ONNX_OP_MAX_VALUE) {
            HAL_UART_PutString("[ONNX] Error: Invalid op_type in custom binary\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }

        char node_name[ONNX_MAX_NAME_LEN];
        onnx_copy_name(node_name, ndef.name);
        if (node_name[0] == '\0') {
            HAL_UART_PutString("[ONNX] Error: Node with empty name in custom binary\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }

        ONNX_Node* node = ONNX_Graph_AddNode(graph, node_name, op_type);
        if (!node) {
            return STATUS_ERROR_OUT_OF_MEMORY;
        }

        node->attributes.kernel_shape_len = onnx_attr_len_clamp(ndef.kernel_shape_len);
        for (uint32_t j = 0; j < node->attributes.kernel_shape_len; j++) {
            node->attributes.kernel_shape[j] = ndef.kernel_shape[j];
        }

        node->attributes.strides_len = onnx_attr_len_clamp(ndef.strides_len);
        for (uint32_t j = 0; j < node->attributes.strides_len; j++) {
            node->attributes.strides[j] = ndef.strides[j];
        }

        node->attributes.pads_len = onnx_attr_len_clamp(ndef.pads_len);
        for (uint32_t j = 0; j < node->attributes.pads_len; j++) {
            node->attributes.pads[j] = ndef.pads[j];
        }

        node->attributes.dilations_len = onnx_attr_len_clamp(ndef.dilations_len);
        for (uint32_t j = 0; j < node->attributes.dilations_len; j++) {
            node->attributes.dilations[j] = ndef.dilations[j];
        }

        node->attributes.perm_len = onnx_attr_len_clamp(ndef.perm_len);
        for (uint32_t j = 0; j < node->attributes.perm_len; j++) {
            node->attributes.perm[j] = ndef.perm[j];
        }

        node->attributes.axis = ndef.axis;
        node->attributes.group = ndef.group;
        node->attributes.alpha = ndef.alpha;
        node->attributes.beta = ndef.beta;
        node->attributes.fuse_relu = (ndef.fuse_relu != 0U);
        node->attributes.keepdims = ndef.keepdims;

        uint32_t num_inputs = ndef.num_inputs;
        if (num_inputs > ONNX_MAX_INPUTS) num_inputs = ONNX_MAX_INPUTS;
        for (uint32_t j = 0; j < num_inputs; j++) {
            uint32_t tidx = ndef.input_indices[j];
            if (tidx == ONNX_CUSTOM_INVALID_INDEX) continue;
            if (tidx >= graph->num_tensors) {
                HAL_UART_PutString("[ONNX] Error: Node input index out of range\n");
                return STATUS_ERROR_INVALID_GRAPH;
            }
            Status s = ONNX_Node_AddInput(node, &graph->tensors[tidx]);
            if (s != STATUS_OK) {
                return s;
            }
        }

        uint32_t num_outputs = ndef.num_outputs;
        if (num_outputs > ONNX_MAX_OUTPUTS) num_outputs = ONNX_MAX_OUTPUTS;
        for (uint32_t j = 0; j < num_outputs; j++) {
            uint32_t tidx = ndef.output_indices[j];
            if (tidx == ONNX_CUSTOM_INVALID_INDEX) continue;
            if (tidx >= graph->num_tensors) {
                HAL_UART_PutString("[ONNX] Error: Node output index out of range\n");
                return STATUS_ERROR_INVALID_GRAPH;
            }
            Status s = ONNX_Node_AddOutput(node, &graph->tensors[tidx]);
            if (s != STATUS_OK) {
                return s;
            }
        }
    }

    for (uint32_t i = 0; i < header.num_inputs; i++) {
        uint32_t tidx;
        memcpy(&tidx, cursor, sizeof(tidx));
        cursor += sizeof(tidx);

        if (tidx == ONNX_CUSTOM_INVALID_INDEX) continue;
        if (tidx >= graph->num_tensors) {
            HAL_UART_PutString("[ONNX] Error: Graph input index out of range\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }

        ONNX_Tensor* t = &graph->tensors[tidx];
        if (t->is_initializer) {
            continue;
        }

        if (!onnx_tensor_is_in_list(t, graph->inputs, graph->num_inputs) &&
            graph->num_inputs < ONNX_MAX_INPUTS) {
            graph->inputs[graph->num_inputs++] = t;
        }
    }

    for (uint32_t i = 0; i < header.num_outputs; i++) {
        uint32_t tidx;
        memcpy(&tidx, cursor, sizeof(tidx));
        cursor += sizeof(tidx);

        if (tidx == ONNX_CUSTOM_INVALID_INDEX) continue;
        if (tidx >= graph->num_tensors) {
            HAL_UART_PutString("[ONNX] Error: Graph output index out of range\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }

        ONNX_Tensor* t = &graph->tensors[tidx];
        if (!onnx_tensor_is_in_list(t, graph->outputs, graph->num_outputs) &&
            graph->num_outputs < ONNX_MAX_OUTPUTS) {
            graph->outputs[graph->num_outputs++] = t;
        }
    }

    uint32_t fused_bn = onnx_fuse_conv_batchnorm(graph);
    if (fused_bn > 0) {
        HAL_UART_PutString("[ONNX] Graph optimization: fused Conv+BatchNorm pairs = ");
        HAL_UART_PutDec(fused_bn);
        HAL_UART_PutString("\n");
    }

    uint32_t fused_relu = onnx_fuse_conv_relu(graph);
    if (fused_relu > 0) {
        HAL_UART_PutString("[ONNX] Graph optimization: fused Conv+ReLU pairs = ");
        HAL_UART_PutDec(fused_relu);
        HAL_UART_PutString("\n");
    }

    Status dep_status = ONNX_Graph_BuildDependencies(graph);
    if (dep_status != STATUS_OK) {
        HAL_UART_PutString("[ONNX] Error: Failed to build dependencies\n");
        return dep_status;
    }

    Status sched_status = ONNX_Graph_GenerateSchedule(graph);
    if (sched_status != STATUS_OK) {
        HAL_UART_PutString("[ONNX] Error: Failed to generate schedule\n");
        return sched_status;
    }

    HAL_UART_PutString("[ONNX] ✓ Custom model loaded successfully!\n");
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Protobuf Format Loader (Minimal Parser)                           */
/* ------------------------------------------------------------------ */

/* Protobuf wire types */
typedef enum {
    WIRE_VARINT = 0,
    WIRE_FIXED64 = 1,
    WIRE_LENGTH_DELIMITED = 2,
    WIRE_START_GROUP = 3,
    WIRE_END_GROUP = 4,
    WIRE_FIXED32 = 5,
} ProtoWireType;

typedef struct {
    const uint8_t* data;
    uint64_t size;
    uint64_t pos;
} ProtoReader;

static float onnx_fast_rsqrt(float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }

    union {
        float f;
        uint32_t i;
    } conv;

    conv.f = x;
    conv.i = 0x5f3759dfU - (conv.i >> 1);
    float y = conv.f;

    y = y * (1.5f - (0.5f * x * y * y));
    return y;
}

static uint32_t onnx_count_tensor_uses(const ONNX_Graph* graph, const ONNX_Tensor* tensor)
{
    if (!graph || !tensor) return 0;

    uint32_t uses = 0;
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        const ONNX_Node* node = &graph->nodes[i];
        for (uint32_t j = 0; j < node->num_inputs; j++) {
            if (node->inputs[j] == tensor) {
                uses++;
            }
        }
    }

    return uses;
}

/* Fold Conv->BatchNorm into Conv weights/bias and remove BatchNorm node.
 * This mirrors common ORT graph fusion and reduces one full tensor pass. */
static uint32_t onnx_fuse_conv_batchnorm(ONNX_Graph* graph)
{
    if (!graph) return 0;

    const float epsilon = 1e-5f;
    uint32_t fused = 0;

    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        ONNX_Node* conv = &graph->nodes[i];
        if (conv->op_type != ONNX_OP_CONV) {
            continue;
        }

        if (conv->num_inputs < 2 || conv->num_outputs != 1) {
            continue;
        }

        ONNX_Tensor* conv_out = conv->outputs[0];
        ONNX_Tensor* w = conv->inputs[1];
        if (!conv_out || !w || w->dtype != ONNX_DTYPE_FLOAT32 || !w->data) {
            continue;
        }

        /* Do not fuse if this intermediate is already a graph output. */
        bool is_graph_output = false;
        for (uint32_t o = 0; o < graph->num_outputs; o++) {
            if (graph->outputs[o] == conv_out) {
                is_graph_output = true;
                break;
            }
        }
        if (is_graph_output) {
            continue;
        }

        /* Find unique consumer of Conv output. */
        uint32_t consumers = 0;
        uint32_t bn_idx = ONNX_MAX_NODES;
        for (uint32_t n = 0; n < graph->num_nodes; n++) {
            ONNX_Node* node = &graph->nodes[n];
            for (uint32_t in = 0; in < node->num_inputs; in++) {
                if (node->inputs[in] == conv_out) {
                    consumers++;
                    if (consumers == 1) {
                        bn_idx = n;
                    }
                }
            }
        }

        if (consumers != 1 || bn_idx == ONNX_MAX_NODES) {
            continue;
        }

        ONNX_Node* bn = &graph->nodes[bn_idx];
        if (bn->op_type != ONNX_OP_BATCHNORM || bn->num_inputs < 5 || bn->num_outputs != 1) {
            continue;
        }
        if (bn->inputs[0] != conv_out) {
            continue;
        }

        ONNX_Tensor* scale = bn->inputs[1];
        ONNX_Tensor* bn_b = bn->inputs[2];
        ONNX_Tensor* mean = bn->inputs[3];
        ONNX_Tensor* var = bn->inputs[4];
        ONNX_Tensor* bn_out = bn->outputs[0];

        if (!scale || !bn_b || !mean || !var || !bn_out) {
            continue;
        }
        if (scale->dtype != ONNX_DTYPE_FLOAT32 || bn_b->dtype != ONNX_DTYPE_FLOAT32 ||
            mean->dtype != ONNX_DTYPE_FLOAT32 || var->dtype != ONNX_DTYPE_FLOAT32) {
            continue;
        }
        if (!scale->data || !bn_b->data || !mean->data || !var->data) {
            continue;
        }

        uint64_t c_out = w->shape.dims[0];
        if (c_out == 0) {
            continue;
        }

        uint64_t w_elems = w->data_size / sizeof(float);
        if ((w_elems % c_out) != 0) {
            continue;
        }
        uint64_t w_per_oc = w_elems / c_out;

        if (scale->shape.total_elements < c_out ||
            bn_b->shape.total_elements < c_out ||
            mean->shape.total_elements < c_out ||
            var->shape.total_elements < c_out) {
            continue;
        }

        ONNX_Tensor* conv_b = (conv->num_inputs > 2) ? conv->inputs[2] : NULL;
        if (conv_b) {
            if (conv_b->dtype != ONNX_DTYPE_FLOAT32 || !conv_b->data || conv_b->shape.total_elements < c_out) {
                continue;
            }
        } else {
            /* Reuse BatchNorm B as Conv bias only when it is not shared. */
            if (onnx_count_tensor_uses(graph, bn_b) != 1) {
                continue;
            }
            if (conv->num_inputs >= ONNX_MAX_INPUTS) {
                continue;
            }
            conv_b = bn_b;
            conv->inputs[conv->num_inputs++] = conv_b;
        }

        float* w_data = (float*)w->data;
        float* scale_data = (float*)scale->data;
        float* bn_b_data = (float*)bn_b->data;
        float* mean_data = (float*)mean->data;
        float* var_data = (float*)var->data;
        float* conv_b_data = (float*)conv_b->data;

        for (uint64_t oc = 0; oc < c_out; oc++) {
            float mul = scale_data[oc] * onnx_fast_rsqrt(var_data[oc] + epsilon);
            uint64_t w_base = oc * w_per_oc;

            for (uint64_t wi = 0; wi < w_per_oc; wi++) {
                w_data[w_base + wi] *= mul;
            }

            if (conv_b == bn_b) {
                conv_b_data[oc] = bn_b_data[oc] - (mean_data[oc] * mul);
            } else {
                conv_b_data[oc] = (conv_b_data[oc] - mean_data[oc]) * mul + bn_b_data[oc];
            }
        }

        /* Conv now directly produces BatchNorm output tensor. */
        conv->outputs[0] = bn_out;

        /* Remove fused BatchNorm node by compacting node array. */
        for (uint32_t m = bn_idx; (m + 1) < graph->num_nodes; m++) {
            graph->nodes[m] = graph->nodes[m + 1];
        }
        graph->num_nodes--;
        fused++;
    }

    return fused;
}

/* Fold Conv->ReLU by tagging Conv to apply activation inline and removing ReLU node. */
static uint32_t onnx_fuse_conv_relu(ONNX_Graph* graph)
{
    if (!graph) return 0;

    uint32_t fused = 0;

    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        ONNX_Node* conv = &graph->nodes[i];
        if (conv->op_type != ONNX_OP_CONV || conv->num_outputs != 1) {
            continue;
        }

        ONNX_Tensor* conv_out = conv->outputs[0];
        if (!conv_out) {
            continue;
        }

        /* Do not fuse if this intermediate is already a graph output. */
        bool is_graph_output = false;
        for (uint32_t o = 0; o < graph->num_outputs; o++) {
            if (graph->outputs[o] == conv_out) {
                is_graph_output = true;
                break;
            }
        }
        if (is_graph_output) {
            continue;
        }

        uint32_t consumers = 0;
        uint32_t relu_idx = ONNX_MAX_NODES;
        for (uint32_t n = 0; n < graph->num_nodes; n++) {
            ONNX_Node* node = &graph->nodes[n];
            for (uint32_t in = 0; in < node->num_inputs; in++) {
                if (node->inputs[in] == conv_out) {
                    consumers++;
                    if (consumers == 1) {
                        relu_idx = n;
                    }
                }
            }
        }

        if (consumers != 1 || relu_idx == ONNX_MAX_NODES) {
            continue;
        }

        ONNX_Node* relu = &graph->nodes[relu_idx];
        if (relu->op_type != ONNX_OP_RELU || relu->num_inputs != 1 || relu->num_outputs != 1) {
            continue;
        }
        if (relu->inputs[0] != conv_out || !relu->outputs[0]) {
            continue;
        }

        conv->attributes.fuse_relu = true;
        conv->outputs[0] = relu->outputs[0];

        /* Remove fused ReLU node by compacting node array. */
        for (uint32_t m = relu_idx; (m + 1) < graph->num_nodes; m++) {
            graph->nodes[m] = graph->nodes[m + 1];
        }
        graph->num_nodes--;
        fused++;
    }

    return fused;
}

static uint64_t proto_read_varint(ProtoReader* reader)
{
    uint64_t result = 0;
    uint32_t shift = 0;
    
    while (reader->pos < reader->size) {
        uint8_t byte = reader->data[reader->pos++];
        result |= ((uint64_t)(byte & 0x7F)) << shift;
        
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
    }
    
    return result;
}

static uint32_t proto_read_tag(ProtoReader* reader, ProtoWireType* wire_type)
{
    uint64_t tag = proto_read_varint(reader);
    *wire_type = (ProtoWireType)(tag & 0x7);
    return (uint32_t)(tag >> 3);
}

/* Read string (length-prefixed) */
static Status proto_read_string(ProtoReader* reader, char* buffer, uint32_t max_len)
{
    uint64_t len = proto_read_varint(reader);
    
    if (reader->pos + len > reader->size) {
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    uint32_t copy_len = (len < max_len - 1) ? (uint32_t)len : (max_len - 1);
    
    uint32_t i;
    for (i = 0; i < copy_len; i++) {
        buffer[i] = reader->data[reader->pos + i];
    }
    buffer[copy_len] = '\0';
    
    reader->pos += len;
    return STATUS_OK;
}

/* Read bytes (length-prefixed) - returns start position and length */
static Status proto_read_bytes(ProtoReader* reader, const uint8_t** out_data, uint64_t* out_len)
{
    uint64_t len = proto_read_varint(reader);
    
    if (reader->pos + len > reader->size) {
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    *out_data = &reader->data[reader->pos];
    *out_len = len;
    reader->pos += len;
    
    return STATUS_OK;
}

/* Skip a field based on wire type */
static void proto_skip_field(ProtoReader* reader, ProtoWireType wire_type)
{
    if (wire_type == WIRE_VARINT) {
        proto_read_varint(reader);
    } else if (wire_type == WIRE_LENGTH_DELIMITED) {
        uint64_t len = proto_read_varint(reader);
        reader->pos += len;
    } else if (wire_type == WIRE_FIXED32) {
        reader->pos += 4;
    } else if (wire_type == WIRE_FIXED64) {
        reader->pos += 8;
    }
}

/* Parse AttributeProto */
static void proto_parse_attribute(ProtoReader* reader, uint64_t attr_msg_len, ONNX_Attributes* attrs)
{
    uint64_t end_pos = reader->pos + attr_msg_len;
    char name[ONNX_MAX_NAME_LEN] = {0};

    while (reader->pos < end_pos && reader->pos < reader->size) {
        ProtoWireType wire_type;
        uint32_t field = proto_read_tag(reader, &wire_type);

        if (field == 1) {  /* name */
            proto_read_string(reader, name, sizeof(name));
        }
        else if (field == 3) {  /* i (int) */
            uint64_t val = proto_read_varint(reader);
            if (name[0] == 'a' && name[1] == 'x' && name[2] == 'i' && name[3] == 's' && name[4] == '\0') {
                attrs->axis = (int64_t)val;
            } else if (name[0] == 'k' && name[1] == 'e' && name[2] == 'e' && name[3] == 'p' &&
                       name[4] == 'd' && name[5] == 'i' && name[6] == 'm' && name[7] == 's' && name[8] == '\0') {
                attrs->keepdims = (int64_t)val;
            } else if (name[0] == 'g' && name[1] == 'r' && name[2] == 'o' && name[3] == 'u' && name[4] == 'p' && name[5] == '\0') {
                attrs->group = (int64_t)val;
            }
        }
        else if (field == 4) {  /* f (float) */
            uint32_t val_bits = 0;
            if (reader->pos + 4 <= reader->size) {
                val_bits = ((uint32_t)reader->data[reader->pos]) |
                           (((uint32_t)reader->data[reader->pos+1]) << 8) |
                           (((uint32_t)reader->data[reader->pos+2]) << 16) |
                           (((uint32_t)reader->data[reader->pos+3]) << 24);
                reader->pos += 4;
            }
            float val = 0.0f;
            memcpy(&val, &val_bits, sizeof(float));

            if (name[0] == 'a' && name[1] == 'l' && name[2] == 'p' && name[3] == 'h' && name[4] == 'a' && name[5] == '\0') {
                attrs->alpha = val;
            } else if (name[0] == 'b' && name[1] == 'e' && name[2] == 't' && name[3] == 'a' && name[4] == '\0') {
                attrs->beta = val;
            }
        }
        else if (field == 5 && wire_type == WIRE_LENGTH_DELIMITED) {  /* t (TensorProto) */
            uint64_t t_len = proto_read_varint(reader);
            uint64_t t_end = reader->pos + t_len;
            bool is_value = (name[0] == 'v' && name[1] == 'a' && name[2] == 'l' && name[3] == 'u' && name[4] == 'e' && name[5] == '\0');

            if (is_value) {
                ONNX_DataType t_dtype = ONNX_DTYPE_UNDEFINED;
                int64_t t_ints[ONNX_MAX_ATTR_INTS] = {0};
                uint32_t t_ints_len = 0;
                const uint8_t* raw = NULL;
                uint64_t raw_len = 0;

                while (reader->pos < t_end && reader->pos < reader->size) {
                    ProtoWireType tw;
                    uint32_t tf = proto_read_tag(reader, &tw);
                    if (tf == 2 && tw == WIRE_VARINT) { /* data_type */
                        t_dtype = (ONNX_DataType)proto_read_varint(reader);
                    } else if (tf == 7) { /* int64_data */
                        if (tw == WIRE_LENGTH_DELIMITED) {
                            uint64_t len = proto_read_varint(reader);
                            uint64_t pend = reader->pos + len;
                            while (reader->pos < pend && reader->pos < reader->size) {
                                if (t_ints_len < ONNX_MAX_ATTR_INTS) {
                                    t_ints[t_ints_len++] = (int64_t)proto_read_varint(reader);
                                } else {
                                    (void)proto_read_varint(reader);
                                }
                            }
                        } else if (tw == WIRE_VARINT) {
                            if (t_ints_len < ONNX_MAX_ATTR_INTS) {
                                t_ints[t_ints_len++] = (int64_t)proto_read_varint(reader);
                            } else {
                                (void)proto_read_varint(reader);
                            }
                        } else {
                            proto_skip_field(reader, tw);
                        }
                    } else if (tf == 9 && tw == WIRE_LENGTH_DELIMITED) { /* raw_data */
                        proto_read_bytes(reader, &raw, &raw_len);
                    } else {
                        proto_skip_field(reader, tw);
                    }
                }
                reader->pos = t_end;

                if (t_ints_len == 0 && t_dtype == ONNX_DTYPE_INT64 && raw && raw_len >= 8) {
                    uint32_t n = (uint32_t)(raw_len / 8);
                    if (n > ONNX_MAX_ATTR_INTS) n = ONNX_MAX_ATTR_INTS;
                    for (uint32_t i = 0; i < n; i++) {
                        uint64_t u = 0;
                        for (uint32_t b = 0; b < 8; b++) {
                            u |= ((uint64_t)raw[i * 8 + b]) << (8 * b);
                        }
                        t_ints[t_ints_len++] = (int64_t)u;
                    }
                }

                attrs->group = (int64_t)t_dtype;
                attrs->kernel_shape_len = t_ints_len;
                for (uint32_t i = 0; i < t_ints_len; i++) {
                    attrs->kernel_shape[i] = t_ints[i];
                }
            } else {
                reader->pos = t_end;
            }
        }
        else if (field == 7 || field == 8) {  /* ints (repeated int64; support both field ids) */
            if (wire_type == WIRE_LENGTH_DELIMITED) {
                uint64_t len = proto_read_varint(reader);
                uint64_t end_packed = reader->pos + len;

                bool is_kernel = (name[0] == 'k' && name[1] == 'e' && name[2] == 'r' && name[3] == 'n' && name[4] == 'e' && name[5] == 'l');
                bool is_strides = (name[0] == 's' && name[1] == 't' && name[2] == 'r' && name[3] == 'i' && name[4] == 'd' && name[5] == 'e' && name[6] == 's');
                bool is_pads = (name[0] == 'p' && name[1] == 'a' && name[2] == 'd' && name[3] == 's');
                bool is_dilations = (name[0] == 'd' && name[1] == 'i' && name[2] == 'l' && name[3] == 'a' && name[4] == 't' && name[5] == 'i');
                bool is_perm = (name[0] == 'p' && name[1] == 'e' && name[2] == 'r' && name[3] == 'm');
                bool is_axes = (name[0] == 'a' && name[1] == 'x' && name[2] == 'e' && name[3] == 's' && name[4] == '\0');
                bool is_split = (name[0] == 's' && name[1] == 'p' && name[2] == 'l' && name[3] == 'i' && name[4] == 't' && name[5] == '\0');

                while (reader->pos < end_packed && reader->pos < reader->size) {
                    uint64_t val = proto_read_varint(reader);
                    if (is_kernel && attrs->kernel_shape_len < ONNX_MAX_ATTR_INTS) {
                        attrs->kernel_shape[attrs->kernel_shape_len++] = (int64_t)val;
                    } else if (is_strides && attrs->strides_len < ONNX_MAX_ATTR_INTS) {
                        attrs->strides[attrs->strides_len++] = (int64_t)val;
                    } else if (is_pads && attrs->pads_len < ONNX_MAX_ATTR_INTS) {
                        attrs->pads[attrs->pads_len++] = (int64_t)val;
                    } else if (is_dilations && attrs->dilations_len < ONNX_MAX_ATTR_INTS) {
                        attrs->dilations[attrs->dilations_len++] = (int64_t)val;
                    } else if (is_perm && attrs->perm_len < ONNX_MAX_ATTR_INTS) {
                        attrs->perm[attrs->perm_len++] = (int64_t)val;
                    } else if (is_axes && attrs->perm_len < ONNX_MAX_ATTR_INTS) {
                        attrs->perm[attrs->perm_len++] = (int64_t)val;
                    } else if (is_split && attrs->kernel_shape_len < ONNX_MAX_ATTR_INTS) {
                        attrs->kernel_shape[attrs->kernel_shape_len++] = (int64_t)val;
                    }
                }
            } else if (wire_type == WIRE_VARINT) {
                uint64_t val = proto_read_varint(reader);
                bool is_kernel = (name[0] == 'k' && name[1] == 'e' && name[2] == 'r' && name[3] == 'n' && name[4] == 'e' && name[5] == 'l');
                bool is_strides = (name[0] == 's' && name[1] == 't' && name[2] == 'r' && name[3] == 'i' && name[4] == 'd' && name[5] == 'e' && name[6] == 's');
                bool is_pads = (name[0] == 'p' && name[1] == 'a' && name[2] == 'd' && name[3] == 's');
                bool is_dilations = (name[0] == 'd' && name[1] == 'i' && name[2] == 'l' && name[3] == 'a' && name[4] == 't' && name[5] == 'i');
                bool is_perm = (name[0] == 'p' && name[1] == 'e' && name[2] == 'r' && name[3] == 'm');
                bool is_axes = (name[0] == 'a' && name[1] == 'x' && name[2] == 'e' && name[3] == 's' && name[4] == '\0');
                bool is_split = (name[0] == 's' && name[1] == 'p' && name[2] == 'l' && name[3] == 'i' && name[4] == 't' && name[5] == '\0');

                if (is_kernel && attrs->kernel_shape_len < ONNX_MAX_ATTR_INTS) {
                    attrs->kernel_shape[attrs->kernel_shape_len++] = (int64_t)val;
                } else if (is_strides && attrs->strides_len < ONNX_MAX_ATTR_INTS) {
                    attrs->strides[attrs->strides_len++] = (int64_t)val;
                } else if (is_pads && attrs->pads_len < ONNX_MAX_ATTR_INTS) {
                    attrs->pads[attrs->pads_len++] = (int64_t)val;
                } else if (is_dilations && attrs->dilations_len < ONNX_MAX_ATTR_INTS) {
                    attrs->dilations[attrs->dilations_len++] = (int64_t)val;
                } else if (is_perm && attrs->perm_len < ONNX_MAX_ATTR_INTS) {
                    attrs->perm[attrs->perm_len++] = (int64_t)val;
                } else if (is_axes && attrs->perm_len < ONNX_MAX_ATTR_INTS) {
                    attrs->perm[attrs->perm_len++] = (int64_t)val;
                } else if (is_split && attrs->kernel_shape_len < ONNX_MAX_ATTR_INTS) {
                    attrs->kernel_shape[attrs->kernel_shape_len++] = (int64_t)val;
                }
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        else if (field == 6) {  /* floats */
            if (wire_type == WIRE_LENGTH_DELIMITED) {
                uint64_t len = proto_read_varint(reader);
                reader->pos += len;
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        else {
            proto_skip_field(reader, wire_type);
        }
    }
}

/* Parse TensorProto and create ONNX_Tensor
 * Bug fixes applied:
 *   Bug 1 – field 8 is TensorProto.name (not field 3 = segment)
 *   Bug 2 – loop bounded by end_pos, not reader->size
 *   Bug 3 – raw_data memcpy'd into arena immediately; never store a .rodata ptr
 */
static Status proto_parse_tensor(ProtoReader* reader,
                                  uint64_t      end_pos,
                                  ONNX_Graph*   graph,
                                  ONNX_Tensor** out_tensor)
{
    char name[ONNX_MAX_NAME_LEN] = {0};
    ONNX_DataType dtype = ONNX_DTYPE_FLOAT32;
    uint64_t dims[ONNX_MAX_DIMS] = {0};
    uint32_t ndim = 0;
    const uint8_t* raw_data = NULL;
    uint64_t raw_data_len = 0;
    /* float_data (field 4): Python onnx library stores floats here instead of raw_data */
    const uint8_t* float_data = NULL;
    uint64_t float_data_len = 0;
    /* int64_data (field 7): used by many shape tensors (e.g., Reshape shape input) */
    static int64_t int64_data_buf[1024];
    uint32_t int64_data_count = 0;
    bool int64_data_overflow = false;

    /* BUG 2 FIX: bound the loop to the message end, not the whole buffer */
    while (reader->pos < end_pos && reader->pos < reader->size) {
        ProtoWireType wire_type;
        uint32_t field = proto_read_tag(reader, &wire_type);

        if (field == 0) break;

        if (field == 1) {        /* dims – repeated int64 (VARINT or packed) */
            if (wire_type == WIRE_LENGTH_DELIMITED) {
                uint64_t len = proto_read_varint(reader);
                uint64_t end_packed = reader->pos + len;
                while (reader->pos < end_packed && reader->pos < reader->size) {
                    uint64_t val = proto_read_varint(reader);
                    if (ndim < ONNX_MAX_DIMS) {
                        dims[ndim++] = val;
                    }
                }
            } else if (wire_type == WIRE_VARINT) {
                uint64_t val = proto_read_varint(reader);
                if (ndim < ONNX_MAX_DIMS) {
                    dims[ndim++] = val;
                }
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        else if (field == 2) {   /* data_type – int32 */
            dtype = (ONNX_DataType)proto_read_varint(reader);
        }
        else if (field == 4) {   /* float_data – packed repeated float32 */
            /* Python's onnx library emits initializers as float_data (field 4)
             * rather than raw_data (field 9). The bytes are identical on little-
             * endian hosts, so we just record the slice and copy it later. */
            proto_read_bytes(reader, &float_data, &float_data_len);
        }
        else if (field == 7) {   /* int64_data – repeated int64 (packed or unpacked) */
            if (wire_type == WIRE_LENGTH_DELIMITED) {
                uint64_t len = proto_read_varint(reader);
                uint64_t end_packed = reader->pos + len;
                while (reader->pos < end_packed && reader->pos < reader->size) {
                    uint64_t v = proto_read_varint(reader);
                    if (int64_data_count < 1024) {
                        int64_data_buf[int64_data_count++] = (int64_t)v;
                    } else {
                        int64_data_overflow = true;
                    }
                }
            } else if (wire_type == WIRE_VARINT) {
                uint64_t v = proto_read_varint(reader);
                if (int64_data_count < 1024) {
                    int64_data_buf[int64_data_count++] = (int64_t)v;
                } else {
                    int64_data_overflow = true;
                }
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        /* BUG 1 FIX: TensorProto.name is field 8, not field 3 */
        else if (field == 8) {   /* name – string */
            proto_read_string(reader, name, sizeof(name));
        }
        else if (field == 9) {   /* raw_data – bytes */
            proto_read_bytes(reader, &raw_data, &raw_data_len);
            break; /* raw_data is always last in a TensorProto */
        }
        else {
            proto_skip_field(reader, wire_type);
        }
    }

    if (int64_data_overflow) {
        HAL_UART_PutString("[ONNX] Error: int64_data too large for parser buffer\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    /* Build shape */
    ONNX_TensorShape shape;
    shape.ndim = ndim;
    uint64_t total = (ndim > 0) ? 1 : 0;
    uint32_t i;
    for (i = 0; i < ndim; i++) {
        shape.dims[i] = dims[i];
        total *= dims[i];
    }
    for (; i < ONNX_MAX_DIMS; i++) {
        shape.dims[i] = 0;
    }
    shape.total_elements = total;

    /* Get or create tensor slot in graph */
    ONNX_Tensor* tensor = ONNX_Graph_FindTensor(graph, name);
    if (tensor) {
        /* Update existing placeholder */
        tensor->dtype = dtype;
        tensor->shape = shape;
        tensor->data_size = shape.total_elements * ONNX_GetDataTypeSize(dtype);
    } else {
        tensor = ONNX_Graph_CreateTensor(graph, name, dtype, &shape);
        if (!tensor) {
            return STATUS_ERROR_OUT_OF_MEMORY;
        }
    }
    tensor->is_initializer = 1;

    /* BUG 3 FIX: allocate arena memory and memcpy immediately.
     * Never leave tensor->data pointing into the read-only protobuf buffer;
     * MMU write-protects .rodata before inference runs.
     * Also handle float_data (field 4) the same way – Python's onnx library
     * uses field 4 instead of field 9 for most float initializers. */
    {
        const uint8_t* src = raw_data ? raw_data : float_data;
        uint64_t  src_len = raw_data ? raw_data_len : float_data_len;

        if (src && src_len > 0 && graph->tensor_arena) {
            tensor->data = KMEM_ArenaAlloc(graph->tensor_arena,
                                           src_len,
                                           KMEM_TENSOR_ALIGN);
            if (!tensor->data) {
                return STATUS_ERROR_OUT_OF_MEMORY;
            }
            memcpy(tensor->data, src, (size_t)src_len);
            tensor->data_size = src_len;
            graph->tensor_memory_used = KMEM_ArenaGetUsed(graph->tensor_arena);
        } else if (dtype == ONNX_DTYPE_INT64 && int64_data_count > 0 && graph->tensor_arena) {
            uint64_t int64_bytes = (uint64_t)int64_data_count * sizeof(int64_t);
            tensor->data = KMEM_ArenaAlloc(graph->tensor_arena,
                                           int64_bytes,
                                           KMEM_TENSOR_ALIGN);
            if (!tensor->data) {
                return STATUS_ERROR_OUT_OF_MEMORY;
            }
            memcpy(tensor->data, int64_data_buf, (size_t)int64_bytes);
            tensor->data_size = int64_bytes;
            graph->tensor_memory_used = KMEM_ArenaGetUsed(graph->tensor_arena);
        }
    }

    *out_tensor = tensor;
    return STATUS_OK;
}

/* Parse NodeProto */
static Status proto_parse_node(ProtoReader* reader, uint64_t node_msg_len, ONNX_Graph* graph)
{
    char name[ONNX_MAX_NAME_LEN] = {0};
    char op_type[ONNX_MAX_NAME_LEN] = {0};
    char inputs[ONNX_MAX_INPUTS][ONNX_MAX_NAME_LEN];
    char outputs[ONNX_MAX_OUTPUTS][ONNX_MAX_NAME_LEN];
    uint32_t num_inputs = 0;
    uint32_t num_outputs = 0;
    ONNX_Attributes attrs = {0};

    uint64_t end_pos = reader->pos + node_msg_len;

    /* Initialize input/output arrays */
    uint32_t i;
    for (i = 0; i < ONNX_MAX_INPUTS; i++) {
        inputs[i][0] = '\0';
    }
    for (i = 0; i < ONNX_MAX_OUTPUTS; i++) {
        outputs[i][0] = '\0';
    }

    while (reader->pos < end_pos && reader->pos < reader->size) {
        ProtoWireType wire_type;
        uint32_t field = proto_read_tag(reader, &wire_type);

        if (field == 1) {  /* input - repeated string */
            if (num_inputs < ONNX_MAX_INPUTS) {
                proto_read_string(reader, inputs[num_inputs], ONNX_MAX_NAME_LEN);
                num_inputs++;
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        else if (field == 2) {  /* output - repeated string */
            if (num_outputs < ONNX_MAX_OUTPUTS) {
                proto_read_string(reader, outputs[num_outputs], ONNX_MAX_NAME_LEN);
                num_outputs++;
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        else if (field == 3) {  /* name - string */
            proto_read_string(reader, name, sizeof(name));
        }
        else if (field == 4) {  /* op_type - string */
            proto_read_string(reader, op_type, sizeof(op_type));
        }
        else if (field == 5) {  /* attribute - repeated AttributeProto */
            uint64_t attr_len = proto_read_varint(reader);
            proto_parse_attribute(reader, attr_len, &attrs);
        }
        else {
            proto_skip_field(reader, wire_type);
        }
    }
    
    /* Map op_type string to enum */
    ONNX_OperatorType op_enum = ONNX_OP_ADD;  /* Default */

    /* A proper implementation would use strcmp or hash table */
    if (op_type[0] != '\0') {
        if (op_type[0] == 'A' && op_type[1] == 'd' && op_type[2] == 'd' && op_type[3] == '\0') {
            op_enum = ONNX_OP_ADD;
        } else if (op_type[0] == 'S' && op_type[1] == 'u' && op_type[2] == 'b' && op_type[3] == '\0') {
            op_enum = ONNX_OP_SUB;
        } else if (op_type[0] == 'M' && op_type[1] == 'u' && op_type[2] == 'l' && op_type[3] == '\0') {
            op_enum = ONNX_OP_MUL;
        } else if (op_type[0] == 'D' && op_type[1] == 'i' && op_type[2] == 'v' && op_type[3] == '\0') {
            op_enum = ONNX_OP_DIV;
        } else if (op_type[0] == 'M' && op_type[1] == 'a' && op_type[2] == 't' &&
                 op_type[3] == 'M' && op_type[4] == 'u' && op_type[5] == 'l' && op_type[6] == '\0') {
            op_enum = ONNX_OP_MATMUL;
        } else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 'l' &&
                 op_type[3] == 'u' && op_type[4] == '\0') {
            op_enum = ONNX_OP_RELU;
        } else if (op_type[0] == 'S' && op_type[1] == 'i' && op_type[2] == 'g' && op_type[3] == 'm') {
            op_enum = ONNX_OP_SIGMOID;
        } else if (op_type[0] == 'T' && op_type[1] == 'a' && op_type[2] == 'n' && op_type[3] == 'h') {
            op_enum = ONNX_OP_TANH;
        } else if (op_type[0] == 'S' && op_type[1] == 'o' && op_type[2] == 'f' && op_type[3] == 't') {
            op_enum = ONNX_OP_SOFTMAX;
        } else if (op_type[0] == 'C' && op_type[1] == 'o' && op_type[2] == 'n' && op_type[3] == 'v') {
            op_enum = ONNX_OP_CONV;
        } else if (op_type[0] == 'M' && op_type[1] == 'a' && op_type[2] == 'x' && op_type[3] == 'P') {
            op_enum = ONNX_OP_MAXPOOL;
        } else if (op_type[0] == 'A' && op_type[1] == 'v' && op_type[2] == 'e' && op_type[3] == 'r') {
            op_enum = ONNX_OP_AVGPOOL;
        } else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 's' && op_type[3] == 'h') {
            op_enum = ONNX_OP_RESHAPE;
        } else if (op_type[0] == 'T' && op_type[1] == 'r' && op_type[2] == 'a' && op_type[3] == 'n') {
            op_enum = ONNX_OP_TRANSPOSE;
        } else if (op_type[0] == 'F' && op_type[1] == 'l' && op_type[2] == 'a' && op_type[3] == 't') {
            op_enum = ONNX_OP_FLATTEN;
        } else if (op_type[0] == 'B' && op_type[1] == 'a' && op_type[2] == 't' && op_type[3] == 'c') {
            op_enum = ONNX_OP_BATCHNORM;
        } else if (op_type[0] == 'G' && op_type[1] == 'e' && op_type[2] == 'm' && op_type[3] == 'm') {
            op_enum = ONNX_OP_GEMM;
        } else if (op_type[0] == 'C' && op_type[1] == 'o' && op_type[2] == 'n' && op_type[3] == 'c') {
            op_enum = ONNX_OP_CONCAT;
        } else if (op_type[0] == 'S' && op_type[1] == 'p' && op_type[2] == 'l' && op_type[3] == 'i' && op_type[4] == 't') {
            op_enum = ONNX_OP_SPLIT;
        } else if (op_type[0] == 'L' && op_type[1] == 'e' && op_type[2] == 'a' && op_type[3] == 'k') {
            op_enum = ONNX_OP_LEAKYRELU;
        } else if (op_type[0] == 'G' && op_type[1] == 'l' && op_type[2] == 'o' && op_type[3] == 'b') {
            op_enum = ONNX_OP_GLOBALAVERAGEPOOL;
        } else if (op_type[0] == 'S' && op_type[1] == 'q' && op_type[2] == 'u' && op_type[3] == 'e') {
            op_enum = ONNX_OP_SQUEEZE;
        } else if (op_type[0] == 'U' && op_type[1] == 'n' && op_type[2] == 's' && op_type[3] == 'q') {
            op_enum = ONNX_OP_UNSQUEEZE;
        } else if (op_type[0] == 'C' && op_type[1] == 'a' && op_type[2] == 's' && op_type[3] == 't') {
            op_enum = ONNX_OP_CAST;
        } else if (op_type[0] == 'A' && op_type[1] == 'b' && op_type[2] == 's' && op_type[3] == '\0') {
            op_enum = ONNX_OP_ABS;
        } else if (op_type[0] == 'N' && op_type[1] == 'e' && op_type[2] == 'g' && op_type[3] == '\0') {
            op_enum = ONNX_OP_NEG;
        } else if (op_type[0] == 'E' && op_type[1] == 'x' && op_type[2] == 'p' && op_type[3] == '\0') {
            op_enum = ONNX_OP_EXP;
        } else if (op_type[0] == 'L' && op_type[1] == 'o' && op_type[2] == 'g' && op_type[3] == '\0') {
            op_enum = ONNX_OP_LOG;
        } else if (op_type[0] == 'S' && op_type[1] == 'q' && op_type[2] == 'r' && op_type[3] == 't') {
            op_enum = ONNX_OP_SQRT;
        } else if (op_type[0] == 'C' && op_type[1] == 'e' && op_type[2] == 'i' && op_type[3] == 'l') {
            op_enum = ONNX_OP_CEIL;
        } else if (op_type[0] == 'F' && op_type[1] == 'l' && op_type[2] == 'o' && op_type[3] == 'o') {
            op_enum = ONNX_OP_FLOOR;
        } else if (op_type[0] == 'S' && op_type[1] == 'i' && op_type[2] == 'n' && op_type[3] == '\0') {
            op_enum = ONNX_OP_SIN;
        } else if (op_type[0] == 'C' && op_type[1] == 'o' && op_type[2] == 's' && op_type[3] == '\0') {
            op_enum = ONNX_OP_COS;
        } else if (op_type[0] == 'C' && op_type[1] == 'o' && op_type[2] == 'n' && op_type[3] == 's') {
            op_enum = ONNX_OP_CONSTANT;
        } else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 'd' && op_type[3] == 'u' && op_type[6] == 'S') {
            op_enum = ONNX_OP_REDUCESUM;
        } else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 'd' && op_type[3] == 'u' && op_type[6] == 'M' && op_type[7] == 'e') {
            op_enum = ONNX_OP_REDUCEMEAN;
        } else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 'd' && op_type[3] == 'u' && op_type[6] == 'M' && op_type[7] == 'a') {
            op_enum = ONNX_OP_REDUCEMAX;
        } else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 'd' && op_type[3] == 'u' && op_type[6] == 'M' && op_type[7] == 'i') {
            op_enum = ONNX_OP_REDUCEMIN;
        } else if (op_type[0] == 'C' && op_type[1] == 'l' && op_type[2] == 'i' && op_type[3] == 'p') {
            op_enum = ONNX_OP_CLIP;
        } else if (op_type[0] == 'I' && op_type[1] == 'd' && op_type[2] == 'e' && op_type[3] == 'n') {
            op_enum = ONNX_OP_IDENTITY;
        } else if (op_type[0] == 'L' && op_type[1] == 'R' && op_type[2] == 'N' && op_type[3] == '\0') {
            op_enum = ONNX_OP_LRN;
        } else if (op_type[0] == 'D' && op_type[1] == 'r' && op_type[2] == 'o' && op_type[3] == 'p') {
            op_enum = ONNX_OP_DROPOUT;
        }
    }
    
    /* Create node */
    ONNX_Node* node = ONNX_Graph_AddNode(graph, name, op_enum);
    if (!node) {
        return STATUS_ERROR_OUT_OF_MEMORY;
    }

    /* Assign attributes to node */
    node->attributes = attrs;

    /* Clean up uninitialized node attributes that might be used as tensor inputs
       if the parser couldn't properly distinguish inputs/outputs or attributes.
       Actually, our ONNX_Execute_Add strictly expects 2 inputs and 1 output.
       Let's check if there are 0 inputs and we need to skip. */
    if (num_inputs == 0 && op_enum == ONNX_OP_ADD) {
        /* Add node with 0 inputs is invalid, but if it happens, we shouldn't add it.
           However, let's let the runtime catch it, but our problem is it has 0 inputs.
           Why does it have 0 inputs? Maybe the parser missed them. */
    }

    /* Placeholder shape for missing tensors */
    ONNX_TensorShape placeholder_shape;
    placeholder_shape.ndim = 1;
    placeholder_shape.dims[0] = 1;
    placeholder_shape.total_elements = 1;
    
    /* Add inputs - find or create tensors */
    for (i = 0; i < num_inputs; i++) {
        if (inputs[i][0] != '\0') {
            ONNX_Tensor* input_tensor = ONNX_Graph_FindTensor(graph, inputs[i]);
            if (!input_tensor) {
                /* Create placeholder tensor (will be sized later) */
                input_tensor = ONNX_Graph_CreateTensor(graph, inputs[i], ONNX_DTYPE_FLOAT32, 
                                                       &placeholder_shape);
                if (!input_tensor) {
                    return STATUS_ERROR_OUT_OF_MEMORY;
                }
            }
            ONNX_Node_AddInput(node, input_tensor);
        }
    }
    
    /* Add outputs - find or create tensors */
    for (i = 0; i < num_outputs; i++) {
        if (outputs[i][0] != '\0') {
            ONNX_Tensor* output_tensor = ONNX_Graph_FindTensor(graph, outputs[i]);
            if (!output_tensor) {
                /* Create placeholder tensor (will be sized later) */
                output_tensor = ONNX_Graph_CreateTensor(graph, outputs[i], ONNX_DTYPE_FLOAT32,
                                                        &placeholder_shape);
                if (!output_tensor) {
                    return STATUS_ERROR_OUT_OF_MEMORY;
                }
            }
            ONNX_Node_AddOutput(node, output_tensor);
        }
    }
    
    return STATUS_OK;
}

/* Parse GraphProto */
static Status proto_parse_graph(ProtoReader* reader, uint64_t graph_msg_len, ONNX_Graph* graph)
{
    uint64_t end_pos = reader->pos + graph_msg_len;
    
    HAL_UART_PutString("[ONNX] Parsing graph...\n");
    
    while (reader->pos < end_pos && reader->pos < reader->size) {
        ProtoWireType wire_type;
        uint32_t field = proto_read_tag(reader, &wire_type);
        
        if (field == 1) {  /* node - repeated NodeProto */
            uint64_t node_len = proto_read_varint(reader);
            Status s = proto_parse_node(reader, node_len, graph);
            if (s != STATUS_OK) {
                HAL_UART_PutString("[ONNX] Error parsing node\n");
                return s;
            }
        }
        else if (field == 2) {  /* name - string */
            char graph_name[ONNX_MAX_NAME_LEN];
            proto_read_string(reader, graph_name, sizeof(graph_name));
            /* Copy to graph->name if needed */
        }
        else if (field == 5) {  /* initializer - repeated TensorProto */
            uint64_t tensor_len = proto_read_varint(reader);
            uint64_t tensor_end = reader->pos + tensor_len;

            ONNX_Tensor* tensor = NULL;
            /* BUG 2 + 3 FIX: pass tensor_end so the sub-parser knows its boundary */
            Status s = proto_parse_tensor(reader, tensor_end, graph, &tensor);
            if (s != STATUS_OK) {
                HAL_UART_PutString("[ONNX] Error parsing initializer\n");
            }
            /* Always advance to end of this tensor message (correct boundary) */
            reader->pos = tensor_end;
        }
        else if (field == 11) {  /* input - repeated ValueInfoProto */
            uint64_t len = proto_read_varint(reader);
            uint64_t vinfo_end = reader->pos + len;
            char name[ONNX_MAX_NAME_LEN] = {0};
            uint64_t dims[ONNX_MAX_DIMS] = {0};
            uint32_t ndim = 0;
            while (reader->pos < vinfo_end && reader->pos < reader->size) {
                ProtoWireType wtype;
                uint32_t vfield = proto_read_tag(reader, &wtype);
                if (vfield == 1) { /* name */
                    proto_read_string(reader, name, sizeof(name));
                } else if (vfield == 2) { /* type -> tensor_type */
                    if (wtype == WIRE_LENGTH_DELIMITED) {
                        /* Level 1: TypeProto message */
                        uint64_t type_len = proto_read_varint(reader);
                        uint64_t type_end = reader->pos + type_len;
                        while (reader->pos < type_end && reader->pos < reader->size) {
                            ProtoWireType l2_wtype;
                            uint32_t l2_field = proto_read_tag(reader, &l2_wtype);
                            if (l2_field == 1 && l2_wtype == WIRE_LENGTH_DELIMITED) {
                                /* Level 2: TypeProto.Tensor message */
                                uint64_t tensor_len = proto_read_varint(reader);
                                uint64_t tensor_end = reader->pos + tensor_len;
                                while (reader->pos < tensor_end && reader->pos < reader->size) {
                                    ProtoWireType l3_wtype;
                                    uint32_t l3_field = proto_read_tag(reader, &l3_wtype);
                                    if (l3_field == 2 && l3_wtype == WIRE_LENGTH_DELIMITED) {
                                        /* Level 3: TensorShapeProto message */
                                        uint64_t shape_len = proto_read_varint(reader);
                                        uint64_t shape_end = reader->pos + shape_len;
                                        while (reader->pos < shape_end && reader->pos < reader->size) {
                                            ProtoWireType l4_wtype;
                                            uint32_t l4_field = proto_read_tag(reader, &l4_wtype);
                                            if (l4_field == 1 && l4_wtype == WIRE_LENGTH_DELIMITED) {
                                                /* Level 4: TensorShapeProto.Dimension message */
                                                uint64_t dim_len = proto_read_varint(reader);
                                                uint64_t dim_end = reader->pos + dim_len;
                                                while (reader->pos < dim_end && reader->pos < reader->size) {
                                                    ProtoWireType l5_wtype;
                                                    uint32_t l5_field = proto_read_tag(reader, &l5_wtype);
                                                    if (l5_field == 1 && l5_wtype == WIRE_VARINT) {
                                                        /* dim_value */
                                                        uint64_t val = proto_read_varint(reader);
                                                        if (ndim < ONNX_MAX_DIMS) dims[ndim++] = val;
                                                    } else {
                                                        proto_skip_field(reader, l5_wtype);
                                                    }
                                                }
                                            } else {
                                                proto_skip_field(reader, l4_wtype);
                                            }
                                        }
                                    } else {
                                        proto_skip_field(reader, l3_wtype);
                                    }
                                }
                            } else {
                                proto_skip_field(reader, l2_wtype);
                            }
                        }
                    } else {
                        proto_skip_field(reader, wtype);
                    }
                } else {
                    proto_skip_field(reader, wtype);
                }
            }
            if (name[0] != '\0' && graph->num_inputs < ONNX_MAX_INPUTS) {
                ONNX_Tensor* t = ONNX_Graph_FindTensor(graph, name);
                if (!t) {
                    /* Create tensor with shape from ValueInfo */
                    ONNX_TensorShape shape = {0};
                    shape.ndim = ndim;
                    uint64_t total = (ndim > 0) ? 1 : 0;
                    for (uint32_t i = 0; i < ndim; i++) {
                        shape.dims[i] = dims[i];
                        total *= dims[i];
                    }
                    shape.total_elements = total;
                    t = ONNX_Graph_CreateTensor(graph, name, ONNX_DTYPE_FLOAT32, &shape);
                } else {
                    /* Update existing tensor's shape and data_size from ValueInfo */
                    t->shape.ndim = ndim;
                    uint64_t total = (ndim > 0) ? 1 : 0;
                    for (uint32_t i = 0; i < ndim; i++) {
                        t->shape.dims[i] = dims[i];
                        total *= dims[i];
                    }
                    t->shape.total_elements = total;
                    t->data_size = total * ONNX_GetDataTypeSize(t->dtype);
                }
                if (t) {
                    bool already_exists = false;
                    for (uint32_t i = 0; i < graph->num_inputs; i++) {
                        if (graph->inputs[i] == t) {
                            already_exists = true;
                            break;
                        }
                    }
                    if (!already_exists) {
                        graph->inputs[graph->num_inputs++] = t;
                    }
                }
            }
            reader->pos = vinfo_end;
        }
        else if (field == 12) {  /* output - repeated ValueInfoProto */
            uint64_t len = proto_read_varint(reader);
            uint64_t vinfo_end = reader->pos + len;
            char name[ONNX_MAX_NAME_LEN] = {0};
            uint64_t dims[ONNX_MAX_DIMS] = {0};
            uint32_t ndim = 0;
            while (reader->pos < vinfo_end && reader->pos < reader->size) {
                ProtoWireType wtype;
                uint32_t vfield = proto_read_tag(reader, &wtype);
                if (vfield == 1) { /* name */
                    proto_read_string(reader, name, sizeof(name));
                } else if (vfield == 2) { /* type -> tensor_type */
                    if (wtype == WIRE_LENGTH_DELIMITED) {
                        /* Level 1: TypeProto message */
                        uint64_t type_len = proto_read_varint(reader);
                        uint64_t type_end = reader->pos + type_len;
                        while (reader->pos < type_end && reader->pos < reader->size) {
                            ProtoWireType l2_wtype;
                            uint32_t l2_field = proto_read_tag(reader, &l2_wtype);
                            if (l2_field == 1 && l2_wtype == WIRE_LENGTH_DELIMITED) {
                                /* Level 2: TypeProto.Tensor message */
                                uint64_t tensor_len = proto_read_varint(reader);
                                uint64_t tensor_end = reader->pos + tensor_len;
                                while (reader->pos < tensor_end && reader->pos < reader->size) {
                                    ProtoWireType l3_wtype;
                                    uint32_t l3_field = proto_read_tag(reader, &l3_wtype);
                                    if (l3_field == 2 && l3_wtype == WIRE_LENGTH_DELIMITED) {
                                        /* Level 3: TensorShapeProto message */
                                        uint64_t shape_len = proto_read_varint(reader);
                                        uint64_t shape_end = reader->pos + shape_len;
                                        while (reader->pos < shape_end && reader->pos < reader->size) {
                                            ProtoWireType l4_wtype;
                                            uint32_t l4_field = proto_read_tag(reader, &l4_wtype);
                                            if (l4_field == 1 && l4_wtype == WIRE_LENGTH_DELIMITED) {
                                                /* Level 4: TensorShapeProto.Dimension message */
                                                uint64_t dim_len = proto_read_varint(reader);
                                                uint64_t dim_end = reader->pos + dim_len;
                                                while (reader->pos < dim_end && reader->pos < reader->size) {
                                                    ProtoWireType l5_wtype;
                                                    uint32_t l5_field = proto_read_tag(reader, &l5_wtype);
                                                    if (l5_field == 1 && l5_wtype == WIRE_VARINT) {
                                                        /* dim_value */
                                                        uint64_t val = proto_read_varint(reader);
                                                        if (ndim < ONNX_MAX_DIMS) dims[ndim++] = val;
                                                    } else {
                                                        proto_skip_field(reader, l5_wtype);
                                                    }
                                                }
                                            } else {
                                                proto_skip_field(reader, l4_wtype);
                                            }
                                        }
                                    } else {
                                        proto_skip_field(reader, l3_wtype);
                                    }
                                }
                            } else {
                                proto_skip_field(reader, l2_wtype);
                            }
                        }
                    } else {
                        proto_skip_field(reader, wtype);
                    }
                } else {
                    proto_skip_field(reader, wtype);
                }
            }
            if (name[0] != '\0' && graph->num_outputs < ONNX_MAX_OUTPUTS) {
                ONNX_Tensor* t = ONNX_Graph_FindTensor(graph, name);
                if (!t) {
                    /* Create tensor with shape from ValueInfo */
                    ONNX_TensorShape shape = {0};
                    shape.ndim = ndim;
                    uint64_t total = (ndim > 0) ? 1 : 0;
                    for (uint32_t i = 0; i < ndim; i++) {
                        shape.dims[i] = dims[i];
                        total *= dims[i];
                    }
                    shape.total_elements = total;
                    t = ONNX_Graph_CreateTensor(graph, name, ONNX_DTYPE_FLOAT32, &shape);
                } else {
                    /* Update existing tensor's shape and data_size from ValueInfo */
                    t->shape.ndim = ndim;
                    uint64_t total = (ndim > 0) ? 1 : 0;
                    for (uint32_t i = 0; i < ndim; i++) {
                        t->shape.dims[i] = dims[i];
                        total *= dims[i];
                    }
                    t->shape.total_elements = total;
                    t->data_size = total * ONNX_GetDataTypeSize(t->dtype);
                }
                if (t) {
                    bool already_exists = false;
                    for (uint32_t i = 0; i < graph->num_outputs; i++) {
                        if (graph->outputs[i] == t) {
                            already_exists = true;
                            break;
                        }
                    }
                    if (!already_exists) {
                        graph->outputs[graph->num_outputs++] = t;
                    }
                }
            }
            reader->pos = vinfo_end;
        }
        else {
            proto_skip_field(reader, wire_type);
        }
    }
    
    HAL_UART_PutString("[ONNX] Graph parsed: ");
    HAL_UART_PutDec(graph->num_nodes);
    HAL_UART_PutString(" nodes, ");
    HAL_UART_PutDec(graph->num_tensors);
    HAL_UART_PutString(" tensors\n");
    
    HAL_UART_PutString("[ONNX] Reader pos: ");
    HAL_UART_PutDec((uint32_t)reader->pos);
    HAL_UART_PutString(" / expected end: ");
    HAL_UART_PutDec((uint32_t)end_pos);
    HAL_UART_PutString("\n");
    
    return STATUS_OK;
}

Status ONNX_LoadProtobuf(ONNX_Graph* graph,
                          const uint8_t* protobuf_data,
                          uint64_t size)
{
    if (!graph || !protobuf_data || size == 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    HAL_UART_PutString("[ONNX] Loading protobuf format...\n");
    HAL_UART_PutString("  Data size: ");
    HAL_UART_PutDec((uint32_t)size);
    HAL_UART_PutString(" bytes\n");
    
    ProtoReader reader = {
        .data = protobuf_data,
        .size = size,
        .pos = 0
    };
    
    Status status = STATUS_OK;
    
    /* Parse ModelProto fields */
    /* Field numbers from onnx.proto:
     *   1: ir_version
     *   7: graph (ModelProto.graph)
     *   8: opset_import
     */
    
    while (reader.pos < reader.size) {
        ProtoWireType wire_type;
        uint32_t field_number = proto_read_tag(&reader, &wire_type);
        
        if (field_number == 1) {  /* ir_version */
            uint64_t ir_version = proto_read_varint(&reader);
            graph->ir_version = (uint32_t)ir_version;
            HAL_UART_PutString("  IR version: ");
            HAL_UART_PutDec((uint32_t)ir_version);
            HAL_UART_PutString("\n");
        }
        else if (field_number == 7) {  /* graph */
            /* Length-delimited, contains the whole GraphProto */
            uint64_t graph_size = proto_read_varint(&reader);
            HAL_UART_PutString("  Graph size: ");
            HAL_UART_PutDec((uint32_t)graph_size);
            HAL_UART_PutString(" bytes\n");
            
            /* Parse the graph */
            status = proto_parse_graph(&reader, graph_size, graph);
            if (status != STATUS_OK) {
                HAL_UART_PutString("[ONNX] Error: Failed to parse graph\n");
                return status;
            }
        }
        else {
            /* Skip unknown fields */
            proto_skip_field(&reader, wire_type);
        }
        
        /* Safety check */
        if (reader.pos > reader.size) {
            HAL_UART_PutString("[ONNX] Error: Corrupt protobuf data\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }
    }
    
    /* Clean up inputs list (remove initializers) */
    uint32_t actual_inputs = 0;
    for (uint32_t i = 0; i < graph->num_inputs; i++) {
        if (!graph->inputs[i]->is_initializer) {
            graph->inputs[actual_inputs++] = graph->inputs[i];
        }
    }
    graph->num_inputs = actual_inputs;

    uint32_t fused_bn = onnx_fuse_conv_batchnorm(graph);
    if (fused_bn > 0) {
        HAL_UART_PutString("[ONNX] Graph optimization: fused Conv+BatchNorm pairs = ");
        HAL_UART_PutDec(fused_bn);
        HAL_UART_PutString("\n");
    }

    uint32_t fused_relu = onnx_fuse_conv_relu(graph);
    if (fused_relu > 0) {
        HAL_UART_PutString("[ONNX] Graph optimization: fused Conv+ReLU pairs = ");
        HAL_UART_PutDec(fused_relu);
        HAL_UART_PutString("\n");
    }

    /* Build dependencies and schedule */
    HAL_UART_PutString("[ONNX] Building dependencies...\n");
    status = ONNX_Graph_BuildDependencies(graph);
    if (status != STATUS_OK) {
        HAL_UART_PutString("[ONNX] Error: Failed to build dependencies\n");
        return status;
    }
    
    HAL_UART_PutString("[ONNX] Generating execution schedule...\n");
    status = ONNX_Graph_GenerateSchedule(graph);
    if (status != STATUS_OK) {
        HAL_UART_PutString("[ONNX] Error: Failed to generate schedule\n");
        return status;
    }
    
    HAL_UART_PutString("[ONNX] ✓ Model loaded successfully!\n");
    
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Generic Load Function                                             */
/* ------------------------------------------------------------------ */

Status ONNX_LoadEmbedded(ONNX_Graph* graph,
                          const uint8_t* data,
                          uint64_t size,
                          ONNX_Format format)
{
    if (!graph || !data || size == 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    switch (format) {
        case ONNX_FORMAT_CUSTOM_BINARY:
            return ONNX_LoadCustomBinary(graph, data, size);
            
        case ONNX_FORMAT_PROTOBUF:
            return ONNX_LoadProtobuf(graph, data, size);
            
        default:
            return STATUS_ERROR_NOT_SUPPORTED;
    }
}

static uint32_t onnx_find_tensor_index(const ONNX_Graph* graph, const ONNX_Tensor* tensor)
{
    if (!graph || !tensor) {
        return ONNX_CUSTOM_INVALID_INDEX;
    }

    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        if (&graph->tensors[i] == tensor) {
            return i;
        }
    }

    return ONNX_CUSTOM_INVALID_INDEX;
}

/* ------------------------------------------------------------------ */
/*  Export to Custom Binary                                           */
/* ------------------------------------------------------------------ */

Status ONNX_ExportCustomBinary(const ONNX_Graph* graph,
                                 uint8_t* buffer,
                                 uint64_t buffer_size,
                                 uint64_t* bytes_written)
{
    if (!graph || !buffer || buffer_size < sizeof(ONNX_CustomHeader)) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    if (graph->num_nodes > ONNX_MAX_NODES ||
        graph->num_tensors > ONNX_MAX_TENSORS ||
        graph->num_inputs > ONNX_MAX_INPUTS ||
        graph->num_outputs > ONNX_MAX_OUTPUTS) {
        return STATUS_ERROR_INVALID_GRAPH;
    }

    uint64_t tensor_data_bytes = 0;
    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        const ONNX_Tensor* t = &graph->tensors[i];
        if (t->is_initializer && t->data && t->data_size > 0) {
            uint64_t next = tensor_data_bytes + t->data_size;
            if (next < tensor_data_bytes) {
                return STATUS_ERROR_INVALID_GRAPH;
            }
            tensor_data_bytes = next;
        }
    }

    uint64_t tensor_defs_bytes = (uint64_t)graph->num_tensors * (uint64_t)sizeof(ONNX_CustomTensorDef);
    uint64_t node_defs_bytes = (uint64_t)graph->num_nodes * (uint64_t)sizeof(ONNX_CustomNodeDef);
    uint64_t input_indices_bytes = (uint64_t)graph->num_inputs * sizeof(uint32_t);
    uint64_t output_indices_bytes = (uint64_t)graph->num_outputs * sizeof(uint32_t);

    uint64_t tensor_data_offset = sizeof(ONNX_CustomHeader) + tensor_defs_bytes +
                                  node_defs_bytes + input_indices_bytes + output_indices_bytes;

    uint64_t total_size = tensor_data_offset + tensor_data_bytes;
    if (total_size > buffer_size || total_size < tensor_data_offset) {
        return STATUS_ERROR_OUT_OF_MEMORY;
    }

    ONNX_CustomHeader header;
    header.magic = ONNX_CUSTOM_MAGIC;
    header.version = ONNX_CUSTOM_VERSION;
    header.num_nodes = graph->num_nodes;
    header.num_tensors = graph->num_tensors;
    header.num_inputs = graph->num_inputs;
    header.num_outputs = graph->num_outputs;
    header.tensor_data_offset = tensor_data_offset;
    memcpy(buffer, &header, sizeof(header));

    uint8_t* cursor = buffer + sizeof(ONNX_CustomHeader);
    uint64_t running_data_offset = 0;

    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        const ONNX_Tensor* t = &graph->tensors[i];
        ONNX_CustomTensorDef tdef;
        memset(&tdef, 0, sizeof(tdef));

        onnx_copy_name(tdef.name, t->name);
        tdef.dtype = (uint32_t)t->dtype;
        tdef.ndim = (t->shape.ndim <= ONNX_MAX_DIMS) ? t->shape.ndim : ONNX_MAX_DIMS;
        for (uint32_t d = 0; d < tdef.ndim; d++) {
            tdef.dims[d] = t->shape.dims[d];
        }
        tdef.is_initializer = t->is_initializer ? 1U : 0U;

        if (t->is_initializer && t->data && t->data_size > 0) {
            tdef.data_offset = running_data_offset;
            tdef.data_size = t->data_size;
            running_data_offset += t->data_size;
        }

        memcpy(cursor, &tdef, sizeof(tdef));
        cursor += sizeof(tdef);
    }

    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        const ONNX_Node* node = &graph->nodes[i];
        ONNX_CustomNodeDef ndef;
        memset(&ndef, 0, sizeof(ndef));

        onnx_copy_name(ndef.name, node->name);
        ndef.op_type = (uint32_t)node->op_type;

        ndef.num_inputs = (node->num_inputs <= ONNX_MAX_INPUTS) ? node->num_inputs : ONNX_MAX_INPUTS;
        ndef.num_outputs = (node->num_outputs <= ONNX_MAX_OUTPUTS) ? node->num_outputs : ONNX_MAX_OUTPUTS;

        for (uint32_t j = 0; j < ONNX_MAX_INPUTS; j++) {
            ndef.input_indices[j] = ONNX_CUSTOM_INVALID_INDEX;
        }
        for (uint32_t j = 0; j < ONNX_MAX_OUTPUTS; j++) {
            ndef.output_indices[j] = ONNX_CUSTOM_INVALID_INDEX;
        }

        for (uint32_t j = 0; j < ndef.num_inputs; j++) {
            uint32_t idx = onnx_find_tensor_index(graph, node->inputs[j]);
            if (idx == ONNX_CUSTOM_INVALID_INDEX) {
                return STATUS_ERROR_INVALID_GRAPH;
            }
            ndef.input_indices[j] = idx;
        }

        for (uint32_t j = 0; j < ndef.num_outputs; j++) {
            uint32_t idx = onnx_find_tensor_index(graph, node->outputs[j]);
            if (idx == ONNX_CUSTOM_INVALID_INDEX) {
                return STATUS_ERROR_INVALID_GRAPH;
            }
            ndef.output_indices[j] = idx;
        }

        ndef.kernel_shape_len = onnx_attr_len_clamp(node->attributes.kernel_shape_len);
        for (uint32_t j = 0; j < ndef.kernel_shape_len; j++) {
            ndef.kernel_shape[j] = node->attributes.kernel_shape[j];
        }

        ndef.strides_len = onnx_attr_len_clamp(node->attributes.strides_len);
        for (uint32_t j = 0; j < ndef.strides_len; j++) {
            ndef.strides[j] = node->attributes.strides[j];
        }

        ndef.pads_len = onnx_attr_len_clamp(node->attributes.pads_len);
        for (uint32_t j = 0; j < ndef.pads_len; j++) {
            ndef.pads[j] = node->attributes.pads[j];
        }

        ndef.dilations_len = onnx_attr_len_clamp(node->attributes.dilations_len);
        for (uint32_t j = 0; j < ndef.dilations_len; j++) {
            ndef.dilations[j] = node->attributes.dilations[j];
        }

        ndef.axis = node->attributes.axis;
        ndef.group = node->attributes.group;
        ndef.alpha = node->attributes.alpha;
        ndef.beta = node->attributes.beta;
        ndef.fuse_relu = node->attributes.fuse_relu ? 1U : 0U;
        ndef.keepdims = node->attributes.keepdims;

        ndef.perm_len = onnx_attr_len_clamp(node->attributes.perm_len);
        for (uint32_t j = 0; j < ndef.perm_len; j++) {
            ndef.perm[j] = node->attributes.perm[j];
        }

        memcpy(cursor, &ndef, sizeof(ndef));
        cursor += sizeof(ndef);
    }

    for (uint32_t i = 0; i < graph->num_inputs; i++) {
        uint32_t idx = onnx_find_tensor_index(graph, graph->inputs[i]);
        if (idx == ONNX_CUSTOM_INVALID_INDEX) {
            return STATUS_ERROR_INVALID_GRAPH;
        }
        memcpy(cursor, &idx, sizeof(idx));
        cursor += sizeof(idx);
    }

    for (uint32_t i = 0; i < graph->num_outputs; i++) {
        uint32_t idx = onnx_find_tensor_index(graph, graph->outputs[i]);
        if (idx == ONNX_CUSTOM_INVALID_INDEX) {
            return STATUS_ERROR_INVALID_GRAPH;
        }
        memcpy(cursor, &idx, sizeof(idx));
        cursor += sizeof(idx);
    }

    uint8_t* data_cursor = buffer + tensor_data_offset;
    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        const ONNX_Tensor* t = &graph->tensors[i];
        if (t->is_initializer && t->data && t->data_size > 0) {
            memcpy(data_cursor, t->data, (size_t)t->data_size);
            data_cursor += t->data_size;
        }
    }

    if (bytes_written) {
        *bytes_written = total_size;
    }

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Model Info                                                        */
/* ------------------------------------------------------------------ */

void ONNX_PrintModelInfo(const uint8_t* data,
                          uint64_t size,
                          ONNX_Format format)
{
    if (!data || size == 0) return;
    
    HAL_UART_PutString("\n========== Model Info ==========\n");
    HAL_UART_PutString("Format: ");
    
    if (format == ONNX_FORMAT_CUSTOM_BINARY) {
        HAL_UART_PutString("Custom Binary\n");
        
        if (size >= sizeof(ONNX_CustomHeader)) {
            const ONNX_CustomHeader* header = (const ONNX_CustomHeader*)data;
            
            if (header->magic == ONNX_CUSTOM_MAGIC) {
                HAL_UART_PutString("Version: ");
                HAL_UART_PutDec(header->version);
                HAL_UART_PutString("\n");
                
                HAL_UART_PutString("Nodes: ");
                HAL_UART_PutDec(header->num_nodes);
                HAL_UART_PutString("\n");
                
                HAL_UART_PutString("Tensors: ");
                HAL_UART_PutDec(header->num_tensors);
                HAL_UART_PutString("\n");
            } else {
                HAL_UART_PutString("Invalid magic number!\n");
            }
        }
    } else if (format == ONNX_FORMAT_PROTOBUF) {
        HAL_UART_PutString("ONNX Protobuf\n");
        HAL_UART_PutString("Size: ");
        HAL_UART_PutDec((uint32_t)size);
        HAL_UART_PutString(" bytes\n");
    }
    
    HAL_UART_PutString("================================\n\n");
}
