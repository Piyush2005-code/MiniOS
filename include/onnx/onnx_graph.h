/**
 * @file onnx_graph.h
 * @brief ONNX computation graph manipulation API
 *
 * Provides functions for loading, parsing, and manipulating ONNX
 * computation graphs. Supports custom scheduling and graph optimization.
 */

#ifndef ONNX_GRAPH_H
#define ONNX_GRAPH_H

#include "onnx_types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Graph Initialization & Cleanup                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize an empty ONNX graph
 * 
 * @param graph Pointer to graph structure to initialize
 * @param name Graph name
 * @return STATUS_OK on success
 */
Status ONNX_Graph_Init(ONNX_Graph* graph, const char* name);

/**
 * @brief Free all resources associated with a graph
 * 
 * @param graph Pointer to graph to cleanup
 */
void ONNX_Graph_Cleanup(ONNX_Graph* graph);

/* ------------------------------------------------------------------ */
/*  Graph Loading & Parsing                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Load ONNX model from memory buffer
 * 
 * Parses a serialized ONNX protobuf model from memory and builds
 * the internal graph representation.
 * 
 * @param graph Pointer to graph structure
 * @param buffer Pointer to ONNX model data (protobuf format)
 * @param buffer_size Size of buffer in bytes
 * @return STATUS_OK on success, STATUS_ERROR_INVALID_GRAPH on parse error
 */
Status ONNX_Graph_LoadFromMemory(ONNX_Graph* graph, 
                                   const void* buffer,
                                   uint64_t buffer_size);

/**
 * @brief Load ONNX model from embedded C array
 * 
 * Loads a model that was embedded as a C array using xxd or similar tools.
 * 
 * @param graph Pointer to graph structure
 * @param model_data Pointer to embedded model array
 * @param model_size Size of embedded model
 * @return STATUS_OK on success
 */
Status ONNX_Graph_LoadEmbedded(ONNX_Graph* graph,
                                 const uint8_t* model_data,
                                 uint64_t model_size);

/* ------------------------------------------------------------------ */
/*  Tensor Management                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a new tensor in the graph
 * 
 * @param graph Pointer to graph
 * @param name Tensor name
 * @param dtype Data type
 * @param shape Tensor shape
 * @return Pointer to created tensor, or NULL on error
 */
ONNX_Tensor* ONNX_Graph_CreateTensor(ONNX_Graph* graph,
                                       const char* name,
                                       ONNX_DataType dtype,
                                       const ONNX_TensorShape* shape);

/**
 * @brief Find a tensor by name
 * 
 * @param graph Pointer to graph
 * @param name Tensor name to search for
 * @return Pointer to tensor, or NULL if not found
 */
ONNX_Tensor* ONNX_Graph_FindTensor(ONNX_Graph* graph, const char* name);

/**
 * @brief Allocate memory for a tensor
 * 
 * @param graph Pointer to graph
 * @param tensor Pointer to tensor
 * @return STATUS_OK on success
 */
Status ONNX_Graph_AllocateTensor(ONNX_Graph* graph, ONNX_Tensor* tensor);

/* ------------------------------------------------------------------ */
/*  Node/Operator Management                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Add a new operator node to the graph
 * 
 * @param graph Pointer to graph
 * @param name Node name
 * @param op_type Operator type
 * @return Pointer to created node, or NULL on error
 */
ONNX_Node* ONNX_Graph_AddNode(ONNX_Graph* graph,
                                const char* name,
                                ONNX_OperatorType op_type);

/**
 * @brief Connect an input tensor to a node
 * 
 * @param node Pointer to node
 * @param tensor Pointer to input tensor
 * @return STATUS_OK on success
 */
Status ONNX_Node_AddInput(ONNX_Node* node, ONNX_Tensor* tensor);

/**
 * @brief Connect an output tensor to a node
 * 
 * @param node Pointer to node
 * @param tensor Pointer to output tensor
 * @return STATUS_OK on success
 */
Status ONNX_Node_AddOutput(ONNX_Node* node, ONNX_Tensor* tensor);

/**
 * @brief Set node execution priority for custom scheduling
 * 
 * @param node Pointer to node
 * @param priority Priority value (higher = higher priority)
 */
void ONNX_Node_SetPriority(ONNX_Node* node, uint32_t priority);

/* ------------------------------------------------------------------ */
/*  Graph Analysis & Scheduling                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Build dependency graph between nodes
 * 
 * Analyzes the graph and establishes dependencies between nodes based
 * on their input/output tensors.
 * 
 * @param graph Pointer to graph
 * @return STATUS_OK on success
 */
Status ONNX_Graph_BuildDependencies(ONNX_Graph* graph);

/**
 * @brief Generate default execution schedule (topological order)
 * 
 * Creates an execution schedule that respects data dependencies.
 * 
 * @param graph Pointer to graph
 * @return STATUS_OK on success, STATUS_ERROR_INVALID_GRAPH if cyclic
 */
Status ONNX_Graph_GenerateSchedule(ONNX_Graph* graph);

/**
 * @brief Generate custom execution schedule based on priorities
 * 
 * Creates an execution schedule that considers both dependencies and
 * node priorities. Useful for optimizing execution order.
 * 
 * @param graph Pointer to graph
 * @return STATUS_OK on success
 */
Status ONNX_Graph_GenerateCustomSchedule(ONNX_Graph* graph);

/**
 * @brief Manually reorder execution schedule
 * 
 * Allows you to manually specify the execution order. Useful for
 * advanced scheduling strategies.
 * 
 * @param graph Pointer to graph
 * @param node_order Array of node indices in desired execution order
 * @param num_nodes Number of nodes in the order
 * @return STATUS_OK on success, STATUS_ERROR_INVALID_ARGUMENT if invalid order
 */
Status ONNX_Graph_SetCustomSchedule(ONNX_Graph* graph,
                                      uint32_t* node_order,
                                      uint32_t num_nodes);

/**
 * @brief Get the current execution schedule
 * 
 * Returns the ordered list of nodes to execute.
 * 
 * @param graph Pointer to graph
 * @param schedule Output: array of node pointers
 * @param schedule_length Output: length of schedule
 */
void ONNX_Graph_GetSchedule(ONNX_Graph* graph,
                             ONNX_Node*** schedule,
                             uint32_t* schedule_length);

/* ------------------------------------------------------------------ */
/*  Graph Manipulation                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Select a subset of nodes for execution
 * 
 * Allows you to execute only specific operators in the graph.
 * Useful for debugging or selective computation.
 * 
 * @param graph Pointer to graph
 * @param node_names Array of node names to enable
 * @param num_nodes Number of nodes in array
 * @return STATUS_OK on success
 */
Status ONNX_Graph_SelectNodes(ONNX_Graph* graph,
                                const char** node_names,
                                uint32_t num_nodes);

/**
 * @brief Disable specific nodes in the graph
 * 
 * @param graph Pointer to graph
 * @param node_names Array of node names to disable
 * @param num_nodes Number of nodes in array
 * @return STATUS_OK on success
 */
Status ONNX_Graph_DisableNodes(ONNX_Graph* graph,
                                 const char** node_names,
                                 uint32_t num_nodes);

/**
 * @brief Reset graph to enable all nodes
 * 
 * @param graph Pointer to graph
 */
void ONNX_Graph_ResetSelection(ONNX_Graph* graph);

/* ------------------------------------------------------------------ */
/*  Graph Introspection                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Print graph structure to UART
 * 
 * Prints a human-readable representation of the graph structure,
 * including all nodes, tensors, and connections.
 * 
 * @param graph Pointer to graph
 */
void ONNX_Graph_Print(const ONNX_Graph* graph);

/**
 * @brief Print execution statistics
 * 
 * Prints performance statistics for each node in the graph.
 * 
 * @param graph Pointer to graph
 */
void ONNX_Graph_PrintStats(const ONNX_Graph* graph);

/**
 * @brief Validate graph structure
 * 
 * Checks that the graph is well-formed: all tensors are connected,
 * shapes are compatible, no cycles exist, etc.
 * 
 * @param graph Pointer to graph
 * @return STATUS_OK if valid, error code otherwise
 */
Status ONNX_Graph_Validate(const ONNX_Graph* graph);

#endif /* ONNX_GRAPH_H */
