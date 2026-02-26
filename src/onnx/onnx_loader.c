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

/* Parse TensorProto and create ONNX_Tensor */
static Status proto_parse_tensor(ProtoReader* reader, ONNX_Graph* graph, ONNX_Tensor** out_tensor)
{
    char name[ONNX_MAX_NAME_LEN] = {0};
    ONNX_DataType dtype = ONNX_DTYPE_FLOAT32;
    uint64_t dims[ONNX_MAX_DIMS] = {0};
    uint32_t ndim = 0;
    const uint8_t* raw_data = NULL;
    uint64_t raw_data_len = 0;
    
    uint64_t start_pos = reader->pos;
    (void)start_pos;  /* May be used for bounds checking */
    
    /* Find tensor size first (we're inside length-delimited message) */
    /* The caller should have read the length */
    
    while (reader->pos < reader->size) {
        ProtoWireType wire_type;
        uint32_t field = proto_read_tag(reader, &wire_type);
        
        if (field == 0) break; /* End of message */
        
        if (field == 1) {  /* dims - repeated int64 */
            if (ndim < ONNX_MAX_DIMS) {
                dims[ndim++] = proto_read_varint(reader);
            } else {
                proto_read_varint(reader);
            }
        }
        else if (field == 2) {  /* data_type - int32 */
            dtype = (ONNX_DataType)proto_read_varint(reader);
        }
        else if (field == 3) {  /* name - string */
            proto_read_string(reader, name, sizeof(name));
        }
        else if (field == 9) {  /* raw_data - bytes */
            proto_read_bytes(reader, &raw_data, &raw_data_len);
            /* After reading raw_data, we're likely at end of tensor */
            break;
        }
        else {
            proto_skip_field(reader, wire_type);
        }
        
        /* Safety: don't read beyond reasonable tensor message size */
        if (reader->pos - start_pos > 100000) {
            return STATUS_ERROR_INVALID_GRAPH;
        }
    }
    
    /* Build shape structure */
    ONNX_TensorShape shape;
    shape.ndim = ndim;
    uint64_t total = 1;
    uint32_t i;
    for (i = 0; i < ndim; i++) {
        shape.dims[i] = dims[i];
        total *= dims[i];
    }
    for (; i < ONNX_MAX_DIMS; i++) {
        shape.dims[i] = 0;
    }
    shape.total_elements = total;
    
    /* Create tensor in graph */
    ONNX_Tensor* tensor = ONNX_Graph_CreateTensor(graph, name, dtype, &shape);
    if (!tensor) {
        return STATUS_ERROR_OUT_OF_MEMORY;
    }
    
    tensor->is_initializer = 1;
    
    /* Note: raw_data will be copied to tensor->data during allocation phase */
    /* For now, store pointer (will copy later) */
    if (raw_data && raw_data_len > 0) {
        tensor->data = (void*)raw_data;  /* Temporary - will be copied */
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
        else {
            proto_skip_field(reader, wire_type);
        }
    }
    
    /* Map op_type string to enum */
    ONNX_OperatorType op_enum = ONNX_OP_ADD;  /* Default */
    
    if (op_type[0] != '\0') {
        /* Simple string comparison (no strcmp in freestanding) */
        if (op_type[0] == 'A' && op_type[1] == 'd' && op_type[2] == 'd' && op_type[3] == '\0') {
            op_enum = ONNX_OP_ADD;
        }
        else if (op_type[0] == 'M' && op_type[1] == 'a' && op_type[2] == 't' && 
                 op_type[3] == 'M' && op_type[4] == 'u' && op_type[5] == 'l' && op_type[6] == '\0') {
            op_enum = ONNX_OP_MATMUL;
        }
        else if (op_type[0] == 'R' && op_type[1] == 'e' && op_type[2] == 'l' && 
                 op_type[3] == 'u' && op_type[4] == '\0') {
            op_enum = ONNX_OP_RELU;
        }
        else if (op_type[0] == 'C' && op_type[1] == 'o' && op_type[2] == 'n' && op_type[3] == 'v') {
            op_enum = ONNX_OP_CONV;
        }
        /* Add more operators as needed */
    }
    
    /* Create node */
    ONNX_Node* node = ONNX_Graph_AddNode(graph, name, op_enum);
    if (!node) {
        return STATUS_ERROR_OUT_OF_MEMORY;
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
            Status s = proto_parse_tensor(reader, graph, &tensor);
            if (s != STATUS_OK) {
                HAL_UART_PutString("[ONNX] Error parsing initializer\n");
                /* Skip this tensor */
                reader->pos = tensor_end;
            } else {
                /* Ensure we're at the right position */
                reader->pos = tensor_end;
            }
        }
        else if (field == 11) {  /* input - repeated ValueInfoProto */
            uint64_t len = proto_read_varint(reader);
            /* TODO: Parse input info */
            reader->pos += len;
        }
        else if (field == 12) {  /* output - repeated ValueInfoProto */
            uint64_t len = proto_read_varint(reader);
            /* TODO: Parse output info */
            reader->pos += len;
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
