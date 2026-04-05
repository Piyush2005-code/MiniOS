/**
 * @file onnx_graph.c
 * @brief ONNX graph manipulation implementation
 */

#include "onnx/onnx_graph.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "kernel/kmem.h"
#include "status.h"

/* Simple string utilities */
static void str_copy(char* dst, const char* src, uint32_t max_len)
{
    uint32_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool str_equal(const char* a, const char* b)
{
    uint32_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        i++;
    }
    return a[i] == b[i];
}

static void mem_zero(void* ptr, uint64_t size)
{
    uint8_t* p = (uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Graph Initialization                                              */
/* ------------------------------------------------------------------ */

Status ONNX_Graph_Init(ONNX_Graph* graph, const char* name)
{
    if (!graph || !name) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Zero out the structure */
    mem_zero(graph, sizeof(ONNX_Graph));
    
    /* Set name */
    str_copy(graph->name, name, sizeof(graph->name));
    
    /* Initialize counters */
    graph->num_nodes = 0;
    graph->num_tensors = 0;
    graph->num_inputs = 0;
    graph->num_outputs = 0;
    graph->num_initializers = 0;
    graph->schedule_length = 0;
    
    /* No memory pool yet */
    graph->tensor_memory_pool = NULL;
    graph->tensor_memory_size = 0;
    graph->tensor_memory_used = 0;
    
    return STATUS_OK;
}

void ONNX_Graph_Cleanup(ONNX_Graph* graph)
{
    if (!graph) return;
    
    /* Free tensor memory pool if allocated */
    if (graph->tensor_memory_pool) {
        /* In a real implementation, you'd call your memory allocator's free */
        graph->tensor_memory_pool = NULL;
    }
    
    /* Zero out */
    mem_zero(graph, sizeof(ONNX_Graph));
}

/* ------------------------------------------------------------------ */
/*  Tensor Management                                                 */
/* ------------------------------------------------------------------ */

ONNX_Tensor* ONNX_Graph_CreateTensor(ONNX_Graph* graph,
                                       const char* name,
                                       ONNX_DataType dtype,
                                       const ONNX_TensorShape* shape)
{
    if (!graph || !name || !shape) {
        return NULL;
    }
    
    if (graph->num_tensors >= ONNX_MAX_TENSORS) {
        HAL_UART_PutString("[ONNX] Error: Maximum tensor limit reached\n");
        return NULL;
    }
    
    ONNX_Tensor* tensor = &graph->tensors[graph->num_tensors];
    graph->num_tensors++;
    
    /* Initialize tensor */
    str_copy(tensor->name, name, sizeof(tensor->name));
    tensor->dtype = dtype;
    tensor->shape = *shape;
    tensor->shape.total_elements = ONNX_GetShapeElements(shape);
    tensor->data = NULL;
    tensor->data_size = tensor->shape.total_elements * ONNX_GetDataTypeSize(dtype);
    tensor->is_initializer = false;
    
    return tensor;
}

ONNX_Tensor* ONNX_Graph_FindTensor(ONNX_Graph* graph, const char* name)
{
    if (!graph || !name) return NULL;
    
    for (uint32_t i = 0; i < graph->num_tensors; i++) {
        if (str_equal(graph->tensors[i].name, name)) {
            return &graph->tensors[i];
        }
    }
    
    return NULL;
}

Status ONNX_Graph_AllocateTensor(ONNX_Graph* graph, ONNX_Tensor* tensor)
{
    if (!graph || !tensor) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* Check if already allocated */
    if (tensor->data != NULL) {
        return STATUS_OK;
    }

    /* Require the arena to be set up */
    if (graph->tensor_arena == NULL) {
        return STATUS_ERROR_NOT_INITIALIZED;
    }

    /* Delegate to the arena allocator with cache-line alignment (FR-018) */
    tensor->data = KMEM_ArenaAlloc(graph->tensor_arena,
                                   tensor->data_size,
                                   KMEM_TENSOR_ALIGN);
    if (!tensor->data) {
        return STATUS_ERROR_OUT_OF_MEMORY;
    }

    /* Keep tensor_memory_used in sync for reporting */
    graph->tensor_memory_used = KMEM_ArenaGetUsed(graph->tensor_arena);

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Node Management                                                   */
/* ------------------------------------------------------------------ */

ONNX_Node* ONNX_Graph_AddNode(ONNX_Graph* graph,
                                const char* name,
                                ONNX_OperatorType op_type)
{
    if (!graph || !name) {
        return NULL;
    }
    
    if (graph->num_nodes >= ONNX_MAX_NODES) {
        HAL_UART_PutString("[ONNX] Error: Maximum node limit reached\n");
        return NULL;
    }
    
    ONNX_Node* node = &graph->nodes[graph->num_nodes];
    graph->num_nodes++;
    
    /* Initialize node */
    mem_zero(node, sizeof(ONNX_Node));
    str_copy(node->name, name, sizeof(node->name));
    node->op_type = op_type;
    node->num_inputs = 0;
    node->num_outputs = 0;
    node->exec_order = 0;
    node->exec_priority = 100;  /* Default priority */
    node->is_scheduled = false;
    node->num_dependencies = 0;
    node->exec_time_us = 0;
    node->exec_count = 0;
    
    return node;
}

Status ONNX_Node_AddInput(ONNX_Node* node, ONNX_Tensor* tensor)
{
    if (!node || !tensor) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    if (node->num_inputs >= ONNX_MAX_INPUTS) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    node->inputs[node->num_inputs] = tensor;
    node->num_inputs++;
    
    return STATUS_OK;
}

Status ONNX_Node_AddOutput(ONNX_Node* node, ONNX_Tensor* tensor)
{
    if (!node || !tensor) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    if (node->num_outputs >= ONNX_MAX_OUTPUTS) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    node->outputs[node->num_outputs] = tensor;
    node->num_outputs++;
    
    return STATUS_OK;
}

void ONNX_Node_SetPriority(ONNX_Node* node, uint32_t priority)
{
    if (node) {
        node->exec_priority = priority;
    }
}

/* ------------------------------------------------------------------ */
/*  Dependency Analysis                                               */
/* ------------------------------------------------------------------ */

Status ONNX_Graph_BuildDependencies(ONNX_Graph* graph)
{
    if (!graph) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* For each node, find which nodes produce its input tensors */
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        ONNX_Node* node = &graph->nodes[i];
        node->num_dependencies = 0;
        
        /* Check each input tensor */
        for (uint32_t j = 0; j < node->num_inputs; j++) {
            ONNX_Tensor* input_tensor = node->inputs[j];
            
            /* Skip if it's an initializer (constant) */
            if (input_tensor->is_initializer) {
                continue;
            }
            
            /* Find which node produces this tensor */
            for (uint32_t k = 0; k < graph->num_nodes; k++) {
                if (k == i) continue;  /* Skip self */
                
                ONNX_Node* producer = &graph->nodes[k];
                
                /* Check if this node produces the input tensor */
                for (uint32_t l = 0; l < producer->num_outputs; l++) {
                    if (producer->outputs[l] == input_tensor) {
                        /* Found dependency */
                        if (node->num_dependencies < ONNX_MAX_INPUTS) {
                            node->dependencies[node->num_dependencies] = producer;
                            node->num_dependencies++;
                        }
                        break;
                    }
                }
            }
        }
    }
    
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Scheduling                                                        */
/* ------------------------------------------------------------------ */

Status ONNX_Graph_GenerateSchedule(ONNX_Graph* graph)
{
    if (!graph) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Build dependencies first */
    Status status = ONNX_Graph_BuildDependencies(graph);
    if (status != STATUS_OK) {
        return status;
    }
    
    /* Queue-based Kahn topological sort: O(nodes + edges). */
    enum { ONNX_MAX_EDGES = ONNX_MAX_NODES * ONNX_MAX_INPUTS };
    static uint32_t in_degree[ONNX_MAX_NODES];
    static uint32_t queue[ONNX_MAX_NODES];
    static uint32_t head_edge[ONNX_MAX_NODES];
    static uint32_t edge_to[ONNX_MAX_EDGES];
    static uint32_t edge_next[ONNX_MAX_EDGES];

    const uint32_t n = graph->num_nodes;
    uint32_t edge_count = 0;

    for (uint32_t i = 0; i < n; i++) {
        in_degree[i] = graph->nodes[i].num_dependencies;
        head_edge[i] = 0xFFFFFFFFu;
        graph->nodes[i].is_scheduled = false;
    }

    /* Build adjacency list: dependency -> dependent. */
    for (uint32_t i = 0; i < n; i++) {
        ONNX_Node* node = &graph->nodes[i];
        for (uint32_t j = 0; j < node->num_dependencies; j++) {
            ONNX_Node* dep = node->dependencies[j];
            if (!dep) {
                continue;
            }

            uint64_t dep_idx64 = (uint64_t)(dep - graph->nodes);
            if (dep_idx64 >= n || edge_count >= ONNX_MAX_EDGES) {
                return STATUS_ERROR_INVALID_GRAPH;
            }

            uint32_t dep_idx = (uint32_t)dep_idx64;
            edge_to[edge_count] = i;
            edge_next[edge_count] = head_edge[dep_idx];
            head_edge[dep_idx] = edge_count;
            edge_count++;
        }
    }

    /* Initialize queue with indegree-0 nodes. */
    uint32_t q_head = 0;
    uint32_t q_tail = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            queue[q_tail++] = i;
        }
    }

    graph->schedule_length = 0;
    while (q_head < q_tail) {
        uint32_t node_idx = queue[q_head++];
        ONNX_Node* node = &graph->nodes[node_idx];

        graph->exec_schedule[graph->schedule_length] = node;
        node->exec_order = graph->schedule_length;
        node->is_scheduled = true;
        graph->schedule_length++;

        for (uint32_t e = head_edge[node_idx]; e != 0xFFFFFFFFu; e = edge_next[e]) {
            uint32_t dep_idx = edge_to[e];
            if (in_degree[dep_idx] > 0) {
                in_degree[dep_idx]--;
                if (in_degree[dep_idx] == 0) {
                    queue[q_tail++] = dep_idx;
                }
            }
        }
    }

    if (graph->schedule_length != n) {
        HAL_UART_PutString("[ONNX] Error: Could not generate schedule (cycle detected?)\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }

    return STATUS_OK;
}

Status ONNX_Graph_GenerateCustomSchedule(ONNX_Graph* graph)
{
    /* First generate normal schedule */
    Status status = ONNX_Graph_GenerateSchedule(graph);
    if (status != STATUS_OK) {
        return status;
    }
    
    /* Then sort by priority (stable sort to preserve dependencies) */
    /* Simple bubble sort for now */
    for (uint32_t i = 0; i < graph->schedule_length - 1; i++) {
        for (uint32_t j = 0; j < graph->schedule_length - i - 1; j++) {
            ONNX_Node* a = graph->exec_schedule[j];
            ONNX_Node* b = graph->exec_schedule[j + 1];
            
            /* Only swap if b has higher priority AND doesn't depend on a */
            if (b->exec_priority > a->exec_priority) {
                bool depends = false;
                for (uint32_t k = 0; k < b->num_dependencies; k++) {
                    if (b->dependencies[k] == a) {
                        depends = true;
                        break;
                    }
                }
                
                if (!depends) {
                    /* Swap */
                    graph->exec_schedule[j] = b;
                    graph->exec_schedule[j + 1] = a;
                }
            }
        }
    }
    
    /* Update exec_order */
    for (uint32_t i = 0; i < graph->schedule_length; i++) {
        graph->exec_schedule[i]->exec_order = i;
    }
    
    return STATUS_OK;
}

void ONNX_Graph_GetSchedule(ONNX_Graph* graph,
                             ONNX_Node*** schedule,
                             uint32_t* schedule_length)
{
    if (!graph || !schedule || !schedule_length) return;
    
    *schedule = graph->exec_schedule;
    *schedule_length = graph->schedule_length;
}

/* ------------------------------------------------------------------ */
/*  Graph Introspection                                               */
/* ------------------------------------------------------------------ */

void ONNX_Graph_Print(const ONNX_Graph* graph)
{
    if (!graph) return;
    
    HAL_UART_PutString("\n");
    HAL_UART_PutString("========== ONNX Graph ==========\n");
    HAL_UART_PutString("Name: ");
    HAL_UART_PutString(graph->name);
    HAL_UART_PutString("\n");
    
    HAL_UART_PutString("Nodes: ");
    HAL_UART_PutDec(graph->num_nodes);
    HAL_UART_PutString("\n");
    
    HAL_UART_PutString("Tensors: ");
    HAL_UART_PutDec(graph->num_tensors);
    HAL_UART_PutString("\n");
    
    HAL_UART_PutString("\n--- Graph Inputs ---\n");
    for (uint32_t i = 0; i < graph->num_inputs; i++) {
        ONNX_Tensor* t = graph->inputs[i];
        HAL_UART_PutString("  ");
        HAL_UART_PutString(t->name);
        HAL_UART_PutString(" : ");
        HAL_UART_PutString(ONNX_GetDataTypeName(t->dtype));
        HAL_UART_PutString(" [");
        for (uint32_t j = 0; j < t->shape.ndim; j++) {
            if (j > 0) HAL_UART_PutString(", ");
            HAL_UART_PutDec((uint32_t)t->shape.dims[j]);
        }
        HAL_UART_PutString("]\n");
    }
    
    HAL_UART_PutString("\n--- Computation Nodes ---\n");
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        ONNX_Node* node = (ONNX_Node*)&graph->nodes[i];
        HAL_UART_PutString("  [");
        HAL_UART_PutDec(i);
        HAL_UART_PutString("] ");
        HAL_UART_PutString(node->name);
        HAL_UART_PutString(" (");
        HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
        HAL_UART_PutString(")\n");
        
        HAL_UART_PutString("      Inputs: ");
        for (uint32_t j = 0; j < node->num_inputs; j++) {
            if (j > 0) HAL_UART_PutString(", ");
            HAL_UART_PutString(node->inputs[j]->name);
        }
        HAL_UART_PutString("\n");
        
        HAL_UART_PutString("      Outputs: ");
        for (uint32_t j = 0; j < node->num_outputs; j++) {
            if (j > 0) HAL_UART_PutString(", ");
            HAL_UART_PutString(node->outputs[j]->name);
        }
        HAL_UART_PutString("\n");
    }
    
    HAL_UART_PutString("\n--- Graph Outputs ---\n");
    for (uint32_t i = 0; i < graph->num_outputs; i++) {
        ONNX_Tensor* t = graph->outputs[i];
        HAL_UART_PutString("  ");
        HAL_UART_PutString(t->name);
        HAL_UART_PutString(" : ");
        HAL_UART_PutString(ONNX_GetDataTypeName(t->dtype));
        HAL_UART_PutString("\n");
    }
    
    HAL_UART_PutString("================================\n\n");
}

void ONNX_Graph_PrintStats(const ONNX_Graph* graph)
{
    if (!graph) return;
    
    HAL_UART_PutString("\n========== Performance Stats ==========\n");
    
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        const ONNX_Node* node = &graph->nodes[i];

        HAL_UART_PutString("[");
        HAL_UART_PutDec(i);
        HAL_UART_PutString("] ");

        if (node->name[0] != '\0') {
            HAL_UART_PutString(node->name);
        } else {
            HAL_UART_PutString("<unnamed>");
        }

        HAL_UART_PutString(" (");
        HAL_UART_PutString(ONNX_GetOperatorName(node->op_type));
        HAL_UART_PutString("): ");
        
        if (node->exec_count > 0) {
            uint64_t avg_us = node->exec_time_us / node->exec_count;
            HAL_UART_PutDec((uint32_t)avg_us);
            HAL_UART_PutString(" us (");
            HAL_UART_PutDec((uint32_t)node->exec_count);
            HAL_UART_PutString(" runs)\n");
        } else {
            HAL_UART_PutString("not executed\n");
        }
    }
    
    HAL_UART_PutString("======================================\n\n");
}

Status ONNX_Graph_Validate(const ONNX_Graph* graph)
{
    if (!graph) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Check that graph has at least one input and output */
    if (graph->num_inputs == 0) {
        HAL_UART_PutString("[ONNX] Error: Graph has no inputs\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    if (graph->num_outputs == 0) {
        HAL_UART_PutString("[ONNX] Error: Graph has no outputs\n");
        return STATUS_ERROR_INVALID_GRAPH;
    }
    
    /* Check that all node inputs are connected */
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        const ONNX_Node* node = &graph->nodes[i];
        
        if (node->num_inputs == 0) {
            HAL_UART_PutString("[ONNX] Error: Node ");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString(" has no inputs\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }
        
        if (node->num_outputs == 0) {
            HAL_UART_PutString("[ONNX] Error: Node ");
            HAL_UART_PutString(node->name);
            HAL_UART_PutString(" has no outputs\n");
            return STATUS_ERROR_INVALID_GRAPH;
        }
    }
    
    HAL_UART_PutString("[ONNX] Graph validation: OK\n");
    return STATUS_OK;
}
