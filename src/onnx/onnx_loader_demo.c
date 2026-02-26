/**
 * @file onnx_loader_demo.c
 * @brief Demo of ONNX model loading from embedded data
 */

#include "onnx/onnx_loader.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "hal/uart.h"
#include "test_model.h"
#include "status.h"

void ONNX_LoaderDemo(void)
{
    HAL_UART_PutString("\n========================================\n");
    HAL_UART_PutString("   ONNX Model Loader Demo\n");
    HAL_UART_PutString("========================================\n\n");
    
    /* Create graph */
    ONNX_Graph graph;
    ONNX_Graph_Init(&graph, "LoadedModel");
    
    /* Print model info */
    ONNX_PrintModelInfo(test_onnx_model, test_onnx_model_len, ONNX_FORMAT_PROTOBUF);
    
    /* Load model from embedded protobuf */
    HAL_UART_PutString("[Demo] Loading model from embedded protobuf...\n");
    Status status = ONNX_LoadEmbedded(&graph, 
                                       test_onnx_model, 
                                       test_onnx_model_len,
                                       ONNX_FORMAT_PROTOBUF);
    
    if (status != STATUS_OK) {
        HAL_UART_PutString("[Demo] Load failed with status: ");
        HAL_UART_PutDec((uint32_t)status);
        HAL_UART_PutString("\n");
        return;
    }
    
    HAL_UART_PutString("[Demo] Model loaded successfully!\n\n");
    
    /* Print graph structure */
    ONNX_Graph_Print(&graph);
    
    HAL_UART_PutString("\n========================================\n\n");
}
