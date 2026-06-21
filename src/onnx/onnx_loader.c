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
    
    /* Read header */
    const ONNX_CustomHeader* header = (const ONNX_CustomHeader*)data;
    
    /* Validate magic number */
    if (header->magic != ONNX_CUSTOM_MAGIC) {
        HAL_UART_PutString("[ONNX] Error: Invalid magic number\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    /* Check version */
    if (header->version != ONNX_CUSTOM_VERSION) {
        HAL_UART_PutString("[ONNX] Error: Unsupported version\n");
        return STATUS_ERROR_NOT_SUPPORTED;
    }
    
    HAL_UART_PutString("[ONNX] Loading custom binary format...\n");
    HAL_UART_PutString("  Nodes: ");
    HAL_UART_PutDec(header->num_nodes);
    HAL_UART_PutString("\n");
    HAL_UART_PutString("  Tensors: ");
    HAL_UART_PutDec(header->num_tensors);
    HAL_UART_PutString("\n");
    
    /* TODO: Parse node definitions */
    /* TODO: Parse tensor shapes */
    /* TODO: Load initializer data */
    
    HAL_UART_PutString("[ONNX] Custom binary loader: NOT YET IMPLEMENTED\n");
    HAL_UART_PutString("       See implementation guide below\n");
    
    return STATUS_ERROR_NOT_SUPPORTED;
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
        else if (field == 7) {  /* ints (repeated int64) */
            if (wire_type == WIRE_LENGTH_DELIMITED) {
                uint64_t len = proto_read_varint(reader);
                uint64_t end_packed = reader->pos + len;

                bool is_kernel = (name[0] == 'k' && name[1] == 'e' && name[2] == 'r' && name[3] == 'n' && name[4] == 'e' && name[5] == 'l');
                bool is_strides = (name[0] == 's' && name[1] == 't' && name[2] == 'r' && name[3] == 'i' && name[4] == 'd' && name[5] == 'e' && name[6] == 's');
                bool is_pads = (name[0] == 'p' && name[1] == 'a' && name[2] == 'd' && name[3] == 's');
                bool is_dilations = (name[0] == 'd' && name[1] == 'i' && name[2] == 'l' && name[3] == 'a' && name[4] == 't' && name[5] == 'i');

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
                    }
                }
            } else if (wire_type == WIRE_VARINT) {
                uint64_t val = proto_read_varint(reader);
                bool is_kernel = (name[0] == 'k' && name[1] == 'e' && name[2] == 'r' && name[3] == 'n' && name[4] == 'e' && name[5] == 'l');
                bool is_strides = (name[0] == 's' && name[1] == 't' && name[2] == 'r' && name[3] == 'i' && name[4] == 'd' && name[5] == 'e' && name[6] == 's');
                bool is_pads = (name[0] == 'p' && name[1] == 'a' && name[2] == 'd' && name[3] == 's');
                bool is_dilations = (name[0] == 'd' && name[1] == 'i' && name[2] == 'l' && name[3] == 'a' && name[4] == 't' && name[5] == 'i');

                if (is_kernel && attrs->kernel_shape_len < ONNX_MAX_ATTR_INTS) {
                    attrs->kernel_shape[attrs->kernel_shape_len++] = (int64_t)val;
                } else if (is_strides && attrs->strides_len < ONNX_MAX_ATTR_INTS) {
                    attrs->strides[attrs->strides_len++] = (int64_t)val;
                } else if (is_pads && attrs->pads_len < ONNX_MAX_ATTR_INTS) {
                    attrs->pads[attrs->pads_len++] = (int64_t)val;
                } else if (is_dilations && attrs->dilations_len < ONNX_MAX_ATTR_INTS) {
                    attrs->dilations[attrs->dilations_len++] = (int64_t)val;
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
                    HAL_UART_PutString("[PARSE] Tensor dim: ");
                    HAL_UART_PutHex((uint32_t)val);
                    HAL_UART_PutString("\n");
                }
            } else {
                proto_skip_field(reader, wire_type);
            }
        }
        else if (field == 2) {   /* data_type – int32 */
            dtype = (ONNX_DataType)proto_read_varint(reader);
            HAL_UART_PutString("[PARSE] Tensor dt: ");
            HAL_UART_PutHex((uint32_t)dtype);
            HAL_UART_PutString("\n");
        }
        else if (field == 4) {   /* float_data – packed repeated float32 */
            /* Python's onnx library emits initializers as float_data (field 4)
             * rather than raw_data (field 9). The bytes are identical on little-
             * endian hosts, so we just record the slice and copy it later. */
            proto_read_bytes(reader, &float_data, &float_data_len);
            HAL_UART_PutString("[PARSE] Found float_data len: ");
            HAL_UART_PutHex((uint32_t)float_data_len);
            HAL_UART_PutString("\n");
        }
        /* BUG 1 FIX: TensorProto.name is field 8, not field 3 */
        else if (field == 8) {   /* name – string */
            proto_read_string(reader, name, sizeof(name));
            HAL_UART_PutString("[PARSE] Tensor name: ");
            HAL_UART_PutString(name);
            HAL_UART_PutString("\n");
        }
        else if (field == 9) {   /* raw_data – bytes */
            proto_read_bytes(reader, &raw_data, &raw_data_len);
            HAL_UART_PutString("[PARSE] Found raw_data len: ");
            HAL_UART_PutHex((uint32_t)raw_data_len);
            HAL_UART_PutString("\n");
            break; /* raw_data is always last in a TensorProto */
        }
        else {
            proto_skip_field(reader, wire_type);
        }
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
        } else if (op_type[0] == 'D' && op_type[1] == 'r' && op_type[2] == 'o' && op_type[3] == 'p' && op_type[4] == 'o' && op_type[5] == 'u' && op_type[6] == 't') {
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
    
    /* Write header */
    ONNX_CustomHeader* header = (ONNX_CustomHeader*)buffer;
    header->magic = ONNX_CUSTOM_MAGIC;
    header->version = ONNX_CUSTOM_VERSION;
    header->num_nodes = graph->num_nodes;
    header->num_tensors = graph->num_tensors;
    header->num_inputs = graph->num_inputs;
    header->num_outputs = graph->num_outputs;
    header->tensor_data_offset = 0; /* TODO: Calculate */
    
    uint64_t offset = sizeof(ONNX_CustomHeader);
    
    /* TODO: Write node definitions */
    /* TODO: Write tensor shapes */
    /* TODO: Write initializer data */
    
    if (bytes_written) {
        *bytes_written = offset;
    }
    
    HAL_UART_PutString("[ONNX] Export: NOT YET IMPLEMENTED\n");
    
    return STATUS_ERROR_NOT_SUPPORTED;
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
